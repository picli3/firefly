#include <math.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TELEMETRY_ENDPOINT       1
#define TELEMETRY_CLUSTER_ID     0xFC00
#define TELEMETRY_COMMAND_ID     0x01
#define START_TEST_COMMAND_ID    0x02
#define SCHEDULE_TEST_COMMAND_ID 0x03
#define TEST_REPORT_COMMAND_ID   0x04
#define TELEMETRY_VERSION        1
#define TEST_PROTOCOL_VERSION    1
#define ZIGBEE_CHANNEL_MASK      (1UL << 15)

#define I2C_SDA_GPIO             GPIO_NUM_12
#define I2C_SCL_GPIO             GPIO_NUM_4
#define INA226_ALERT_GPIO        GPIO_NUM_11
#define AC_PRESENT_GPIO          GPIO_NUM_10
#define RELAY_CONTROL_GPIO       GPIO_NUM_22
#define TEST_BUTTON_GPIO         GPIO_NUM_5
#define NTC_ADC_CHANNEL          ADC_CHANNEL_0 /* GPIO1 */
#define INA226_ADDRESS           0x40

#define INA226_REG_CONFIG        0x00
#define INA226_REG_BUS_VOLTAGE   0x02
#define INA226_REG_POWER         0x03
#define INA226_REG_CURRENT       0x04
#define INA226_REG_CALIBRATION   0x05
#define INA226_CONFIG_CONTINUOUS 0x4527
#define INA226_CALIBRATION       5120 /* R010: 10 mOhm shunt, current LSB = 100 uA */

#define REPORT_NORMAL_MS         60000
#define REPORT_ATTENTION_MS      30000
#define REPORT_ALERT_MS          10000
#define ZIGBEE_KEEP_ALIVE_MS     3000
#define LOCAL_TEST_DURATION_S    30
#define MAX_TEST_DURATION_S      300
#define TEST_SAMPLE_MS           5000
#define TEST_MIN_VOLTAGE_MV      5250
#define TEST_MIN_CURRENT_MA      50

#define FLAG_AC_PRESENT          (1U << 0)
#define FLAG_ALERT               (1U << 1)
#define FLAG_UVLO                (1U << 2)
#define FLAG_SENSOR_VALID        (1U << 3)
#define FLAG_SELF_TEST           (1U << 4)

typedef struct __attribute__((packed)) {
    uint8_t payload_version;
    uint8_t flags;
    uint16_t sequence;
    uint32_t device_id;
    uint16_t vpack_mv;
    int16_t ipack_ma;
    int32_t power_mw;
    int16_t temp_centi_c;
    uint8_t soh_pct;
    uint8_t reserved;
    uint32_t uptime_s;
} emergency_telemetry_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t group_index;
    uint16_t test_id;
    uint32_t start_delay_s;
    uint16_t duration_s;
    uint16_t reserved;
} battery_test_schedule_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t result;
    uint16_t test_id;
    uint32_t device_id;
    uint16_t voltage_before_mv;
    uint16_t voltage_min_mv;
    int16_t current_avg_ma;
    int16_t current_peak_ma;
    int32_t power_avg_mw;
    int16_t temperature_max_centi_c;
    uint16_t duration_s;
    uint8_t sample_count;
    uint8_t flags;
} battery_test_report_t;

enum {
    TEST_RESULT_PASS,
    TEST_RESULT_LOW_VOLTAGE,
    TEST_RESULT_LOW_CURRENT,
    TEST_RESULT_HIGH_TEMPERATURE,
    TEST_RESULT_AC_NOT_PRESENT,
    TEST_RESULT_SENSOR_INVALID,
    TEST_RESULT_ABORTED,
};

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

static const char *TAG = "emergency_node";
static i2c_master_dev_handle_t s_ina226;
static bool s_ina226_ready;
static uint8_t s_ina226_address = INA226_ADDRESS;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_adc_calibrated;
static bool s_zigbee_joined;
static volatile bool s_test_active;
static volatile int64_t s_test_end_us;
static SemaphoreHandle_t s_test_mutex;
static battery_test_stats_t s_test_stats;
static battery_test_schedule_t s_schedule;
static bool s_schedule_valid;
static int64_t s_schedule_due_us;
static battery_test_report_t s_pending_report;
static bool s_pending_report_valid;
static int64_t s_report_retry_due_us;
static uint16_t s_last_voltage_mv;
static TaskHandle_t s_telemetry_task;
static uint16_t s_sequence;
static uint32_t s_device_id;

