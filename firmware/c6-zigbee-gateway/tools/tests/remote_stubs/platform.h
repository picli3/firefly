#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_GATEWAY_ETH_ENABLED 1
#define CONFIG_GATEWAY_MQTT_GATEWAY_ID "host-test"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_FAIL -1
const char *esp_err_to_name(esp_err_t e);
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef void *esp_mqtt_client_handle_t;
typedef struct {
    int current_data_offset, total_data_len, topic_len, data_len;
    char *topic, *data;
    bool retain;
} host_mqtt_event_t;
typedef host_mqtt_event_t *esp_mqtt_event_handle_t;
int esp_mqtt_client_enqueue(void *, const char *, const char *, int, int, int, bool);
typedef void *QueueHandle_t;
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define pdPASS 1
QueueHandle_t xQueueCreate(unsigned, unsigned);
void vQueueDelete(QueueHandle_t);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_i64(nvs_handle_t, const char *, int64_t);
esp_err_t nvs_get_i64(nvs_handle_t, const char *, int64_t *);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
