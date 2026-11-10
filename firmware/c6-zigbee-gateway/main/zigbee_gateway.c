#include "zigbee_gateway.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "node_registry.h"
#include "nvs.h"
#include "nwk/esp_zigbee_nwk.h"
#include "telemetry_log.h"
#include "telemetry_csv.h"
#include "test_campaign.h"
#include "zdo/esp_zigbee_zdo_command.h"

static const char *TAG = "zigbee_gateway";
static volatile bool s_ready;

static const char *nwk_status_name(uint8_t status)
{
    switch (status) {
    case ESP_ZB_NWK_COMMAND_STATUS_NO_ROUTE_AVAILABLE: return "NO_ROUTE_AVAILABLE";
    case ESP_ZB_NWK_COMMAND_STATUS_TREE_LINK_FAILURE: return "TREE_LINK_FAILURE";
    case ESP_ZB_NWK_COMMAND_STATUS_NONE_TREE_LINK_FAILURE: return "NON_TREE_LINK_FAILURE";
    case ESP_ZB_NWK_COMMAND_STATUS_LOW_BATTERY_LEVEL: return "LOW_BATTERY_LEVEL";
    case ESP_ZB_NWK_COMMAND_STATUS_NO_ROUTING_CAPACITY: return "NO_ROUTING_CAPACITY";
    case ESP_ZB_NWK_COMMAND_STATUS_NO_INDIRECT_CAPACITY: return "NO_INDIRECT_CAPACITY";
    case ESP_ZB_NWK_COMMAND_STATUS_INDIRECT_TRANSACTION_EXPIRY: return "INDIRECT_TRANSACTION_EXPIRY";
    case ESP_ZB_NWK_COMMAND_STATUS_TARGET_DEVICE_UNAVAILABLE: return "TARGET_DEVICE_UNAVAILABLE";
    case ESP_ZB_NWK_COMMAND_STATUS_TARGET_ADDRESS_UNALLOCATED: return "TARGET_ADDRESS_UNALLOCATED";
    case ESP_ZB_NWK_COMMAND_STATUS_PARENT_LINK_FAILURE: return "PARENT_LINK_FAILURE";
    case ESP_ZB_NWK_COMMAND_STATUS_VALIDATE_ROUTE: return "VALIDATE_ROUTE";
    case ESP_ZB_NWK_COMMAND_STATUS_SOURCE_ROUTE_FAILURE: return "SOURCE_ROUTE_FAILURE";
    case ESP_ZB_NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE: return "MANY_TO_ONE_ROUTE_FAILURE";
    case ESP_ZB_NWK_COMMAND_STATUS_ADDRESS_CONFLICT: return "ADDRESS_CONFLICT";
    case ESP_ZB_NWK_COMMAND_STATUS_VERIFY_ADDRESS: return "VERIFY_ADDRESS";
    case ESP_ZB_NWK_COMMAND_STATUS_PAN_IDENTIFIER_UPDATE: return "PAN_IDENTIFIER_UPDATE";
    case ESP_ZB_NWK_COMMAND_STATUS_NETWORK_ADDRESS_UPDATE: return "NETWORK_ADDRESS_UPDATE";
    case ESP_ZB_NWK_COMMAND_STATUS_BAD_FRAME_COUNTER: return "BAD_FRAME_COUNTER";
    case ESP_ZB_NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER: return "BAD_KEY_SEQUENCE_NUMBER";
    case ESP_ZB_NWK_COMMAND_STATUS_UNKNOWN_COMMAND: return "UNKNOWN_COMMAND";
    default: return "UNRECOGNIZED";
    }
}

#define REPEAT_REST_US INT64_C(300000000)
#define REPEAT_FRESH_US INT64_C(75000000)

enum repeat_phase { REPEAT_WAIT, REPEAT_STARTING, REPEAT_ACTIVE };
// Accessed only by Zigbee callbacks or while holding the Zigbee stack lock.
static struct {
    uint32_t target;
    enum repeat_phase phase;
    int64_t due, seen;
    uint8_t flags;
    bool wait_for_ac;
} s_repeat;

