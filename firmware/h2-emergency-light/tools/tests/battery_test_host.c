#include "../../main/battery_test.c"
#include <assert.h>
#include <stdio.h>
static bool ac = true, energized;
static battery_test_report_v2_t sent;
static battery_config_snapshot_t active_config = {.revision = 1,
    .thresholds = {3900, 3700, 3500, 3500, 4200, 3600, 3500}};
const char *esp_err_to_name(esp_err_t e) { return "host"; }
int64_t esp_timer_get_time(void) { return 1000000; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned n) { return 1; }
void xSemaphoreGive(SemaphoreHandle_t s) {}
void vTaskDelay(unsigned n) {}
int xTaskCreate(void (*fn)(void *), const char *n, unsigned size, void *arg, unsigned pri, void *h) { return 1; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) { return ESP_FAIL; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n) { return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n) { return ESP_FAIL; }
esp_err_t nvs_commit(nvs_handle_t h) { return ESP_OK; }
void nvs_close(nvs_handle_t h) {}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) { return ESP_OK; }
bool relay_ac_present(void) { return ac; }
bool relay_test_button_pressed(void) { return false; }
void relay_set_test_active(bool on) { energized = on; }
void battery_config_snapshot(battery_config_snapshot_t *s) { *s = active_config; }
void battery_config_begin_test(battery_config_snapshot_t *s) { *s = active_config; }
void battery_config_end_test(void) {
    /* An arriving pending profile becomes active before report transmission. */
    active_config.revision = 2;
    active_config.thresholds.test_min_mv = 5000;
}
uint32_t battery_config_boot_id(void) { return 77; }
static esp_err_t sender(const battery_test_report_v2_t *r) { sent = *r; return ESP_OK; }
static bool ready(void) { return true; }
int main(void) {
    assert(battery_test_init(0xf96afdfc, sender, ready, NULL) == ESP_OK);
    emergency_telemetry_t sample = {.vpack_mv = 4000, .ipack_ma = 100,
        .power_mw = 400, .temp_centi_c = -250, .flags = EMERGENCY_FLAG_SENSOR_VALID};
    assert(battery_test_start(301, 1) == ESP_ERR_INVALID_ARG && !energized);
    assert(battery_test_start(30, 1) == ESP_OK && energized);
    battery_test_record(&sample, true);
    stop_test(BATTERY_TEST_PASS);
    assert(!energized && !battery_test_is_active());
    assert(active_config.revision == 2 && sent.config_version == 1);
    assert(sent.report.result == BATTERY_TEST_PASS && sent.report.version == 2);
    assert(sent.report.temperature_max_centi_c == -250 && sent.boot_id == 77);
    assert(battery_test_start(30, 2) == ESP_OK && energized);
    battery_test_record(&sample, true);
    stop_test(BATTERY_TEST_PASS);
    assert(sent.config_version == 2 && sent.report.result == BATTERY_TEST_LOW_VOLTAGE);
    ac = false;
    assert(battery_test_start(30, 3) == ESP_ERR_INVALID_STATE && !energized);
    assert(sent.config_version == 2 && sent.report.result == BATTERY_TEST_AC_NOT_PRESENT);
    puts("H2 test: deferred profile preserves start revision/result; negative temperature, limits and AC passed");
}
