#include "ntc.h"

#include <math.h>
#include <stdbool.h>

#include "app_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ntc";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_calibrated;

esp_err_t ntc_init(void)
{
    adc_oneshot_unit_init_cfg_t adc_cfg = {.unit_id = ADC_UNIT_1};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_adc), TAG, "ADC init failed");
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, APP_NTC_ADC_CHANNEL, &chan_cfg), TAG,
                        "ADC channel failed");

    adc_cali_curve_fitting_config_t cal_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = APP_NTC_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_calibrated = adc_cali_create_scheme_curve_fitting(&cal_cfg, &s_adc_cali) == ESP_OK;
    return ESP_OK;
}

esp_err_t ntc_read_centi_c(int16_t *temperature)
{
    int raw;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, APP_NTC_ADC_CHANNEL, &raw), TAG, "NTC ADC read failed");
    int mv = 0;
    if (!s_calibrated || adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) != ESP_OK ||
        mv <= 0 || mv >= APP_NTC_SUPPLY_MV) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Divider: 3V3 -> 10K fixed -> ADC -> NTC 10K -> GND. */
    float resistance = APP_NTC_FIXED_RESISTANCE_OHM * (float)mv / (APP_NTC_SUPPLY_MV - (float)mv);
    float kelvin = 1.0f / ((1.0f / APP_NTC_NOMINAL_KELVIN) +
                          logf(resistance / APP_NTC_NOMINAL_OHM) / APP_NTC_BETA);
    *temperature = (int16_t)lroundf((kelvin - APP_CELSIUS_ZERO_KELVIN) * 100.0f);
    return ESP_OK;
}