static esp_err_t save_repeat_target(uint32_t target)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("gateway", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "repeat_node", target);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void repeat_observe(const emergency_telemetry_t *sample)
{
    if (!s_repeat.target || sample->device_id != s_repeat.target) return;
    const int64_t now = esp_timer_get_time();
    const bool ac = sample->flags & EMERGENCY_FLAG_AC_PRESENT;
    const bool active = sample->flags & EMERGENCY_FLAG_SELF_TEST;
    const bool valid = sample->flags & EMERGENCY_FLAG_SENSOR_VALID;
    s_repeat.seen = now;
    s_repeat.flags = sample->flags;
    if (s_repeat.phase == REPEAT_STARTING && valid && active && !ac) {
        s_repeat.phase = REPEAT_ACTIVE;
        s_repeat.due = now + INT64_C(60000000);
        ESP_LOGI(TAG, "REPEAT active node=%08" PRIx32, s_repeat.target);
    } else if (valid && ac && !active &&
               (s_repeat.phase == REPEAT_ACTIVE || (s_repeat.phase == REPEAT_WAIT && s_repeat.wait_for_ac))) {
        s_repeat.phase = REPEAT_WAIT;
        s_repeat.wait_for_ac = false;
        s_repeat.due = now + REPEAT_REST_US;
        ESP_LOGI(TAG, "REPEAT rest node=%08" PRIx32 " seconds=300", s_repeat.target);
    } else if (s_repeat.phase == REPEAT_WAIT && (!ac || active)) {
        s_repeat.wait_for_ac = true;
    }
}

