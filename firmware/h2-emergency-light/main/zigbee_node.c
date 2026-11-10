#include "zigbee_node.h"

#include <string.h>

#include "app_config.h"
#include "battery_test.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "sdkconfig.h"
#include "telemetry.h"

#if !CONFIG_ZB_ZCZR
#error "Select the Zigbee coordinator/router (ZCZR) stack for h2-emergency-light"
#endif

static const char *TAG = "zigbee_node";
static bool s_joined;

static esp_err_t send_command(uint8_t command_id, const void *payload, uint16_t size)
{
    if (!s_joined) return ESP_ERR_INVALID_STATE;

    esp_zb_zcl_custom_cluster_cmd_req_t req = {0};
    req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    req.zcl_basic_cmd.dst_endpoint = EMERGENCY_ENDPOINT;
    req.zcl_basic_cmd.src_endpoint = EMERGENCY_ENDPOINT;
    req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_id = EMERGENCY_CLUSTER_ID;
    req.custom_cmd_id = command_id;
    req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    req.data.type = ESP_ZB_ZCL_ATTR_TYPE_SET;
    req.data.size = size;
    req.data.value = (void *)payload;

    esp_zb_lock_acquire(portMAX_DELAY);
    uint8_t sequence = esp_zb_zcl_custom_cluster_cmd_req(&req);
    esp_zb_lock_release();
    ESP_LOGD(TAG, "Zigbee command 0x%02x queued with TSN=%u", command_id, sequence);
    return ESP_OK;
}

esp_err_t zigbee_node_send_telemetry(const emergency_telemetry_t *sample)
{
    return send_command(EMERGENCY_CMD_TELEMETRY, sample, sizeof(*sample));
}

esp_err_t zigbee_node_send_test_report(const battery_test_report_t *report)
{
    return send_command(EMERGENCY_CMD_TEST_REPORT, report, sizeof(*report));
}

bool zigbee_node_is_joined(void)
{
    return s_joined;
}

static esp_err_t action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id != ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID) return ESP_OK;
    const esp_zb_zcl_custom_cluster_command_message_t *msg = message;
    ESP_RETURN_ON_FALSE(msg && msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid Zigbee command");
    if (msg->info.cluster != EMERGENCY_CLUSTER_ID) return ESP_OK;

    if (msg->info.command.id == EMERGENCY_CMD_START_TEST) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(uint16_t), ESP_ERR_INVALID_SIZE, TAG,
                            "Invalid test command size");
        const uint8_t *data = msg->data.value;
        uint16_t duration_s = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
        return battery_test_start(duration_s, 0);
    }

    if (msg->info.command.id == EMERGENCY_CMD_SCHEDULE_TEST) {
        ESP_RETURN_ON_FALSE(msg->data.size == sizeof(battery_test_schedule_t), ESP_ERR_INVALID_SIZE, TAG,
                            "Invalid schedule size");
        battery_test_schedule_t schedule;
        memcpy(&schedule, msg->data.value, sizeof(schedule));
        return battery_test_schedule(&schedule);
    }
    return ESP_OK;
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
                s_joined = true;
                telemetry_wake();
            }
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        s_joined = signal->esp_err_status == ESP_OK;
        if (s_joined) telemetry_wake();
        if (!s_joined) {
            esp_zb_scheduler_alarm((esp_zb_callback_t)commissioning_retry, ESP_ZB_BDB_MODE_NETWORK_STEERING,
                                   APP_COMMISSIONING_RETRY_MS);
        }
        ESP_LOGI(TAG, "Zigbee network %s", s_joined ? "joined" : "not joined");
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal %s status %s", esp_zb_zdo_signal_to_string(type),
                 esp_err_to_name(signal->esp_err_status));
        break;
    }
}

static void zigbee_task(void *arg)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {.max_children = APP_ZIGBEE_MAX_CHILDREN},
    };
    esp_zb_init(&cfg);
    esp_zb_sleep_enable(false);
    ESP_LOGI(TAG, "Zigbee router enabled; receiver stays on while powered");

    uint8_t cluster_revision = 1;
    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(EMERGENCY_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom, 0x0000, ESP_ZB_ZCL_ATTR_TYPE_U8,
                    ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &cluster_revision));
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep = {
        .endpoint = EMERGENCY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 1,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoints, clusters, ep));
    ESP_ERROR_CHECK(esp_zb_device_register(endpoints));
    esp_zb_core_action_handler_register(action_handler);
    esp_zb_set_primary_network_channel_set(APP_ZIGBEE_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

esp_err_t zigbee_node_init(void)
{
    esp_zb_platform_config_t platform = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    return esp_zb_platform_config(&platform);
}

esp_err_t zigbee_node_start(void)
{
    return xTaskCreate(zigbee_task, "zigbee", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
