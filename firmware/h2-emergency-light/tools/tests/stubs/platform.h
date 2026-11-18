#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_RETURN_ON_FALSE(c, e, ...) do { if (!(c)) return (e); } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(e) ((void)(e))
const char *esp_err_to_name(esp_err_t);
uint32_t esp_crc32_le(uint32_t, const uint8_t *, uint32_t);
uint32_t esp_random(void);
int64_t esp_timer_get_time(void);
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned, unsigned);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
void vTaskDelay(unsigned);
unsigned ulTaskNotifyTake(int, unsigned);
void xTaskNotifyGive(TaskHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