static esp_err_t action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id != ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID) return ESP_OK;

    const esp_zb_zcl_custom_cluster_command_message_t *msg = message;
    ESP_RETURN_ON_FALSE(msg && msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid ZCL command");
    ESP_RETURN_ON_FALSE(msg->info.cluster == EMERGENCY_CLUSTER_ID,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Unexpected cluster 0x%04x", msg->info.cluster);
    uint16_t short_addr = msg->info.src_address.u.short_addr;

    if (msg->info.command.id == EMERGENCY_CMD_TELEMETRY) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(emergency_telemetry_t),
                            ESP_ERR_INVALID_SIZE, TAG, "Unexpected telemetry size %u", msg->data.size);
        emergency_telemetry_t sample;
        memcpy(&sample, msg->data.value, sizeof(sample));
        ESP_RETURN_ON_FALSE(sample.payload_version == EMERGENCY_TELEMETRY_VERSION,
                            ESP_ERR_INVALID_VERSION, TAG, "Unsupported payload version %u", sample.payload_version);
        node_registry_register(sample.device_id, short_addr);
        repeat_observe(&sample);
        telemetry_csv_enqueue(short_addr, &sample);
        telemetry_log_sample(short_addr, &sample);
        return ESP_OK;
    }

    if (msg->info.command.id == EMERGENCY_CMD_TEST_REPORT) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(battery_test_report_t),
                            ESP_ERR_INVALID_SIZE, TAG, "Unexpected test report size %u", msg->data.size);
        battery_test_report_t report;
        memcpy(&report, msg->data.value, sizeof(report));
        ESP_RETURN_ON_FALSE(report.version == EMERGENCY_TEST_PROTOCOL_VERSION,
                            ESP_ERR_INVALID_VERSION, TAG, "Unsupported test report version %u", report.version);
        node_registry_register(report.device_id, short_addr);
        telemetry_log_test_report(short_addr, &report);
        test_campaign_process_report(&report);
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
            ESP_LOGI(TAG, "Forming Zigbee network on channel %u", APP_ZIGBEE_CHANNEL);
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION));
        } else {
            s_ready = true;
            ESP_LOGI(TAG, "Restored Zigbee network: PAN=0x%04x channel=%u",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            ESP_ERROR_CHECK(esp_zb_bdb_open_network(APP_NETWORK_OPEN_SECONDS));
            ESP_LOGI(TAG, "Pairing enabled for %u seconds", APP_NETWORK_OPEN_SECONDS);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (signal->esp_err_status == ESP_OK) {
            s_ready = true;
            ESP_LOGI(TAG, "Network formed: PAN=0x%04x channel=%u",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
            ESP_LOGI(TAG, "Pairing enabled for %u seconds", APP_NETWORK_OPEN_SECONDS);
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
    case ESP_ZB_NLME_STATUS_INDICATION: {
        const esp_zb_zdo_signal_nwk_status_indication_params_t *params =
            esp_zb_app_signal_get_params(signal->p_app_signal);
        if (!params) {
            ESP_LOGW(TAG, "NLME event=%s payload=missing", esp_err_to_name(signal->esp_err_status));
        } else {
            ESP_LOGW(TAG, "NLME event=%s nwk_status=0x%02x (%s) addr=0x%04x",
                     esp_err_to_name(signal->esp_err_status), params->status,
                     nwk_status_name(params->status), params->network_addr);
            if (params->status == ESP_ZB_NWK_COMMAND_STATUS_UNKNOWN_COMMAND) {
                ESP_LOGW(TAG, "NLME addr=0x%04x unknown_command=0x%02x",
                         params->network_addr, params->unknown_command_id);
            }
        }
        break;
    }
    default:
        ESP_LOGI(TAG, "ZDO signal %s status %s", esp_zb_zdo_signal_to_string(type),
                 esp_err_to_name(signal->esp_err_status));
        break;
    }
}

static uint8_t send_test_command_locked(uint16_t short_addr, uint8_t command, const void *data, uint16_t size)
{
    esp_zb_zcl_custom_cluster_cmd_req_t req = {0};
    req.zcl_basic_cmd.dst_addr_u.addr_short = short_addr;
    req.zcl_basic_cmd.dst_endpoint = EMERGENCY_ENDPOINT;
    req.zcl_basic_cmd.src_endpoint = EMERGENCY_ENDPOINT;
    req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_id = EMERGENCY_CLUSTER_ID;
    req.custom_cmd_id = command;
    req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV;
    req.data.type = ESP_ZB_ZCL_ATTR_TYPE_SET;
    req.data.size = size;
    req.data.value = (void *)data;
    return esp_zb_zcl_custom_cluster_cmd_req(&req);
}

uint8_t zigbee_gateway_send_schedule(uint16_t short_addr, const battery_test_schedule_t *schedule)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    uint8_t sequence = send_test_command_locked(short_addr, EMERGENCY_CMD_SCHEDULE_TEST, schedule, sizeof(*schedule));
    esp_zb_lock_release();
    return sequence;
}

esp_err_t zigbee_gateway_start_manual_test(uint32_t device_id)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(250))) return ESP_ERR_TIMEOUT;
    if (device_id == s_repeat.target) {
        esp_zb_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t addr;
    if (!node_registry_find_recent(device_id, &addr) || addr == 0 || addr >= 0xfff8) {
        esp_zb_lock_release();
        return ESP_ERR_NOT_FOUND;
    }
    const uint8_t duration_le[] = {30, 0};
    uint8_t seq = send_test_command_locked(addr, EMERGENCY_CMD_START_TEST, duration_le, sizeof(duration_le));
    esp_zb_lock_release();
    ESP_LOGI(TAG, "MANUAL_TEST sent node=%08" PRIx32 " short=0x%04x duration=30s TSN=%u", device_id, addr, seq);
    return ESP_OK;
}

