#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef HOST_ETH_DISABLED
#define CONFIG_GATEWAY_ETH_ENABLED 1
#endif
#define CONFIG_GATEWAY_MQTT_GATEWAY_ID "host-test"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_LOGW(...) ((void)0)
typedef void *esp_mqtt_client_handle_t;
enum { MQTT_EVENT_DATA, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_ERROR };
typedef struct {
    int event_id, msg_id, current_data_offset, total_data_len, topic_len, data_len;
    char *topic, *data;
    bool retain;
} host_mqtt_event_t;
typedef host_mqtt_event_t *esp_mqtt_event_handle_t;
int esp_mqtt_client_enqueue(void *, const char *, const char *, int, int, int, bool);
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
QueueHandle_t xQueueCreate(unsigned, unsigned);
void vQueueDelete(QueueHandle_t);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