static void send_test_report(const battery_test_report_t *report);

static esp_err_t ina226_write(uint8_t reg, uint16_t value)
{
    uint8_t data[] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_transmit(s_ina226, data, sizeof(data), 100);
}

static esp_err_t ina226_read(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_ina226, &reg, 1, data, sizeof(data), 100), TAG,
                        "INA226 read failed");
    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

static esp_err_t sensors_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "I2C init failed");

    bool any_device_found = false;
    bool ina226_found = false;
    ESP_LOGI(TAG, "Scanning I2C bus on SDA=GPIO%d SCL=GPIO%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        if (i2c_master_probe(bus, address, 20) == ESP_OK) {
            any_device_found = true;
            ESP_LOGI(TAG, "I2C device found at 0x%02x", address);
            if (!ina226_found && address >= 0x40 && address <= 0x4f) {
                s_ina226_address = address;
                ina226_found = true;
            }
        }
    }
    if (!any_device_found) {
        ESP_LOGW(TAG, "No I2C devices found; line levels SDA=%d SCL=%d", gpio_get_level(I2C_SDA_GPIO),
                 gpio_get_level(I2C_SCL_GPIO));
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_ina226_address,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_ina226), TAG, "INA226 add failed");
    esp_err_t ina_status = ina226_write(INA226_REG_CALIBRATION, INA226_CALIBRATION);
    if (ina_status == ESP_OK) ina_status = ina226_write(INA226_REG_CONFIG, INA226_CONFIG_CONTINUOUS);
    if (ina_status == ESP_OK) {
        s_ina226_ready = true;
        ESP_LOGI(TAG, "INA226 detected at address 0x%02x", s_ina226_address);
    } else {
        ESP_LOGW(TAG, "INA226 not detected in range 0x40-0x4f; continuing with invalid sensor telemetry");
    }

    adc_oneshot_unit_init_cfg_t adc_cfg = {.unit_id = ADC_UNIT_1};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_adc), TAG, "ADC init failed");
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, NTC_ADC_CHANNEL, &chan_cfg), TAG, "ADC channel failed");

    adc_cali_curve_fitting_config_t cal_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = NTC_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_adc_calibrated = adc_cali_create_scheme_curve_fitting(&cal_cfg, &s_adc_cali) == ESP_OK;

    gpio_config_t alert_cfg = {
        .pin_bit_mask = (1ULL << INA226_ALERT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&alert_cfg), TAG, "ALERT GPIO config failed");
    gpio_config_t ac_cfg = {
        .pin_bit_mask = (1ULL << AC_PRESENT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&ac_cfg), TAG, "AC GPIO config failed");

    gpio_config_t relay_cfg = {
        .pin_bit_mask = (1ULL << RELAY_CONTROL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&relay_cfg), TAG, "Relay GPIO config failed");
    gpio_set_level(RELAY_CONTROL_GPIO, 0);

    gpio_config_t button_cfg = {
        .pin_bit_mask = (1ULL << TEST_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&button_cfg);
}

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
            size == sizeof(s_schedule) && s_schedule.version == TEST_PROTOCOL_VERSION) {
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
            size == sizeof(s_pending_report) && s_pending_report.version == TEST_PROTOCOL_VERSION) {
            s_pending_report_valid = true;
            ESP_LOGW(TAG, "Restored pending report for test %u", s_pending_report.test_id);
        }
        nvs_close(nvs);
    }
}

