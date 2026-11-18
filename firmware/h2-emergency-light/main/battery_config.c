#include "battery_config.h"
#include <stddef.h>
#include <string.h>
#include "app_config.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "telemetry.h"

/* One atomic NVS blob: a torn commit cannot mix active and pending fields. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t format;
    uint8_t active_valid, pending_valid;
    battery_config_request_t active, pending;
    uint32_t crc;
} config_store_t;
static config_store_t s_store;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_requests;
static bool s_testing, s_storage_ok;
static uint32_t s_device, s_boot;
static battery_config_sender_t s_sender;
static const char *TAG = "battery_config";

static uint32_t checksum(const config_store_t *v)
{
    return esp_crc32_le(0, (const uint8_t *)v, offsetof(config_store_t, crc));
}
static bool request_valid(const battery_config_request_t *r)
{
    return r->version == EMERGENCY_CONFIG_VERSION && r->revision &&
           battery_thresholds_valid(&r->thresholds);
}
static esp_err_t save(config_store_t *next)
{
    nvs_handle_t nvs;
    next->crc = checksum(next);
    esp_err_t err = nvs_open("battery_config", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "state", next, sizeof(*next));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) s_store = *next;
    else ESP_LOGE(TAG, "Configuration commit failed: %s; previous profile retained", esp_err_to_name(err));
    return err;
}
static void snapshot_locked(battery_config_snapshot_t *out)
{
    *out = (battery_config_snapshot_t){ .thresholds = {
        APP_ATTENTION_VOLTAGE_MV, APP_ALERT_VOLTAGE_MV, APP_UVLO_VOLTAGE_MV,
        APP_TEST_MIN_VOLTAGE_MV, APP_SOH_FULL_VOLTAGE_MV,
        APP_SOH_AC_EMPTY_VOLTAGE_MV, APP_SOH_DC_EMPTY_VOLTAGE_MV,
    }};
    if (s_store.active_valid) {
        out->thresholds = s_store.active.thresholds;
        out->revision = s_store.active.revision;
    }
}
void battery_config_snapshot(battery_config_snapshot_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snapshot_locked(out);
    xSemaphoreGive(s_mutex);
}
void battery_config_begin_test(battery_config_snapshot_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_testing = true;
    snapshot_locked(out);
    xSemaphoreGive(s_mutex);
}
void battery_config_end_test(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_testing = false;
    xSemaphoreGive(s_mutex);
    /* Worker applies pending state and sends ACK outside Zigbee/test locks. */
}
uint32_t battery_config_boot_id(void) { return s_boot; }

