#include "telemetry_log.h"

#include <inttypes.h>

#include "esp_log.h"

static const char *TAG = "c6_zigbee_gateway";

const char *telemetry_log_test_result(uint8_t result)
{
    static const char *const names[] = {
        "PASS", "LOW_VOLTAGE", "LOW_CURRENT", "HIGH_TEMPERATURE",
        "AC_NOT_PRESENT", "SENSOR_INVALID", "ABORTED",
    };
    return result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}

void telemetry_log_sample(uint16_t short_addr, const emergency_telemetry_t *sample)
{
    ESP_LOGI(TAG,
             "RX short=0x%04x node=%08" PRIx32 " seq=%u V=%u_mV I=%d_mA P=%" PRId32
             "_mW T=%.2f_C SoH=%u%% AC=%u alert=%u UVLO=%u sensor_valid=%u self_test=%u uptime=%" PRIu32 "_s",
             short_addr, sample->device_id, sample->sequence, sample->vpack_mv, sample->ipack_ma,
             sample->power_mw, sample->temp_centi_c / 100.0, sample->soh_pct,
             !!(sample->flags & EMERGENCY_FLAG_AC_PRESENT), !!(sample->flags & EMERGENCY_FLAG_ALERT),
             !!(sample->flags & EMERGENCY_FLAG_UVLO), !!(sample->flags & EMERGENCY_FLAG_SENSOR_VALID),
             !!(sample->flags & EMERGENCY_FLAG_SELF_TEST), sample->uptime_s);
}

void telemetry_log_test_report(uint16_t short_addr, const battery_test_report_t *report)
{
    ESP_LOGI(TAG,
             "TEST short=0x%04x node=%08" PRIx32 " id=%u result=%s V_before=%u_mV V_min=%u_mV "
             "I_avg=%d_mA I_peak=%d_mA P_avg=%" PRId32 "_mW T_max=%.2f_C duration=%u_s samples=%u",
             short_addr, report->device_id, report->test_id, telemetry_log_test_result(report->result),
             report->voltage_before_mv, report->voltage_min_mv, report->current_avg_ma,
             report->current_peak_ma, report->power_avg_mw,
             report->temperature_max_centi_c / 100.0, report->duration_s, report->sample_count);
}