static battery_test_report_t make_test_report(const battery_test_stats_t *stats, uint8_t forced_result)
{
    battery_test_report_t report = {
        .version = TEST_PROTOCOL_VERSION,
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
    if (forced_result == TEST_RESULT_PASS) {
        if (stats->sample_count == 0 || report.voltage_min_mv == 0) report.result = TEST_RESULT_SENSOR_INVALID;
        else if (report.voltage_min_mv < TEST_MIN_VOLTAGE_MV) report.result = TEST_RESULT_LOW_VOLTAGE;
        else if (report.current_peak_ma < TEST_MIN_CURRENT_MA) report.result = TEST_RESULT_LOW_CURRENT;
        else if (report.temperature_max_centi_c != INT16_MIN && report.temperature_max_centi_c >= 4000) {
            report.result = TEST_RESULT_HIGH_TEMPERATURE;
        }
    }
    return report;
}

static void stop_battery_test(uint8_t result)
{
    gpio_set_level(RELAY_CONTROL_GPIO, 0);

    xSemaphoreTake(s_test_mutex, portMAX_DELAY);
    bool was_active = s_test_active;
    s_test_active = false;
    s_test_end_us = 0;
    battery_test_stats_t stats = s_test_stats;
    xSemaphoreGive(s_test_mutex);

    if (!was_active) return;
    battery_test_report_t report = make_test_report(&stats, result);
    if (s_telemetry_task) xTaskNotifyGive(s_telemetry_task);
    send_test_report(&report);
    ESP_LOGI(TAG, "Battery test %u stopped; result=%u, samples=%u, relay returned to NC",
             report.test_id, report.result, report.sample_count);
}

static void send_rejected_test_report(uint16_t test_id, uint16_t duration_s, uint8_t result)
{
    battery_test_stats_t stats = {
        .test_id = test_id,
        .duration_s = duration_s,
        .voltage_before_mv = s_last_voltage_mv,
        .voltage_min_mv = UINT16_MAX,
        .temperature_max_centi_c = INT16_MIN,
    };
    battery_test_report_t report = make_test_report(&stats, result);
    send_test_report(&report);
}

static esp_err_t start_battery_test(uint16_t duration_s, uint16_t test_id)
{
    if (duration_s == 0) {
        stop_battery_test(TEST_RESULT_ABORTED);
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(duration_s <= MAX_TEST_DURATION_S, ESP_ERR_INVALID_ARG, TAG, "Test exceeds %u seconds",
                        MAX_TEST_DURATION_S);
    if (!gpio_get_level(AC_PRESENT_GPIO)) {
        send_rejected_test_report(test_id, duration_s, TEST_RESULT_AC_NOT_PRESENT);
        ESP_LOGW(TAG, "Cannot start test %u: AC is already absent", test_id);
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_test_mutex, portMAX_DELAY);
    if (s_test_active) {
        xSemaphoreGive(s_test_mutex);
        send_rejected_test_report(test_id, duration_s, TEST_RESULT_ABORTED);
        return ESP_ERR_INVALID_STATE;
    }
    s_test_stats = (battery_test_stats_t) {
        .test_id = test_id,
        .duration_s = duration_s,
        .voltage_before_mv = s_last_voltage_mv,
        .voltage_min_mv = UINT16_MAX,
        .temperature_max_centi_c = INT16_MIN,
    };
    s_test_end_us = esp_timer_get_time() + (int64_t)duration_s * 1000000LL;
    s_test_active = true;
    xSemaphoreGive(s_test_mutex);

    gpio_set_level(RELAY_CONTROL_GPIO, 1);
    if (s_telemetry_task) xTaskNotifyGive(s_telemetry_task);
    ESP_LOGW(TAG, "Battery test %u started for %u seconds; AC relay energized", test_id, duration_s);
    return ESP_OK;
}

static void relay_task(void *arg)
{
    bool previous_button = true;
    while (true) {
        bool button = gpio_get_level(TEST_BUTTON_GPIO) != 0;
        if (previous_button && !button) {
            if (s_test_active) stop_battery_test(TEST_RESULT_ABORTED);
            else (void)start_battery_test(LOCAL_TEST_DURATION_S, 0);
        }
        previous_button = button;

        int64_t now = esp_timer_get_time();
        if (s_test_active && now >= s_test_end_us) stop_battery_test(TEST_RESULT_PASS);
        if (s_schedule_valid && now >= s_schedule_due_us) {
            xSemaphoreTake(s_test_mutex, portMAX_DELAY);
            battery_test_schedule_t schedule = s_schedule;
            s_schedule_valid = false;
            xSemaphoreGive(s_test_mutex);
            clear_persisted_schedule();
            (void)start_battery_test(schedule.duration_s, schedule.test_id);
        }
        if (s_pending_report_valid && s_zigbee_joined && now >= s_report_retry_due_us) {
            battery_test_report_t report;
            xSemaphoreTake(s_test_mutex, portMAX_DELAY);
            report = s_pending_report;
            xSemaphoreGive(s_test_mutex);
            send_test_report(&report);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static esp_err_t read_ntc_centi_c(int16_t *temperature)
{
    int raw;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, NTC_ADC_CHANNEL, &raw), TAG, "NTC ADC read failed");
    int mv = 0;
    if (!s_adc_calibrated || adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) != ESP_OK || mv <= 0 || mv >= 3300) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Divider: 3V3 -> 10K fixed -> ADC -> NTC 10K -> GND. */
    float resistance = 10000.0f * (float)mv / (3300.0f - (float)mv);
    float kelvin = 1.0f / ((1.0f / 298.15f) + logf(resistance / 10000.0f) / 3950.0f);
    *temperature = (int16_t)lroundf((kelvin - 273.15f) * 100.0f);
    return ESP_OK;
}

static uint8_t estimate_soh(uint16_t voltage_mv, bool ac_present)
{
    if (ac_present) {
        if (voltage_mv >= 6300) return 100;
        if (voltage_mv <= 5500) return 0;
        return (uint8_t)((voltage_mv - 5500) * 100U / 800U);
    }
    if (voltage_mv >= 6300) return 100;
    if (voltage_mv <= 5250) return 0;
    return (uint8_t)((voltage_mv - 5250) * 100U / 1050U);
}

static esp_err_t sample_telemetry(emergency_telemetry_t *sample)
{
    bool ac_present = gpio_get_level(AC_PRESENT_GPIO) != 0;
    sample->payload_version = TELEMETRY_VERSION;
    sample->flags = ac_present ? FLAG_AC_PRESENT : 0;
    if (s_test_active) sample->flags |= FLAG_SELF_TEST;
    sample->sequence = s_sequence++;
    sample->device_id = s_device_id;

    uint16_t bus_raw = 0;
    uint16_t current_raw = 0;
    uint16_t power_raw = 0;
    bool ina_valid = s_ina226_ready &&
                     ina226_read(INA226_REG_BUS_VOLTAGE, &bus_raw) == ESP_OK &&
                     ina226_read(INA226_REG_CURRENT, &current_raw) == ESP_OK &&
                     ina226_read(INA226_REG_POWER, &power_raw) == ESP_OK;
    if (ina_valid) {
        sample->vpack_mv = (uint16_t)(((uint32_t)bus_raw * 5U) / 4U);
        sample->ipack_ma = (int16_t)current_raw / 10;
        sample->power_mw = (int32_t)power_raw * 5 / 2;
        s_last_voltage_mv = sample->vpack_mv;
    }

    int16_t temperature = INT16_MIN;
    bool ntc_valid = read_ntc_centi_c(&temperature) == ESP_OK;
    sample->temp_centi_c = temperature;
    if (ina_valid && ntc_valid) sample->flags |= FLAG_SENSOR_VALID;
    sample->soh_pct = ina_valid ? estimate_soh(sample->vpack_mv, ac_present) : 0;
    sample->reserved = 0;
    sample->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    if ((ina_valid && sample->vpack_mv < 5500) || (ntc_valid && sample->temp_centi_c >= 4000)) {
        sample->flags |= FLAG_ALERT;
    }
    if (ina_valid && sample->vpack_mv < 5250) sample->flags |= FLAG_UVLO;
    return ESP_OK;
}

static void update_test_stats(const emergency_telemetry_t *sample)
{
    xSemaphoreTake(s_test_mutex, portMAX_DELAY);
    if (s_test_active && sample->vpack_mv > 0) {
        if (sample->vpack_mv < s_test_stats.voltage_min_mv) s_test_stats.voltage_min_mv = sample->vpack_mv;
        int16_t current_abs = sample->ipack_ma < 0 ? -sample->ipack_ma : sample->ipack_ma;
        if (current_abs > s_test_stats.current_peak_ma) s_test_stats.current_peak_ma = current_abs;
        if (sample->temp_centi_c != INT16_MIN &&
            (s_test_stats.temperature_max_centi_c == INT16_MIN ||
             sample->temp_centi_c > s_test_stats.temperature_max_centi_c)) {
            s_test_stats.temperature_max_centi_c = sample->temp_centi_c;
        }
        s_test_stats.current_sum_ma += sample->ipack_ma;
        s_test_stats.power_sum_mw += sample->power_mw;
        s_test_stats.last_flags = sample->flags;
        if (s_test_stats.sample_count < UINT8_MAX) s_test_stats.sample_count++;
    }
    xSemaphoreGive(s_test_mutex);
}

static void send_telemetry(const emergency_telemetry_t *sample)
{
    esp_zb_zcl_custom_cluster_cmd_req_t req = {0};
    req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    req.zcl_basic_cmd.dst_endpoint = TELEMETRY_ENDPOINT;
    req.zcl_basic_cmd.src_endpoint = TELEMETRY_ENDPOINT;
    req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_id = TELEMETRY_CLUSTER_ID;
    req.custom_cmd_id = TELEMETRY_COMMAND_ID;
    req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    req.data.type = ESP_ZB_ZCL_ATTR_TYPE_SET;
    req.data.size = sizeof(*sample);
    req.data.value = (void *)sample;

    esp_zb_lock_acquire(portMAX_DELAY);
    uint8_t status = esp_zb_zcl_custom_cluster_cmd_req(&req);
    esp_zb_lock_release();
    if (status != 0) ESP_LOGW(TAG, "Zigbee send status: %u", status);
}

static void send_test_report(const battery_test_report_t *report)
{
    if (!s_zigbee_joined) {
        xSemaphoreTake(s_test_mutex, portMAX_DELAY);
        s_pending_report = *report;
        s_pending_report_valid = true;
        s_report_retry_due_us = esp_timer_get_time() + 30000000LL;
        xSemaphoreGive(s_test_mutex);
        persist_pending_report(report);
        ESP_LOGW(TAG, "Test %u report not sent: Zigbee network unavailable", report->test_id);
        return;
    }

    esp_zb_zcl_custom_cluster_cmd_req_t req = {0};
    req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    req.zcl_basic_cmd.dst_endpoint = TELEMETRY_ENDPOINT;
    req.zcl_basic_cmd.src_endpoint = TELEMETRY_ENDPOINT;
    req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_id = TELEMETRY_CLUSTER_ID;
    req.custom_cmd_id = TEST_REPORT_COMMAND_ID;
    req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    req.data.type = ESP_ZB_ZCL_ATTR_TYPE_SET;
    req.data.size = sizeof(*report);
    req.data.value = (void *)report;

    esp_zb_lock_acquire(portMAX_DELAY);
    uint8_t status = esp_zb_zcl_custom_cluster_cmd_req(&req);
    esp_zb_lock_release();
    xSemaphoreTake(s_test_mutex, portMAX_DELAY);
    if (status == 0) {
        s_pending_report_valid = false;
    } else {
        s_pending_report = *report;
        s_pending_report_valid = true;
        s_report_retry_due_us = esp_timer_get_time() + 30000000LL;
    }
    xSemaphoreGive(s_test_mutex);
    persist_pending_report(status == 0 ? NULL : report);
    if (status != 0) ESP_LOGW(TAG, "Test report send status: %u", status);
}

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id != ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID) return ESP_OK;
    const esp_zb_zcl_custom_cluster_command_message_t *msg = message;
    ESP_RETURN_ON_FALSE(msg && msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid Zigbee command");
    if (msg->info.cluster != TELEMETRY_CLUSTER_ID) return ESP_OK;

    if (msg->info.command.id == START_TEST_COMMAND_ID) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(uint16_t), ESP_ERR_INVALID_SIZE, TAG, "Invalid test command size");
        const uint8_t *data = msg->data.value;
        uint16_t duration_s = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
        return start_battery_test(duration_s, 0);
    }

    if (msg->info.command.id == SCHEDULE_TEST_COMMAND_ID) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(battery_test_schedule_t), ESP_ERR_INVALID_SIZE, TAG,
                            "Invalid schedule size");
        battery_test_schedule_t schedule;
        memcpy(&schedule, msg->data.value, sizeof(schedule));
        ESP_RETURN_ON_FALSE(schedule.version == TEST_PROTOCOL_VERSION &&
                            schedule.duration_s > 0 && schedule.duration_s <= MAX_TEST_DURATION_S,
                            ESP_ERR_INVALID_ARG, TAG, "Invalid test schedule");

        xSemaphoreTake(s_test_mutex, portMAX_DELAY);
        s_schedule = schedule;
        s_schedule_valid = true;
        s_schedule_due_us = esp_timer_get_time() + (int64_t)schedule.start_delay_s * 1000000LL;
        xSemaphoreGive(s_test_mutex);
        persist_schedule(&schedule);
        ESP_LOGI(TAG, "Test %u scheduled: group=%u starts_in=%" PRIu32 "s duration=%us",
                 schedule.test_id, schedule.group_index, schedule.start_delay_s, schedule.duration_s);
    }
    return ESP_OK;
}

