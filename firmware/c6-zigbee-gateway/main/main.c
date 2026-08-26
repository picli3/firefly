#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
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
#define SCHEDULE_TEST_COMMAND_ID 0x03
#define TEST_REPORT_COMMAND_ID   0x04
#define TELEMETRY_VERSION        1
#define TEST_PROTOCOL_VERSION    1
#define ZIGBEE_CHANNEL           15
#define ZIGBEE_CHANNEL_MASK      (1UL << ZIGBEE_CHANNEL)
#define ZIGBEE_MAX_CHILDREN      50
#define NETWORK_OPEN_SECONDS     180
#define MAX_TRACKED_NODES        50
#define COMMAND_SPACING_MS       50

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
    uint32_t device_id;
    uint16_t short_addr;
    uint16_t last_test_id;
    uint32_t last_seen_s;
} node_record_t;

static const char *TAG = "c6_zigbee_gateway";
static SemaphoreHandle_t s_nodes_mutex;
static node_record_t s_nodes[MAX_TRACKED_NODES];
static size_t s_node_count;
static volatile bool s_zigbee_ready;
static uint16_t s_campaign_id;
static size_t s_campaign_expected;
static size_t s_campaign_reports;
static size_t s_campaign_passed;

static const char *test_result_name(uint8_t result)
{
    static const char *const names[] = {
        "PASS", "LOW_VOLTAGE", "LOW_CURRENT", "HIGH_TEMPERATURE",
        "AC_NOT_PRESENT", "SENSOR_INVALID", "ABORTED",
    };
    return result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}

static int find_node(uint32_t device_id)
{
    for (size_t i = 0; i < s_node_count; i++) {
        if (s_nodes[i].device_id == device_id) return (int)i;
    }
    return -1;
}

static void register_node(uint32_t device_id, uint16_t short_addr)
{
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    int index = find_node(device_id);
    if (index < 0 && s_node_count < MAX_TRACKED_NODES) {
        index = (int)s_node_count++;
        s_nodes[index].device_id = device_id;
        ESP_LOGI(TAG, "Registered node=%08" PRIx32 " short=0x%04x total=%u",
                 device_id, short_addr, (unsigned)s_node_count);
    }
    if (index >= 0) {
        s_nodes[index].short_addr = short_addr;
        s_nodes[index].last_seen_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    } else {
        ESP_LOGW(TAG, "Node table full; ignored node=%08" PRIx32, device_id);
    }
    xSemaphoreGive(s_nodes_mutex);
}

static void log_telemetry(uint16_t short_addr, const emergency_telemetry_t *sample)
{
    ESP_LOGI(TAG,
             "RX short=0x%04x node=%08" PRIx32 " seq=%u V=%u_mV I=%d_mA P=%" PRId32
             "_mW T=%.2f_C SoH=%u%% AC=%u alert=%u UVLO=%u sensor_valid=%u self_test=%u uptime=%" PRIu32 "_s",
             short_addr, sample->device_id, sample->sequence, sample->vpack_mv, sample->ipack_ma,
             sample->power_mw, sample->temp_centi_c / 100.0, sample->soh_pct,
             !!(sample->flags & FLAG_AC_PRESENT), !!(sample->flags & FLAG_ALERT),
             !!(sample->flags & FLAG_UVLO), !!(sample->flags & FLAG_SENSOR_VALID),
             !!(sample->flags & FLAG_SELF_TEST), sample->uptime_s);
}