static void repeat_tick(uint8_t unused)
{
    (void)unused;
    int64_t now = esp_timer_get_time();
    if (s_repeat.target && now >= s_repeat.due) {
        if (s_repeat.phase != REPEAT_WAIT) {
            ESP_LOGW(TAG, "REPEAT stopped node=%08" PRIx32 " reason=missing_confirmation", s_repeat.target);
            s_repeat.target = 0;
            ESP_ERROR_CHECK_WITHOUT_ABORT(save_repeat_target(0));
        } else if (s_ready && s_repeat.seen && now - s_repeat.seen <= REPEAT_FRESH_US &&
                   (s_repeat.flags & (EMERGENCY_FLAG_AC_PRESENT | EMERGENCY_FLAG_SENSOR_VALID | EMERGENCY_FLAG_SELF_TEST)) ==
                   (EMERGENCY_FLAG_AC_PRESENT | EMERGENCY_FLAG_SENSOR_VALID)) {
            uint16_t addr;
            if (node_registry_find_recent(s_repeat.target, &addr) && addr > 0 && addr < 0xfff8) {
                const uint8_t duration_le[] = {30, 0};
                s_repeat.phase = REPEAT_STARTING;
                s_repeat.due = now + INT64_C(25000000);
                send_test_command_locked(addr, EMERGENCY_CMD_START_TEST, duration_le, sizeof(duration_le));
                ESP_LOGI(TAG, "REPEAT sent node=%08" PRIx32 " short=0x%04x duration=30s", s_repeat.target, addr);
            }
        }
    }
    esp_zb_scheduler_alarm(repeat_tick, 0, 1000);
}

esp_err_t zigbee_gateway_repeat_start(uint32_t device_id)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(250))) return ESP_ERR_TIMEOUT;
    uint16_t addr;
    esp_err_t err;
    if (s_repeat.target) {
        err = ESP_ERR_INVALID_STATE;
    } else if (!device_id || !node_registry_find_recent(device_id, &addr)) {
        err = ESP_ERR_NOT_FOUND;
    } else if ((err = save_repeat_target(device_id)) == ESP_OK) {
        memset(&s_repeat, 0, sizeof(s_repeat));
        s_repeat.target = device_id;
    }
    esp_zb_lock_release();
    return err;
}

esp_err_t zigbee_gateway_repeat_stop(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(250))) return ESP_ERR_TIMEOUT;
    s_repeat.target = 0;
    esp_err_t err = save_repeat_target(0);
    esp_zb_lock_release();
    return err;
}

esp_err_t zigbee_gateway_repeat_status(char *buffer, size_t capacity)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(250))) return ESP_ERR_TIMEOUT;
    int64_t remaining = s_repeat.due - esp_timer_get_time();
    const char *phase = !s_repeat.target ? "off" : s_repeat.phase == REPEAT_STARTING ? "starting" :
                        s_repeat.phase == REPEAT_ACTIVE ? "active" : remaining > 0 ? "rest" : "waiting_ac";
    snprintf(buffer, capacity, "@REPEAT STATUS node=%08" PRIx32 " phase=%s remaining_s=%" PRId64 "\n",
             s_repeat.target, phase, s_repeat.target && remaining > 0 ? (remaining + 999999) / 1000000 : 0);
    esp_zb_lock_release();
    return ESP_OK;
}

bool zigbee_gateway_is_ready(void)
{
    return s_ready;
}

esp_err_t zigbee_gateway_log_links(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(250))) return ESP_ERR_TIMEOUT;

    // Copy only the diagnostics we need. Do not hold the stack lock during UART output.
    struct {
        uint16_t addr;
        int8_t rssi;
        uint8_t lqi, type, relationship, age, outgoing_cost;
    } links[APP_MAX_TRACKED_NODES];
    size_t count = 0;
    esp_err_t status = ESP_OK;
    esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_neighbor_info_t neighbor;
    while (count < APP_MAX_TRACKED_NODES &&
           (status = esp_zb_nwk_get_next_neighbor(&it, &neighbor)) == ESP_OK) {
        links[count].addr = neighbor.short_addr;
        links[count].rssi = neighbor.rssi;
        links[count].lqi = neighbor.lqi;
        links[count].type = neighbor.device_type;
        links[count].relationship = neighbor.relationship;
        links[count].age = neighbor.age;
        links[count].outgoing_cost = neighbor.outgoing_cost;
        count++;
    }
    esp_zb_lock_release();

    for (size_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "LINK local observer=0x0000 neighbor=0x%04x rssi_dbm=%d lqi=%u type=%u relation=%u age=%u outgoing_cost=%u",
                 links[i].addr, links[i].rssi, links[i].lqi, links[i].type,
                 links[i].relationship, links[i].age, links[i].outgoing_cost);
    }
    ESP_LOGI(TAG, "LINK local_end count=%u capped=%u", (unsigned)count, count == APP_MAX_TRACKED_NODES);
    return status == ESP_ERR_NOT_FOUND ? ESP_OK : status;
}

