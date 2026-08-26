#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs_flash.h"

#define TELEMETRY_ENDPOINT       1
#define TELEMETRY_CLUSTER_ID     0xFC00
#define TELEMETRY_COMMAND_ID     0x01
#define ZIGBEE_CHANNEL_MASK      (1UL << 15)
#define UART_PORT                UART_NUM_1
#define UART_TX_GPIO             GPIO_NUM_12
#define UART_RX_GPIO             GPIO_NUM_4
#define UART_BAUD_RATE           115200
#define UART_SOF_0               0xA5
#define UART_SOF_1               0x5A
#define UART_PROTOCOL_VERSION    1
#define UART_TYPE_TELEMETRY      1
#define ZIGBEE_MAX_CHILDREN      50

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

static const char *TAG = "h2_coordinator";

static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    while (length--) {
        crc ^= (uint16_t)*data++ << 8;
        for (int bit = 0; bit < 8; bit++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

static void uart_send_telemetry(const emergency_telemetry_t *payload)
{
    uint8_t frame[2 + 4 + sizeof(*payload) + 2];
    frame[0] = UART_SOF_0;
    frame[1] = UART_SOF_1;
    frame[2] = UART_PROTOCOL_VERSION;
    frame[3] = UART_TYPE_TELEMETRY;
    frame[4] = sizeof(*payload) & 0xFF;
    frame[5] = sizeof(*payload) >> 8;
    memcpy(&frame[6], payload, sizeof(*payload));
    uint16_t crc = crc16_ccitt(&frame[2], 4 + sizeof(*payload));
    frame[6 + sizeof(*payload)] = crc & 0xFF;
    frame[7 + sizeof(*payload)] = crc >> 8;
    uart_write_bytes(UART_PORT, frame, sizeof(frame));
}

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id != ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID) return ESP_OK;
    const esp_zb_zcl_custom_cluster_command_message_t *msg = message;
    ESP_RETURN_ON_FALSE(msg && msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Invalid ZCL command");
    ESP_RETURN_ON_FALSE(msg->info.cluster == TELEMETRY_CLUSTER_ID &&
                        msg->info.command.id == TELEMETRY_COMMAND_ID &&
                        msg->data.size == sizeof(emergency_telemetry_t), ESP_ERR_INVALID_SIZE, TAG, "Unexpected telemetry command");

    emergency_telemetry_t sample;
    memcpy(&sample, msg->data.value, sizeof(sample));
    ESP_RETURN_ON_FALSE(sample.payload_version == 1, ESP_ERR_INVALID_VERSION, TAG, "Unsupported payload version");
    uart_send_telemetry(&sample);
    ESP_LOGI(TAG, "Node %08" PRIx32 " seq=%u V=%u mV forwarded", sample.device_id, sample.sequence, sample.vpack_mv);
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
                ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION));
            } else {
                ESP_ERROR_CHECK(esp_zb_bdb_open_network(180));
            }
        }
        break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (signal->esp_err_status == ESP_OK) {
            ESP_LOGI(TAG, "Network formed: PAN 0x%04x channel %u", esp_zb_get_pan_id(), esp_zb_get_current_channel());
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
        } else {
            esp_zb_scheduler_alarm((esp_zb_callback_t)commissioning_retry, ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        esp_zb_zdo_signal_device_annce_params_t *p = esp_zb_app_signal_get_params(signal->p_app_signal);
        ESP_LOGI(TAG, "Node joined: short address 0x%04x", p->device_short_addr);
        break;
    }
    default:
        ESP_LOGI(TAG, "ZDO signal %s status %s", esp_zb_zdo_signal_to_string(type), esp_err_to_name(signal->esp_err_status));
        break;
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
    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(TELEMETRY_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom, 0x0000, ESP_ZB_ZCL_ATTR_TYPE_U8,
                    ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &cluster_revision));
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, custom, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep = {
        .endpoint = TELEMETRY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
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
    uart_config_t uart_cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_zb_platform_config_t platform = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    xTaskCreate(zigbee_task, "zigbee", 6144, NULL, 5, NULL);
}
