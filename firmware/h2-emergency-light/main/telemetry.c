#include "telemetry.h"

#include <limits.h>
#include <stdbool.h>

#include "app_config.h"
#include "battery_test.h"
#include "battery_config.h"
#include "emergency_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ina226.h"
#include "ntc.h"
#include "relay.h"
#include "zigbee_node.h"

static const char *TAG = "telemetry";
static TaskHandle_t s_task;
static uint16_t s_sequence;
static uint32_t s_device_id;

static uint8_t estimate_soh(uint16_t voltage_mv, bool ac_present, const battery_thresholds_t *t)
{
    uint16_t empty = ac_present ? t->ac_empty_mv : t->dc_empty_mv;
    if (voltage_mv >= t->full_mv) return 100;
    if (voltage_mv <= empty || t->full_mv <= empty) return 0;
    return (uint8_t)((voltage_mv - empty) * 100U / (t->full_mv - empty));
}

static esp_err_t sample_telemetry(emergency_telemetry_t *sample, bool *voltage_valid,
                                 const battery_thresholds_t *thresholds)
{
    bool ac_present = relay_ac_present();
    sample->payload_version = EMERGENCY_TELEMETRY_VERSION_V2;
    sample->flags = ac_present ? EMERGENCY_FLAG_AC_PRESENT : 0;
    if (battery_test_is_active()) sample->flags |= EMERGENCY_FLAG_SELF_TEST;
    sample->sequence = s_sequence++;
    sample->device_id = s_device_id;

    ina226_measurement_t measurement;
    bool ina_valid = ina226_read_measurement(&measurement) == ESP_OK;
    *voltage_valid = ina_valid;
    if (ina_valid) {
        sample->vpack_mv = measurement.voltage_mv;
        sample->ipack_ma = measurement.current_ma;
        sample->power_mw = measurement.power_mw;
    }

    int16_t temperature = INT16_MIN;
    bool ntc_valid = ntc_read_centi_c(&temperature) == ESP_OK;
    sample->temp_centi_c = temperature;
    if (ina_valid && ntc_valid) sample->flags |= EMERGENCY_FLAG_SENSOR_VALID;
    sample->soh_pct = ina_valid ? estimate_soh(sample->vpack_mv, ac_present, thresholds) : 0;
    sample->reserved = 0;
    sample->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    if ((ina_valid && sample->vpack_mv < thresholds->alert_mv) ||
        (ntc_valid && sample->temp_centi_c >= APP_TEST_MAX_TEMPERATURE_CC)) {
        sample->flags |= EMERGENCY_FLAG_ALERT;
    }
    if (ina_valid && sample->vpack_mv < thresholds->critical_mv) sample->flags |= EMERGENCY_FLAG_UVLO;
    return ESP_OK;
}

static void telemetry_task(void *arg)
{
    while (true) {
        battery_config_snapshot_t config;
        battery_config_snapshot(&config);
        emergency_telemetry_v2_t frame = {.config_version = config.revision,
            .config_capable = 1, .boot_id = battery_config_boot_id()};
        emergency_telemetry_t sample = {0};
        bool voltage_valid;
        uint32_t delay_ms = APP_REPORT_NORMAL_MS;
        if (sample_telemetry(&sample, &voltage_valid, &config.thresholds) == ESP_OK) {
            battery_test_record(&sample, voltage_valid);
            ESP_LOGI(TAG, "V=%u mV I=%d mA T=%.2f C AC=%u SoH=%u%%", sample.vpack_mv, sample.ipack_ma,
                     sample.temp_centi_c / 100.0, !!(sample.flags & EMERGENCY_FLAG_AC_PRESENT), sample.soh_pct);
            frame.sample = sample;
            if (zigbee_node_is_joined()) zigbee_node_send_telemetry(&frame);
            if (battery_test_is_active()) delay_ms = APP_TEST_SAMPLE_MS;
            else if (sample.flags & EMERGENCY_FLAG_ALERT) delay_ms = APP_REPORT_ALERT_MS;
            else if (sample.vpack_mv < config.thresholds.attention_mv) delay_ms = APP_REPORT_ATTENTION_MS;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t telemetry_init(uint32_t device_id)
{
    s_device_id = device_id;
    return ESP_OK;
}

esp_err_t telemetry_start(void)
{
    return xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void telemetry_wake(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