static void remote_links_response(const esp_zb_zdo_mgmt_lqi_rsp_t *rsp, void *context)
{
    uint16_t observer = (uint16_t)(uintptr_t)context;
    if (!rsp || rsp->status != 0 || (rsp->neighbor_table_list_count && !rsp->neighbor_table_list)) {
        ESP_LOGW(TAG, "LINK remote_error observer=0x%04x status=0x%02x", observer, rsp ? rsp->status : 0xff);
        return;
    }
    for (uint8_t i = 0; i < rsp->neighbor_table_list_count; i++) {
        const esp_zb_zdo_neighbor_table_list_record_t *neighbor = &rsp->neighbor_table_list[i];
        ESP_LOGI(TAG, "LINK remote observer=0x%04x neighbor=0x%04x lqi=%u type=%u relation=%u depth=%u",
                 observer, neighbor->network_addr, neighbor->lqi, neighbor->device_type,
                 neighbor->relationship, neighbor->depth);
    }
    unsigned next = (unsigned)rsp->start_index + rsp->neighbor_table_list_count;
    ESP_LOGI(TAG, "LINK remote_end observer=0x%04x total=%u start=%u count=%u next=%u",
             observer, rsp->neighbor_table_entries, rsp->start_index, rsp->neighbor_table_list_count,
             next < rsp->neighbor_table_entries && rsp->neighbor_table_list_count ? next : 0);
}

esp_err_t zigbee_gateway_query_links(uint16_t short_addr, uint8_t start_index)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (short_addr == 0 || short_addr >= 0xfff8) return ESP_ERR_INVALID_ARG;
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(250))) return ESP_ERR_TIMEOUT;
    esp_zb_zdo_mgmt_lqi_req_param_t req = {.dst_addr = short_addr, .start_index = start_index};
    esp_zb_zdo_mgmt_lqi_req(&req, remote_links_response, (void *)(uintptr_t)short_addr);
    esp_zb_lock_release();
    return ESP_OK;
}

static void zigbee_task(void *arg)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {.max_children = APP_ZIGBEE_MAX_CHILDREN},
    };
    esp_zb_init(&cfg);

    uint8_t cluster_revision = 1;
    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(EMERGENCY_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom, 0x0000,
                    ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &cluster_revision));
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, custom,
                    ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint = {
        .endpoint = EMERGENCY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 1,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoints, clusters, endpoint));
    ESP_ERROR_CHECK(esp_zb_device_register(endpoints));
    esp_zb_core_action_handler_register(action_handler);
    esp_zb_set_primary_network_channel_set(APP_ZIGBEE_CHANNEL_MASK);

    ESP_LOGI(TAG, "Starting Zigbee coordinator on channel %u", APP_ZIGBEE_CHANNEL);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_scheduler_alarm(repeat_tick, 0, 1000);
    esp_zb_stack_main_loop();
}

esp_err_t zigbee_gateway_start(void)
{
    nvs_handle_t handle;
    if (nvs_open("gateway", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, "repeat_node", &s_repeat.target);
        nvs_close(handle);
    }
    if (s_repeat.target) s_repeat.due = esp_timer_get_time() + REPEAT_REST_US;
    esp_zb_platform_config_t platform = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_RETURN_ON_ERROR(esp_zb_platform_config(&platform), TAG, "Zigbee platform init failed");
    return xTaskCreate(zigbee_task, "zigbee", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