static void process_test_report(uint16_t short_addr, const battery_test_report_t *report)
{
    ESP_LOGI(TAG,
             "TEST short=0x%04x node=%08" PRIx32 " id=%u result=%s V_before=%u_mV V_min=%u_mV "
             "I_avg=%d_mA I_peak=%d_mA P_avg=%" PRId32 "_mW T_max=%.2f_C duration=%u_s samples=%u",
             short_addr, report->device_id, report->test_id, test_result_name(report->result),
             report->voltage_before_mv, report->voltage_min_mv, report->current_avg_ma,
             report->current_peak_ma, report->power_avg_mw,
             report->temperature_max_centi_c / 100.0, report->duration_s, report->sample_count);

    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    int index = find_node(report->device_id);
    if (index >= 0 && report->test_id == s_campaign_id && s_nodes[index].last_test_id != report->test_id) {
        s_nodes[index].last_test_id = report->test_id;
        s_campaign_reports++;
        if (report->result == TEST_RESULT_PASS) s_campaign_passed++;
        ESP_LOGI(TAG, "Campaign %u progress: reports=%u/%u passed=%u failed=%u",
                 s_campaign_id, (unsigned)s_campaign_reports, (unsigned)s_campaign_expected,
                 (unsigned)s_campaign_passed, (unsigned)(s_campaign_reports - s_campaign_passed));
        if (s_campaign_reports == s_campaign_expected) {
            ESP_LOGI(TAG, "Campaign %u complete: passed=%u failed=%u",
                     s_campaign_id, (unsigned)s_campaign_passed,
                     (unsigned)(s_campaign_reports - s_campaign_passed));
        }
    }
    xSemaphoreGive(s_nodes_mutex);
}

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id != ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID) return ESP_OK;

    const esp_zb_zcl_custom_cluster_command_message_t *msg = message;
    ESP_RETURN_ON_FALSE(msg && msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid ZCL command");
    ESP_RETURN_ON_FALSE(msg->info.cluster == TELEMETRY_CLUSTER_ID,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Unexpected cluster 0x%04x", msg->info.cluster);
    uint16_t short_addr = msg->info.src_address.u.short_addr;

    if (msg->info.command.id == TELEMETRY_COMMAND_ID) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(emergency_telemetry_t),
                            ESP_ERR_INVALID_SIZE, TAG, "Unexpected telemetry size %u", msg->data.size);
        emergency_telemetry_t sample;
        memcpy(&sample, msg->data.value, sizeof(sample));
        ESP_RETURN_ON_FALSE(sample.payload_version == TELEMETRY_VERSION,
                            ESP_ERR_INVALID_VERSION, TAG, "Unsupported payload version %u", sample.payload_version);
        register_node(sample.device_id, short_addr);
        log_telemetry(short_addr, &sample);
        return ESP_OK;
    }

    if (msg->info.command.id == TEST_REPORT_COMMAND_ID) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(battery_test_report_t),
                            ESP_ERR_INVALID_SIZE, TAG, "Unexpected test report size %u", msg->data.size);
        battery_test_report_t report;
        memcpy(&report, msg->data.value, sizeof(report));
        ESP_RETURN_ON_FALSE(report.version == TEST_PROTOCOL_VERSION,
                            ESP_ERR_INVALID_VERSION, TAG, "Unsupported test report version %u", report.version);
        register_node(report.device_id, short_addr);
        process_test_report(short_addr, &report);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unexpected command 0x%02x", msg->info.command.id);
    return ESP_ERR_NOT_SUPPORTED;
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
        if (signal->esp_err_status != ESP_OK) {
            ESP_LOGE(TAG, "Zigbee initialization failed: %s", esp_err_to_name(signal->esp_err_status));
        } else if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Forming Zigbee network on channel %u", ZIGBEE_CHANNEL);
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION));
        } else {
            s_zigbee_ready = true;
            ESP_LOGI(TAG, "Restored Zigbee network: PAN=0x%04x channel=%u",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            ESP_ERROR_CHECK(esp_zb_bdb_open_network(NETWORK_OPEN_SECONDS));
            ESP_LOGI(TAG, "Pairing enabled for %u seconds", NETWORK_OPEN_SECONDS);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (signal->esp_err_status == ESP_OK) {
            s_zigbee_ready = true;
            ESP_LOGI(TAG, "Network formed: PAN=0x%04x channel=%u",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
            ESP_LOGI(TAG, "Pairing enabled for %u seconds", NETWORK_OPEN_SECONDS);
        } else {
            ESP_LOGW(TAG, "Network formation failed; retrying");
            esp_zb_scheduler_alarm((esp_zb_callback_t)commissioning_retry,
                                   ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        ESP_LOGI(TAG, "Network steering finished: %s", esp_err_to_name(signal->esp_err_status));
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        esp_zb_zdo_signal_device_annce_params_t *params =
            esp_zb_app_signal_get_params(signal->p_app_signal);
        ESP_LOGI(TAG, "Node joined: short_address=0x%04x", params->device_short_addr);
        break;
    }
    default:
        ESP_LOGI(TAG, "ZDO signal %s status %s", esp_zb_zdo_signal_to_string(type),
                 esp_err_to_name(signal->esp_err_status));
        break;
    }
}

static uint8_t send_test_schedule(uint16_t short_addr, const battery_test_schedule_t *schedule)
{
    esp_zb_zcl_custom_cluster_cmd_req_t req = {0};
    req.zcl_basic_cmd.dst_addr_u.addr_short = short_addr;
    req.zcl_basic_cmd.dst_endpoint = TELEMETRY_ENDPOINT;
    req.zcl_basic_cmd.src_endpoint = TELEMETRY_ENDPOINT;
    req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_id = TELEMETRY_CLUSTER_ID;
    req.custom_cmd_id = SCHEDULE_TEST_COMMAND_ID;
    req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV;
    req.data.type = ESP_ZB_ZCL_ATTR_TYPE_SET;
    req.data.size = sizeof(*schedule);
    req.data.value = (void *)schedule;

    esp_zb_lock_acquire(portMAX_DELAY);
    uint8_t status = esp_zb_zcl_custom_cluster_cmd_req(&req);
    esp_zb_lock_release();
    return status;
}

static void save_campaign_id(uint16_t test_id)
{
    nvs_handle_t nvs;
    if (nvs_open("gateway", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u16(nvs, "test_id", test_id);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void start_campaign(void)
{
    node_record_t nodes[MAX_TRACKED_NODES];
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    size_t count = s_node_count;
    memcpy(nodes, s_nodes, count * sizeof(nodes[0]));
    xSemaphoreGive(s_nodes_mutex);

    if (count == 0) {
        ESP_LOGW(TAG, "Automatic campaign skipped: no nodes have reported telemetry");
        return;
    }

    for (size_t i = 0; i + 1 < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (nodes[j].device_id < nodes[i].device_id) {
                node_record_t temp = nodes[i];
                nodes[i] = nodes[j];
                nodes[j] = temp;
            }
        }
    }

    s_campaign_id++;
    if (s_campaign_id == 0) s_campaign_id = 1;
    save_campaign_id(s_campaign_id);
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    s_campaign_expected = count;
    s_campaign_reports = 0;
    s_campaign_passed = 0;
    xSemaphoreGive(s_nodes_mutex);

    uint32_t group_span_s = CONFIG_AUTO_TEST_DURATION_SECONDS + CONFIG_AUTO_TEST_GUARD_SECONDS;
    size_t group_count = (count + CONFIG_AUTO_TEST_GROUP_SIZE - 1) / CONFIG_AUTO_TEST_GROUP_SIZE;
    ESP_LOGW(TAG, "Starting campaign %u: nodes=%u groups=%u group_size=%u duration=%us",
             s_campaign_id, (unsigned)count, (unsigned)group_count,
             CONFIG_AUTO_TEST_GROUP_SIZE, CONFIG_AUTO_TEST_DURATION_SECONDS);

    for (size_t i = 0; i < count; i++) {
        uint8_t group = i / CONFIG_AUTO_TEST_GROUP_SIZE;
        battery_test_schedule_t schedule = {
            .version = TEST_PROTOCOL_VERSION,
            .group_index = group,
            .test_id = s_campaign_id,
            .start_delay_s = CONFIG_AUTO_TEST_LEAD_SECONDS + group * group_span_s,
            .duration_s = CONFIG_AUTO_TEST_DURATION_SECONDS,
        };
        uint8_t status = send_test_schedule(nodes[i].short_addr, &schedule);
        ESP_LOGI(TAG, "Schedule node=%08" PRIx32 " short=0x%04x group=%u delay=%" PRIu32 "s status=%u",
                 nodes[i].device_id, nodes[i].short_addr, group, schedule.start_delay_s, status);
        vTaskDelay(pdMS_TO_TICKS(COMMAND_SPACING_MS));
    }
}

static void campaign_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((uint64_t)CONFIG_AUTO_TEST_FIRST_DELAY_MINUTES * 60ULL * 1000ULL));
    while (true) {
        if (s_zigbee_ready) start_campaign();
        else ESP_LOGW(TAG, "Automatic campaign postponed: Zigbee network is not ready");
        vTaskDelay(pdMS_TO_TICKS((uint64_t)CONFIG_AUTO_TEST_INTERVAL_MINUTES * 60ULL * 1000ULL));
    }
}

static void zigbee_task(void *arg)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {.max_children = ZIGBEE_MAX_CHILDREN},
    };
    esp_zb_init(&cfg);

    uint8_t cluster_revision = 1;
    esp_zb_attribute_list_t *custom_cluster = esp_zb_zcl_attr_list_create(TELEMETRY_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom_cluster, 0x0000,
                    ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &cluster_revision));
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, custom_cluster,
                    ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint = {
        .endpoint = TELEMETRY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 1,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoints, clusters, endpoint));
    ESP_ERROR_CHECK(esp_zb_device_register(endpoints));
    esp_zb_core_action_handler_register(zigbee_action_handler);
    esp_zb_set_primary_network_channel_set(ZIGBEE_CHANNEL_MASK);

    ESP_LOGI(TAG, "Starting Zigbee coordinator on channel %u", ZIGBEE_CHANNEL);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(status);

    nvs_handle_t nvs;
    if (nvs_open("gateway", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u16(nvs, "test_id", &s_campaign_id);
        nvs_close(nvs);
    }
    s_nodes_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_nodes_mutex ? ESP_OK : ESP_ERR_NO_MEM);

    esp_zb_platform_config_t platform = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    xTaskCreate(zigbee_task, "zigbee", 6144, NULL, 5, NULL);
    xTaskCreate(campaign_task, "campaign", 4096, NULL, 4, NULL);
}
