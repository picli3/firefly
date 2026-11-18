#include "../../main/telemetry.c"
#include <assert.h>
#include <stdio.h>
static uint16_t voltage;
static bool ina_ok = true;
bool relay_ac_present(void) { return true; }
bool battery_test_is_active(void) { return false; }
int64_t esp_timer_get_time(void) { return 12000000; }
esp_err_t ina226_read_measurement(ina226_measurement_t *m) {
    *m = (ina226_measurement_t){voltage, -100, -400}; return ina_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t ntc_read_centi_c(int16_t *t) { *t = -123; return ESP_OK; }
int main(void) {
    battery_thresholds_t four = {3900, 3700, 3500, 3500, 4200, 3600, 3500};
    battery_thresholds_t six = {5850, 5500, 5250, 5250, 6300, 5500, 5250};
    battery_thresholds_t twelve = {11700, 11000, 10500, 10500, 12600, 11000, 10500};
    assert(estimate_soh(0, true, &four) == 0);
    assert(estimate_soh(3900, true, &four) == 50);
    assert(estimate_soh(4200, true, &four) == 100);
    assert(estimate_soh(65535, false, &four) == 100);
    assert(estimate_soh(11700, false, &twelve) == 57);
    battery_thresholds_t invalid = four; invalid.full_mv = invalid.ac_empty_mv;
    assert(estimate_soh(3500, true, &invalid) == 0); /* no division by zero */
    telemetry_init(0xf96afdfc);
    emergency_telemetry_t s = {0}; bool valid;
    voltage = 3900; sample_telemetry(&s, &valid, &four);
    assert(valid && s.device_id == 0xf96afdfc && s.payload_version == 2);
    assert(s.soh_pct == 50 && s.temp_centi_c == -123 && s.ipack_ma == -100);
    assert(!(s.flags & (EMERGENCY_FLAG_ALERT | EMERGENCY_FLAG_UVLO)));
    sample_telemetry(&s, &valid, &six);
    assert((s.flags & EMERGENCY_FLAG_ALERT) && (s.flags & EMERGENCY_FLAG_UVLO));
    voltage = 11500; sample_telemetry(&s, &valid, &twelve);
    assert(!(s.flags & (EMERGENCY_FLAG_ALERT | EMERGENCY_FLAG_UVLO)));
    voltage = 3400; sample_telemetry(&s, &valid, &four);
    assert((s.flags & EMERGENCY_FLAG_ALERT) && (s.flags & EMERGENCY_FLAG_UVLO));
    ina_ok = false; s = (emergency_telemetry_t){0};
    sample_telemetry(&s, &valid, &four);
    assert(!valid && !(s.flags & (EMERGENCY_FLAG_SENSOR_VALID | EMERGENCY_FLAG_UVLO)));
    puts("H2 telemetry: independent 4/6/12V thresholds, signed measurements and SoH bounds passed");
}
