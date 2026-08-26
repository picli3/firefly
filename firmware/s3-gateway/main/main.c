#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#define UART_PORT                UART_NUM_1
#define UART_RX_GPIO             GPIO_NUM_17
#define UART_TX_GPIO             GPIO_NUM_18
#define UART_BAUD_RATE           115200
#define UART_SOF_0               0xA5
#define UART_SOF_1               0x5A
#define UART_PROTOCOL_VERSION    1
#define UART_TYPE_TELEMETRY      1
#define UART_MAX_PAYLOAD         128
#define WIFI_CONNECTED_BIT       BIT0
#define MQTT_CONNECTED_BIT       BIT1

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

typedef enum {
    PARSER_SOF_0,
    PARSER_SOF_1,
    PARSER_HEADER,
    PARSER_PAYLOAD,
    PARSER_CRC,
} parser_state_t;

typedef struct {
    parser_state_t state;
    uint8_t header[4];
    uint8_t payload[UART_MAX_PAYLOAD];
    uint8_t crc_bytes[2];
    size_t position;
    uint16_t length;
} uart_parser_t;

static const char *TAG = "s3_gateway";
static EventGroupHandle_t s_events;
static QueueHandle_t s_telemetry_queue;
static esp_mqtt_client_handle_t s_mqtt;

