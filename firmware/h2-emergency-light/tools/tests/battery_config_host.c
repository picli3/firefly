/* Runs production acceptance/persistence code, replacing only RTOS and NVS. */
#include "../../main/battery_config.c"
#include <assert.h>
#include <stdio.h>
static config_store_t durable, staged;
static bool exists, fail_set, fail_commit;
static unsigned commits, writes;
const char *esp_err_to_name(esp_err_t e) { return "host"; }
uint32_t esp_crc32_le(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) { crc ^= *p++; for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1)); }
    return ~crc;
}
uint32_t esp_random(void) { return 123; }
int64_t esp_timer_get_time(void) { return 0; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned n) { return 1; }
void xSemaphoreGive(SemaphoreHandle_t s) {}
QueueHandle_t xQueueCreate(unsigned n, unsigned size) { return (void *)1; }
int xQueueSend(QueueHandle_t q, const void *p, unsigned n) { return 1; }
int xQueueReceive(QueueHandle_t q, void *p, unsigned n) { return 0; }
void vTaskDelay(unsigned n) {}
int xTaskCreate(void (*fn)(void *), const char *n, unsigned size, void *arg, unsigned pri, void *h) { return 1; }
void telemetry_wake(void) {}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) {
    if (mode == NVS_READONLY && !exists) return ESP_ERR_NVS_NOT_FOUND;
    staged = durable; *h = 1; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n) {
    assert(n == sizeof(staged)); ++writes;
    if (fail_set) return ESP_FAIL;
    memcpy(&staged, p, n); return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n) {
    assert(*n >= sizeof(durable));
    memcpy(p, &durable, sizeof(durable)); *n = sizeof(durable); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    ++commits; if (fail_commit) return ESP_FAIL;
    durable = staged; exists = true; return ESP_OK;
}
void nvs_close(nvs_handle_t h) {}
static esp_err_t sender(const battery_config_ack_t *ack) { return ESP_OK; }
static battery_config_request_t request(unsigned revision) {
    battery_config_request_t r = {.version = 1, .revision = revision,
        .thresholds = {3900, 3700, 3500, 3500, 4200, 3600, 3500}};
    r.command_uuid[6] = 0x40; r.command_uuid[8] = 0x80; r.command_uuid[15] = revision;
    return r;
}
int main(void) {
    assert(sizeof(emergency_telemetry_t) == 24 && sizeof(emergency_telemetry_v2_t) == 33);
    assert(sizeof(battery_test_report_t) == 26 && sizeof(battery_test_report_v2_t) == 35);
    assert(sizeof(battery_config_request_t) == 35 && sizeof(battery_config_ack_t) == 27);
    assert(battery_config_init(0xf96afdfc, sender) == ESP_OK);
    battery_config_snapshot_t snapshot;
    battery_config_snapshot(&snapshot);
    assert(snapshot.revision == 0 && snapshot.thresholds.full_mv == 6300);
    battery_config_request_t r = request(1);
    battery_config_ack_t ack = accept(&r);
    assert(ack.status == BATTERY_CONFIG_APPLIED && commits == 1);
    battery_config_snapshot(&snapshot);
    assert(snapshot.revision == 1 && snapshot.thresholds.full_mv == 4200);
    for (int i = 0; i < 10; ++i) assert(accept(&r).status == BATTERY_CONFIG_APPLIED);
    assert(commits == 1 && writes == 1);
    r.thresholds.alert_mv++;
    assert(accept(&r).reason == BATTERY_CONFIG_REASON_CONFLICT && commits == 1);
    r = request(2); r.thresholds.ac_empty_mv = r.thresholds.full_mv;
    assert(accept(&r).reason == BATTERY_CONFIG_REASON_INVALID && commits == 1);
    r = request(2); fail_set = true;
    assert(accept(&r).reason == BATTERY_CONFIG_REASON_STORAGE);
    fail_set = false; fail_commit = true;
    assert(accept(&r).reason == BATTERY_CONFIG_REASON_STORAGE);
    battery_config_snapshot(&snapshot); assert(snapshot.revision == 1);
    fail_commit = false;
    /* Reboot before a failed commit retains last complete profile. */
    assert(battery_config_init(0xf96afdfc, sender) == ESP_OK);
    battery_config_snapshot(&snapshot); assert(snapshot.revision == 1);
    battery_config_begin_test(&snapshot); assert(snapshot.revision == 1);
    ack = accept(&r); assert(ack.status == BATTERY_CONFIG_DEFERRED);
    assert(s_store.active.revision == 1 && durable.pending.revision == 2);
    unsigned before = commits;
    assert(accept(&r).status == BATTERY_CONFIG_DEFERRED && commits == before);
    assert(!apply_pending_locked(&ack));
    battery_config_request_t newer = request(3);
    assert(accept(&newer).status == BATTERY_CONFIG_DEFERRED);
    assert(accept(&r).reason == BATTERY_CONFIG_REASON_STALE);
    /* Snapshot retained by the test still names its initial profile. */
    assert(snapshot.revision == 1);
    battery_config_end_test(); fail_commit = true;
    assert(!apply_pending_locked(&ack) && s_store.active.revision == 1);
    fail_commit = false;
    assert(apply_pending_locked(&ack) && ack.status == BATTERY_CONFIG_APPLIED && ack.revision == 3);
    assert(!s_store.pending_valid && snapshot.revision == 1);
    assert(accept(&r).reason == BATTERY_CONFIG_REASON_STALE);
    /* Restart restores active state and a persisted pending profile independently. */
    battery_config_begin_test(&snapshot); r = request(4);
    assert(accept(&r).status == BATTERY_CONFIG_DEFERRED);
    s_testing = false; assert(battery_config_init(0xf96afdfc, sender) == ESP_OK);
    assert(s_store.active.revision == 3 && s_store.pending.revision == 4);
    assert(apply_pending_locked(&ack) && ack.revision == 4);
    config_store_t lamp_four = durable;
    /* Another H2 starts with its own flash and can independently use a 6V profile. */
    exists = false;
    assert(battery_config_init(0x8e6dcd6a, sender) == ESP_OK);
    r = request(1); r.thresholds = (battery_thresholds_t){5850, 5500, 5250, 5250, 6300, 5500, 5250};
    assert(accept(&r).status == BATTERY_CONFIG_APPLIED);
    battery_config_snapshot(&snapshot); assert(snapshot.thresholds.full_mv == 6300 && snapshot.revision == 1);
    durable = lamp_four; exists = true;
    assert(battery_config_init(0xf96afdfc, sender) == ESP_OK);
    battery_config_snapshot(&snapshot); assert(snapshot.thresholds.full_mv == 4200 && snapshot.revision == 4);
    /* Corrupt data is preserved, never silently replaced or called applied. */
    durable.crc ^= 1; config_store_t corrupt = durable;
    assert(battery_config_init(0xf96afdfc, sender) == ESP_OK);
    r = request(5); assert(accept(&r).reason == BATTERY_CONFIG_REASON_STORAGE);
    assert(!memcmp(&durable, &corrupt, sizeof(durable)));
    battery_config_snapshot(&snapshot); assert(snapshot.revision == 0);
    puts("H2 config: duplicate, stale, conflict, thresholds, write/commit faults, reboot, deferred and CRC passed");
}
