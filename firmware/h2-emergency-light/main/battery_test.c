#include "battery_test.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "relay.h"

typedef struct {
    uint16_t test_id;
    uint16_t duration_s;
    uint16_t voltage_before_mv;
    uint16_t voltage_min_mv;
    int16_t current_peak_ma;
    int16_t temperature_max_centi_c;
    int64_t current_sum_ma;
    int64_t power_sum_mw;
    uint8_t sample_count;
    uint8_t last_flags;
} battery_test_stats_t;

static const char *TAG = "battery_test";
static volatile bool s_active;
static volatile int64_t s_end_us;
static SemaphoreHandle_t s_mutex;
static battery_test_stats_t s_stats;
static battery_test_schedule_t s_schedule;
static bool s_schedule_valid;
static int64_t s_schedule_due_us;
static battery_test_report_t s_pending_report;
static bool s_pending_report_valid;
static int64_t s_report_retry_due_us;
static uint16_t s_last_voltage_mv;
static uint32_t s_device_id;
static battery_test_report_sender_t s_report_sender;
static battery_test_transport_ready_t s_transport_ready;
static battery_test_wake_callback_t s_wake_telemetry;

static void clear_persisted_schedule(void)
{
    nvs_handle_t nvs;
    if (nvs_open("battery_test", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "schedule");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void persist_schedule(const battery_test_schedule_t *schedule)
{
    nvs_handle_t nvs;
    if (nvs_open("battery_test", NVS_READWRITE, &nvs) == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_blob(nvs, "schedule", schedule, sizeof(*schedule)));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvs));
        nvs_close(nvs);
    }
}

static void persist_pending_report(const battery_test_report_t *report)
{
    nvs_handle_t nvs;
    if (nvs_open("battery_test", NVS_READWRITE, &nvs) == ESP_OK) {
        if (report) nvs_set_blob(nvs, "report", report, sizeof(*report));
        else nvs_erase_key(nvs, "report");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void load_persisted_schedule(void)
{
    nvs_handle_t nvs;
    size_t size = sizeof(s_schedule);
    if (nvs_open("battery_test", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "schedule", &s_schedule, &size) == ESP_OK &&
            size == sizeof(s_schedule) && s_schedule.version == EMERGENCY_TEST_PROTOCOL_VERSION) {
            s_schedule_valid = true;
            s_schedule_due_us = esp_timer_get_time() + (int64_t)s_schedule.start_delay_s * 1000000LL;
            ESP_LOGW(TAG, "Restored test %u group %u; starts in %" PRIu32 " seconds",
                     s_schedule.test_id, s_schedule.group_index, s_schedule.start_delay_s);
        }
        nvs_close(nvs);
    }
}

static void load_pending_report(void)
{
    nvs_handle_t nvs;
    size_t size = sizeof(s_pending_report);
    if (nvs_open("battery_test", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "report", &s_pending_report, &size) == ESP_OK &&
            size == sizeof(s_pending_report) && s_pending_report.version == EMERGENCY_TEST_PROTOCOL_VERSION) {
            // An older firmware may have saved this report under the previous ID.
            s_pending_report.device_id = s_device_id;
            s_pending_report_valid = true;
            ESP_LOGW(TAG, "Restored pending report for test %u", s_pending_report.test_id);
        }
        nvs_close(nvs);
    }
}