static battery_config_ack_t ack_for(const battery_config_request_t *r, uint8_t status, uint8_t reason)
{
    battery_config_ack_t ack = {.version = EMERGENCY_CONFIG_VERSION, .device_id = s_device,
        .revision = r->revision, .status = status, .reason = reason};
    memcpy(ack.command_uuid, r->command_uuid, 16);
    return ack;
}
/* Caller holds mutex. No radio operation while holding it. */
static battery_config_ack_t accept(const battery_config_request_t *r)
{
    uint8_t reason = BATTERY_CONFIG_REASON_NONE;
    if (!request_valid(r)) reason = BATTERY_CONFIG_REASON_INVALID;
    else if (!s_storage_ok) reason = BATTERY_CONFIG_REASON_STORAGE;
    const battery_config_request_t *entries[] = {&s_store.active, &s_store.pending};
    const bool valid[] = {s_store.active_valid, s_store.pending_valid};
    if (!reason) {
        for (unsigned i = 0; i < 2; ++i) {
            if (!valid[i]) continue;
            if (r->revision == entries[i]->revision || !memcmp(r->command_uuid, entries[i]->command_uuid, 16)) {
                if (!memcmp(r, entries[i], sizeof(*r)))
                    return ack_for(r, i ? BATTERY_CONFIG_DEFERRED : BATTERY_CONFIG_APPLIED, 0);
                reason = BATTERY_CONFIG_REASON_CONFLICT;
                break;
            }
            if (r->revision < entries[i]->revision) reason = BATTERY_CONFIG_REASON_STALE;
        }
    }
    if (reason) return ack_for(r, BATTERY_CONFIG_REJECTED, reason);
    config_store_t next = s_store;
    if (s_testing) {
        next.pending = *r;
        next.pending_valid = 1;
    } else {
        next.active = *r;
        next.active_valid = 1;
        next.pending_valid = 0;
    }
    if (save(&next) != ESP_OK) return ack_for(r, BATTERY_CONFIG_REJECTED, BATTERY_CONFIG_REASON_STORAGE);
    ESP_LOGI(TAG, "revision=%lu %s", (unsigned long)r->revision, s_testing ? "deferred" : "applied");
    telemetry_wake();
    return ack_for(r, s_testing ? BATTERY_CONFIG_DEFERRED : BATTERY_CONFIG_APPLIED, 0);
}
static bool apply_pending_locked(battery_config_ack_t *ack)
{
    if (s_testing || !s_store.pending_valid) return false;
    config_store_t next = s_store;
    next.active = next.pending;
    next.active_valid = 1;
    next.pending_valid = 0;
    if (save(&next) != ESP_OK) return false;
    *ack = ack_for(&s_store.active, BATTERY_CONFIG_APPLIED, 0);
    telemetry_wake();
    return true;
}
static void config_task(void *arg)
{
    int64_t announce_due = 0;
    battery_config_ack_t retry = {0};
    bool retry_valid = false;
    while (true) {
        battery_config_request_t req;
        /* Retain a failed ACK until Zigbee is ready; bounded request queue backpressures. */
        if (!retry_valid && xQueueReceive(s_requests, &req, pdMS_TO_TICKS(200)) == pdTRUE) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            retry = accept(&req);
            xSemaphoreGive(s_mutex);
            retry_valid = true;
        } else vTaskDelay(pdMS_TO_TICKS(200));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        /* Applying a persisted profile must not wait for an ACK radio retry. */
        battery_config_ack_t applied;
        if (apply_pending_locked(&applied)) {
            retry = applied; /* applied supersedes the same command's deferred ACK */
            retry_valid = true;
        }
        config_store_t snapshot = s_store;
        xSemaphoreGive(s_mutex);
        if (retry_valid && s_sender(&retry) == ESP_OK) retry_valid = false;
        int64_t now = esp_timer_get_time();
        if (now >= announce_due) {
            /* Real persisted H2 ACKs are replayable after gateway/MQTT reconnect. */
            if (snapshot.active_valid) {
                battery_config_ack_t ack = ack_for(&snapshot.active, BATTERY_CONFIG_APPLIED, 0);
                s_sender(&ack);
            }
            if (snapshot.pending_valid) {
                battery_config_ack_t ack = ack_for(&snapshot.pending, BATTERY_CONFIG_DEFERRED, 0);
                s_sender(&ack);
            }
            announce_due = now + 30000000LL;
        }
    }
}
esp_err_t battery_config_receive(const battery_config_request_t *req)
{
    return req && xQueueSend(s_requests, req, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t battery_config_init(uint32_t device, battery_config_sender_t sender)
{
    s_device = device;
    s_boot = esp_random();
    s_sender = sender;
    s_mutex = xSemaphoreCreateMutex();
    s_requests = xQueueCreate(8, sizeof(battery_config_request_t));
    if (!s_mutex || !s_requests || !sender) return ESP_ERR_NO_MEM;
    s_store = (config_store_t){.magic = 0x42434647, .format = 1};
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("battery_config", NVS_READONLY, &nvs);
    s_storage_ok = err == ESP_ERR_NVS_NOT_FOUND;
    if (err == ESP_OK) {
        config_store_t loaded;
        size_t size = sizeof(loaded);
        err = nvs_get_blob(nvs, "state", &loaded, &size);
        s_storage_ok = err == ESP_ERR_NVS_NOT_FOUND;
        if (err == ESP_OK && size == sizeof(loaded) && loaded.magic == s_store.magic &&
            loaded.format == 1 && loaded.crc == checksum(&loaded) &&
            loaded.active_valid <= 1 && loaded.pending_valid <= 1 &&
            (!loaded.active_valid || request_valid(&loaded.active)) &&
            (!loaded.pending_valid || (request_valid(&loaded.pending) &&
                (!loaded.active_valid || loaded.pending.revision > loaded.active.revision)))) {
            s_store = loaded;
            s_storage_ok = true;
        }
        nvs_close(nvs);
    }
    if (!s_storage_ok) ESP_LOGE(TAG, "Invalid/unreadable config storage preserved; using unconfirmed legacy defaults");
    else if (!s_store.active_valid) ESP_LOGW(TAG, "Unconfirmed legacy battery thresholds; config_version unknown");
    return xTaskCreate(config_task, "battery_config", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