static void telemetry_task(void *arg)
{
    while (true) {
        emergency_telemetry_t sample = {0};
        uint32_t delay_ms = REPORT_NORMAL_MS;
        if (sample_telemetry(&sample) == ESP_OK) {
            if (s_test_active) update_test_stats(&sample);
            ESP_LOGI(TAG, "V=%u mV I=%d mA T=%.2f C AC=%u SoH=%u%%", sample.vpack_mv, sample.ipack_ma,
                     sample.temp_centi_c / 100.0, !!(sample.flags & FLAG_AC_PRESENT), sample.soh_pct);
            if (s_zigbee_joined) send_telemetry(&sample);
            if (s_test_active) delay_ms = TEST_SAMPLE_MS;
            else if (sample.flags & FLAG_ALERT) delay_ms = REPORT_ALERT_MS;
            else if (sample.vpack_mv < 5850) delay_ms = REPORT_ATTENTION_MS;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}

static void commissioning_retry(uint8_t mode)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal)
{
    esp_zb_app_signal_type_t type = *signal->p_app_signal;
    switch (type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION));
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (signal->esp_err_status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
            } else {
                s_zigbee_joined = true;
                if (s_telemetry_task) xTaskNotifyGive(s_telemetry_task);
            }
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        s_zigbee_joined = signal->esp_err_status == ESP_OK;
        if (s_zigbee_joined && s_telemetry_task) xTaskNotifyGive(s_telemetry_task);
        if (!s_zigbee_joined) {
            esp_zb_scheduler_alarm((esp_zb_callback_t)commissioning_retry, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        ESP_LOGI(TAG, "Zigbee network %s", s_zigbee_joined ? "joined" : "not joined");
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal %s status %s", esp_zb_zdo_signal_to_string(type), esp_err_to_name(signal->esp_err_status));
        break;
    }
}

static void zigbee_task(void *arg)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = ZIGBEE_KEEP_ALIVE_MS,
        },
    };
    esp_zb_init(&cfg);

    uint8_t cluster_revision = 1;
    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(TELEMETRY_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom, 0x0000, ESP_ZB_ZCL_ATTR_TYPE_U8,
                    ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &cluster_revision));
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep = {
        .endpoint = TELEMETRY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 1,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoints, clusters, ep));
    ESP_ERROR_CHECK(esp_zb_device_register(endpoints));
    esp_zb_core_action_handler_register(zigbee_action_handler);
    esp_zb_set_primary_network_channel_set(ZIGBEE_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_IEEE802154));
    memcpy(&s_device_id, &mac[2], sizeof(s_device_id));
    esp_err_t nvs_status = nvs_flash_init();
    if (nvs_status == ESP_ERR_NVS_NO_FREE_PAGES || nvs_status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_status);
    s_test_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_test_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    load_persisted_schedule();
    load_pending_report();
    ESP_ERROR_CHECK(sensors_init());

    esp_zb_platform_config_t platform = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    xTaskCreate(zigbee_task, "zigbee", 6144, NULL, 5, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, &s_telemetry_task);
    xTaskCreate(relay_task, "relay_test", 3072, NULL, 6, NULL);
}
