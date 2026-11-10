#include "test_campaign.h"

#include <inttypes.h>

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_registry.h"
#include "nvs.h"

static const char *TAG = "test_campaign";
static SemaphoreHandle_t s_mutex;
static test_schedule_sender_t s_send_schedule;
static zigbee_ready_provider_t s_zigbee_ready;
static uint16_t s_campaign_id;
static size_t s_expected;
static size_t s_reports;
static size_t s_passed;

static void save_campaign_id(void)
{
    nvs_handle_t nvs;
    if (nvs_open("gateway", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u16(nvs, "test_id", s_campaign_id);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static bool start_campaign(void)
{
    node_record_t nodes[APP_MAX_TRACKED_NODES];
    size_t count = node_registry_snapshot_sorted(nodes, APP_MAX_TRACKED_NODES);
    if (count == 0) {
        ESP_LOGW(TAG, "Automatic campaign skipped: no nodes have reported telemetry");
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_campaign_id++;
    if (s_campaign_id == 0) s_campaign_id = 1;
    s_expected = count;
    s_reports = 0;
    s_passed = 0;
    uint16_t campaign_id = s_campaign_id;
    xSemaphoreGive(s_mutex);
    save_campaign_id();

    uint32_t group_span_s = CONFIG_AUTO_TEST_DURATION_SECONDS + CONFIG_AUTO_TEST_GUARD_SECONDS;
    size_t group_count = (count + CONFIG_AUTO_TEST_GROUP_SIZE - 1) / CONFIG_AUTO_TEST_GROUP_SIZE;
    ESP_LOGW(TAG, "Starting campaign %u: nodes=%u groups=%u group_size=%u duration=%us",
             campaign_id, (unsigned)count, (unsigned)group_count,
             CONFIG_AUTO_TEST_GROUP_SIZE, CONFIG_AUTO_TEST_DURATION_SECONDS);

    for (size_t i = 0; i < count; i++) {
        uint8_t group = i / CONFIG_AUTO_TEST_GROUP_SIZE;
        battery_test_schedule_t schedule = {
            .version = EMERGENCY_TEST_PROTOCOL_VERSION,
            .group_index = group,
            .test_id = campaign_id,
            .start_delay_s = CONFIG_AUTO_TEST_LEAD_SECONDS + group * group_span_s,
            .duration_s = CONFIG_AUTO_TEST_DURATION_SECONDS,
        };
        uint8_t sequence = s_send_schedule(nodes[i].short_addr, &schedule);
        ESP_LOGI(TAG, "Schedule node=%08" PRIx32 " short=0x%04x group=%u delay=%" PRIu32 "s TSN=%u",
                 nodes[i].device_id, nodes[i].short_addr, group, schedule.start_delay_s, sequence);
        vTaskDelay(pdMS_TO_TICKS(APP_COMMAND_SPACING_MS));
    }
    return true;
}

static void campaign_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((uint64_t)CONFIG_AUTO_TEST_FIRST_DELAY_MINUTES * 60ULL * 1000ULL));
    while (true) {
        TickType_t campaign_start = xTaskGetTickCount();
        bool started = false;
        if (s_zigbee_ready()) started = start_campaign();
        else ESP_LOGW(TAG, "Automatic campaign postponed: Zigbee network is not ready");
        if (started) {
            /* Start-to-start interval: sending schedules must not extend the
             * requested 5-minute discharge / 55-minute recovery cycle. */
            xTaskDelayUntil(&campaign_start,
                           pdMS_TO_TICKS((uint64_t)CONFIG_AUTO_TEST_INTERVAL_MINUTES * 60ULL * 1000ULL));
        } else {
            vTaskDelay(pdMS_TO_TICKS(APP_CAMPAIGN_RETRY_MS));
        }
    }
}

esp_err_t test_campaign_init(test_schedule_sender_t sender, zigbee_ready_provider_t ready_provider)
{
    if (!sender || !ready_provider) return ESP_ERR_INVALID_ARG;
    s_send_schedule = sender;
    s_zigbee_ready = ready_provider;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    nvs_handle_t nvs;
    if (nvs_open("gateway", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u16(nvs, "test_id", &s_campaign_id);
        nvs_close(nvs);
    }
#ifdef CONFIG_AUTO_TEST_ENABLED
    const bool enabled = true;
#else
    const bool enabled = false;
#endif
    if (!enabled) {
        ESP_LOGI(TAG, "Automatic battery tests disabled; telemetry recording continues");
        return ESP_OK;
    }
    return xTaskCreate(campaign_task, "campaign", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void test_campaign_process_report(const battery_test_report_t *report)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (report->test_id == s_campaign_id &&
        node_registry_mark_test_report(report->device_id, report->test_id)) {
        s_reports++;
        if (report->result == BATTERY_TEST_PASS) s_passed++;
        ESP_LOGI(TAG, "Campaign %u progress: reports=%u/%u passed=%u failed=%u",
                 s_campaign_id, (unsigned)s_reports, (unsigned)s_expected,
                 (unsigned)s_passed, (unsigned)(s_reports - s_passed));
        if (s_reports == s_expected) {
            ESP_LOGI(TAG, "Campaign %u complete: passed=%u failed=%u", s_campaign_id,
                     (unsigned)s_passed, (unsigned)(s_reports - s_passed));
        }
    }
    xSemaphoreGive(s_mutex);
}