static battery_test_report_t make_report(const battery_test_stats_t *stats, uint8_t forced_result)
{
    battery_test_report_t report = {
        .version = EMERGENCY_TEST_PROTOCOL_VERSION,
        .result = forced_result,
        .test_id = stats->test_id,
        .device_id = s_device_id,
        .voltage_before_mv = stats->voltage_before_mv,
        .voltage_min_mv = stats->voltage_min_mv == UINT16_MAX ? 0 : stats->voltage_min_mv,
        .current_peak_ma = stats->current_peak_ma,
        .temperature_max_centi_c = stats->temperature_max_centi_c,
        .duration_s = stats->duration_s,
        .sample_count = stats->sample_count,
        .flags = stats->last_flags,
    };
    if (stats->sample_count > 0) {
        report.current_avg_ma = (int16_t)(stats->current_sum_ma / stats->sample_count);
        report.power_avg_mw = (int32_t)(stats->power_sum_mw / stats->sample_count);
    }
    if (forced_result == BATTERY_TEST_PASS) {
        if (stats->sample_count == 0 || report.voltage_min_mv == 0) report.result = BATTERY_TEST_SENSOR_INVALID;
        else if (report.voltage_min_mv < APP_TEST_MIN_VOLTAGE_MV) report.result = BATTERY_TEST_LOW_VOLTAGE;
        else if (report.current_peak_ma < APP_TEST_MIN_CURRENT_MA) report.result = BATTERY_TEST_LOW_CURRENT;
        else if (report.temperature_max_centi_c != INT16_MIN &&
                 report.temperature_max_centi_c >= APP_TEST_MAX_TEMPERATURE_CC) {
            report.result = BATTERY_TEST_HIGH_TEMPERATURE;
        }
    }
    return report;
}

static void send_report(const battery_test_report_t *report)
{
    esp_err_t status = s_report_sender(report);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (status == ESP_OK) {
        s_pending_report_valid = false;
    } else {
        s_pending_report = *report;
        s_pending_report_valid = true;
        s_report_retry_due_us = esp_timer_get_time() + APP_REPORT_RETRY_US;
    }
    xSemaphoreGive(s_mutex);
    persist_pending_report(status == ESP_OK ? NULL : report);
    if (status == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Test %u report not sent: Zigbee network unavailable", report->test_id);
    }
}

static void stop_test(uint8_t result)
{
    relay_set_test_active(false);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool was_active = s_active;
    s_active = false;
    s_end_us = 0;
    battery_test_stats_t stats = s_stats;
    xSemaphoreGive(s_mutex);

    if (!was_active) return;
    battery_test_report_t report = make_report(&stats, result);
    if (s_wake_telemetry) s_wake_telemetry();
    send_report(&report);
    ESP_LOGI(TAG, "Battery test %u stopped; result=%u, samples=%u, relay returned to NC",
             report.test_id, report.result, report.sample_count);
}

static void send_rejected_report(uint16_t test_id, uint16_t duration_s, uint8_t result)
{
    battery_test_stats_t stats = {
        .test_id = test_id,
        .duration_s = duration_s,
        .voltage_before_mv = s_last_voltage_mv,
        .voltage_min_mv = UINT16_MAX,
        .temperature_max_centi_c = INT16_MIN,
    };
    battery_test_report_t report = make_report(&stats, result);
    send_report(&report);
}