static uint16_t crc16_ccitt(const uint8_t *data, size_t length, uint16_t crc)
{
    while (length--) {
        crc ^= (uint16_t)*data++ << 8;
        for (int bit = 0; bit < 8; bit++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

static void parser_reset(uart_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
    parser->state = PARSER_SOF_0;
}

static void parser_emit(uart_parser_t *parser)
{
    uint16_t expected = (uint16_t)parser->crc_bytes[0] | ((uint16_t)parser->crc_bytes[1] << 8);
    uint16_t actual = crc16_ccitt(parser->header, sizeof(parser->header), 0xFFFF);
    actual = crc16_ccitt(parser->payload, parser->length, actual);
    if (actual != expected) {
        ESP_LOGW(TAG, "UART CRC mismatch: expected %04x got %04x", expected, actual);
        return;
    }
    if (parser->header[0] != UART_PROTOCOL_VERSION || parser->header[1] != UART_TYPE_TELEMETRY ||
        parser->length != sizeof(emergency_telemetry_t)) {
        ESP_LOGW(TAG, "Unsupported UART frame");
        return;
    }
    emergency_telemetry_t sample;
    memcpy(&sample, parser->payload, sizeof(sample));
    if (sample.payload_version != 1) return;
    if (xQueueSend(s_telemetry_queue, &sample, 0) != pdTRUE) ESP_LOGW(TAG, "Telemetry queue full; dropping new sample");
}

static void parser_feed(uart_parser_t *p, uint8_t byte)
{
    switch (p->state) {
    case PARSER_SOF_0:
        if (byte == UART_SOF_0) p->state = PARSER_SOF_1;
        break;
    case PARSER_SOF_1:
        if (byte == UART_SOF_1) { p->state = PARSER_HEADER; p->position = 0; }
        else p->state = byte == UART_SOF_0 ? PARSER_SOF_1 : PARSER_SOF_0;
        break;
    case PARSER_HEADER:
        p->header[p->position++] = byte;
        if (p->position == sizeof(p->header)) {
            p->length = (uint16_t)p->header[2] | ((uint16_t)p->header[3] << 8);
            if (p->length == 0 || p->length > UART_MAX_PAYLOAD) parser_reset(p);
            else { p->state = PARSER_PAYLOAD; p->position = 0; }
        }
        break;
    case PARSER_PAYLOAD:
        p->payload[p->position++] = byte;
        if (p->position == p->length) { p->state = PARSER_CRC; p->position = 0; }
        break;
    case PARSER_CRC:
        p->crc_bytes[p->position++] = byte;
        if (p->position == 2) { parser_emit(p); parser_reset(p); }
        break;
    }
}

static void uart_task(void *arg)
{
    uint8_t buffer[128];
    uart_parser_t parser;
    parser_reset(&parser);
    while (true) {
        int count = uart_read_bytes(UART_PORT, buffer, sizeof(buffer), pdMS_TO_TICKS(1000));
        for (int i = 0; i < count; i++) parser_feed(&parser, buffer[i]);
    }
}

static void publisher_task(void *arg)
{
    emergency_telemetry_t sample;
    char topic[96];
    char json[384];
    while (true) {
        if (xQueueReceive(s_telemetry_queue, &sample, portMAX_DELAY) != pdTRUE) continue;
        while (!(xEventGroupGetBits(s_events) & MQTT_CONNECTED_BIT)) vTaskDelay(pdMS_TO_TICKS(1000));

        snprintf(topic, sizeof(topic), "%s/%08" PRIx32 "/telemetry", CONFIG_EMERGENCY_MQTT_TOPIC_PREFIX, sample.device_id);
        snprintf(json, sizeof(json),
                 "{\"schema_version\":1,\"device_id\":\"%08" PRIx32 "\",\"sequence\":%u,"
                 "\"vpack\":%.3f,\"ipack\":%.3f,\"power_w\":%.3f,\"temp_pack\":%.2f,"
                 "\"ac_present\":%s,\"alert\":%s,\"uvlo\":%s,\"sensor_valid\":%s,\"self_test\":%s,"
                 "\"soh_pct\":%u,\"node_uptime_s\":%" PRIu32 "}",
                 sample.device_id, sample.sequence, sample.vpack_mv / 1000.0, sample.ipack_ma / 1000.0,
                 sample.power_mw / 1000.0, sample.temp_centi_c / 100.0,
                 (sample.flags & BIT0) ? "true" : "false", (sample.flags & BIT1) ? "true" : "false",
                 (sample.flags & BIT2) ? "true" : "false", (sample.flags & BIT3) ? "true" : "false",
                 (sample.flags & BIT4) ? "true" : "false", sample.soh_pct, sample.uptime_s);
        int id = esp_mqtt_client_publish(s_mqtt, topic, json, 0, 1, 0);
        if (id < 0) {
            ESP_LOGW(TAG, "MQTT publish failed; retrying sample");
            xQueueSendToFront(s_telemetry_queue, &sample, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            ESP_LOGI(TAG, "Published %s msg_id=%d", topic, id);
        }
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t wifi = {0};
    strlcpy((char *)wifi.sta.ssid, CONFIG_EMERGENCY_WIFI_SSID, sizeof(wifi.sta.ssid));
    strlcpy((char *)wifi.sta.password, CONFIG_EMERGENCY_WIFI_PASSWORD, sizeof(wifi.sta.password));
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id == MQTT_EVENT_CONNECTED) xEventGroupSetBits(s_events, MQTT_CONNECTED_BIT);
    else if (id == MQTT_EVENT_DISCONNECTED) xEventGroupClearBits(s_events, MQTT_CONNECTED_BIT);
    else if (id == MQTT_EVENT_ERROR) ESP_LOGW(TAG, "MQTT transport error");
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_EMERGENCY_MQTT_URI,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = CONFIG_EMERGENCY_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_EMERGENCY_MQTT_PASSWORD,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.reconnect_timeout_ms = 5000,
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt));
}

void app_main(void)
{
    esp_err_t nvs_status = nvs_flash_init();
    if (nvs_status == ESP_ERR_NVS_NO_FREE_PAGES || nvs_status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_status);
    s_events = xEventGroupCreate();
    s_telemetry_queue = xQueueCreate(32, sizeof(emergency_telemetry_t));

    uart_config_t uart_cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    wifi_init();
    mqtt_init();
    xTaskCreate(uart_task, "uart_rx", 4096, NULL, 6, NULL);
    xTaskCreate(publisher_task, "mqtt_publish", 4096, NULL, 5, NULL);
}