esp_err_t battery_test_start(uint16_t duration_s, uint16_t test_id)
{
    if (duration_s == 0) {
        stop_test(BATTERY_TEST_ABORTED);
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(duration_s <= APP_MAX_TEST_DURATION_S, ESP_ERR_INVALID_ARG, TAG,
                        "Test exceeds %u seconds", APP_MAX_TEST_DURATION_S);
    if (!relay_ac_present()) {
        send_rejected_report(test_id, duration_s, BATTERY_TEST_AC_NOT_PRESENT);
        ESP_LOGW(TAG, "Cannot start test %u: AC is already absent", test_id);
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_active) {
        xSemaphoreGive(s_mutex);
        send_rejected_report(test_id, duration_s, BATTERY_TEST_ABORTED);
        return ESP_ERR_INVALID_STATE;
    }
    s_stats = (battery_test_stats_t) {
        .test_id = test_id,
        .duration_s = duration_s,
        .voltage_before_mv = s_last_voltage_mv,
        .voltage_min_mv = UINT16_MAX,
        .temperature_max_centi_c = INT16_MIN,
    };
    s_end_us = esp_timer_get_time() + (int64_t)duration_s * 1000000LL;
    s_active = true;
    xSemaphoreGive(s_mutex);

    relay_set_test_active(true);
    if (s_wake_telemetry) s_wake_telemetry();
    ESP_LOGW(TAG, "Battery test %u started for %u seconds; AC relay energized", test_id, duration_s);
    return ESP_OK;
}

esp_err_t battery_test_schedule(const battery_test_schedule_t *schedule)
{
    ESP_RETURN_ON_FALSE(schedule && schedule->version == EMERGENCY_TEST_PROTOCOL_VERSION &&
                        schedule->duration_s > 0 && schedule->duration_s <= APP_MAX_TEST_DURATION_S,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid test schedule");
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_schedule = *schedule;
    s_schedule_valid = true;
    s_schedule_due_us = esp_timer_get_time() + (int64_t)schedule->start_delay_s * 1000000LL;
    xSemaphoreGive(s_mutex);
    persist_schedule(schedule);
    ESP_LOGI(TAG, "Test %u scheduled: group=%u starts_in=%" PRIu32 "s duration=%us",
             schedule->test_id, schedule->group_index, schedule->start_delay_s, schedule->duration_s);
    return ESP_OK;
}

bool battery_test_is_active(void)
{
    return s_active;
}

void battery_test_record(const emergency_telemetry_t *sample, bool voltage_valid)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (voltage_valid) s_last_voltage_mv = sample->vpack_mv;
    if (s_active && sample->vpack_mv > 0) {
        if (sample->vpack_mv < s_stats.voltage_min_mv) s_stats.voltage_min_mv = sample->vpack_mv;
        int16_t current_abs = sample->ipack_ma < 0 ? -sample->ipack_ma : sample->ipack_ma;
        if (current_abs > s_stats.current_peak_ma) s_stats.current_peak_ma = current_abs;
        if (sample->temp_centi_c != INT16_MIN &&
            (s_stats.temperature_max_centi_c == INT16_MIN ||
             sample->temp_centi_c > s_stats.temperature_max_centi_c)) {
            s_stats.temperature_max_centi_c = sample->temp_centi_c;
        }
        s_stats.current_sum_ma += sample->ipack_ma;
        s_stats.power_sum_mw += sample->power_mw;
        s_stats.last_flags = sample->flags;
        if (s_stats.sample_count < UINT8_MAX) s_stats.sample_count++;
    }
    xSemaphoreGive(s_mutex);
}

static void battery_test_task(void *arg)
{
    bool previous_button = true;
    while (true) {
        bool button = !relay_test_button_pressed();
        if (previous_button && !button) {
            if (s_active) stop_test(BATTERY_TEST_ABORTED);
            else (void)battery_test_start(APP_LOCAL_TEST_DURATION_S, 0);
        }
        previous_button = button;

        int64_t now = esp_timer_get_time();
        if (s_active && now >= s_end_us) stop_test(BATTERY_TEST_PASS);
        if (s_schedule_valid && now >= s_schedule_due_us) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            battery_test_schedule_t schedule = s_schedule;
            s_schedule_valid = false;
            xSemaphoreGive(s_mutex);
            clear_persisted_schedule();
            (void)battery_test_start(schedule.duration_s, schedule.test_id);
        }
        if (s_pending_report_valid && s_transport_ready() && now >= s_report_retry_due_us) {
            battery_test_report_t report;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            report = s_pending_report;
            xSemaphoreGive(s_mutex);
            send_report(&report);
        }
        vTaskDelay(pdMS_TO_TICKS(APP_TEST_TASK_POLL_MS));
    }
}

esp_err_t battery_test_init(uint32_t device_id, battery_test_report_sender_t report_sender,
                            battery_test_transport_ready_t transport_ready,
                            battery_test_wake_callback_t wake_telemetry)
{
    ESP_RETURN_ON_FALSE(report_sender && transport_ready, ESP_ERR_INVALID_ARG, TAG,
                        "Report transport callbacks are required");
    s_device_id = device_id;
    s_report_sender = report_sender;
    s_transport_ready = transport_ready;
    s_wake_telemetry = wake_telemetry;
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "Test mutex allocation failed");
    load_persisted_schedule();
    load_pending_report();
    return ESP_OK;
}

esp_err_t battery_test_start_task(void)
{
    return xTaskCreate(battery_test_task, "relay_test", 3072, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
