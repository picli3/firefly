#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define APP_I2C_SDA_GPIO             GPIO_NUM_4
#define APP_I2C_SCL_GPIO             GPIO_NUM_12
#define APP_INA226_ALERT_GPIO        GPIO_NUM_11
#define APP_AC_PRESENT_GPIO          GPIO_NUM_10
#define APP_RELAY_CONTROL_GPIO       GPIO_NUM_2
#define APP_RELAY_ACTIVE_LEVEL       1
#define APP_TEST_BUTTON_GPIO         GPIO_NUM_5
#define APP_NTC_ADC_CHANNEL          ADC_CHANNEL_0 /* GPIO1 */
#define APP_INA226_ADDRESS           0x40
#define APP_INA226_CALIBRATION       25600 /* R5: 2 mOhm shunt, current LSB = 100 uA */
#define APP_I2C_FREQUENCY_HZ         100000
#define APP_I2C_PROBE_TIMEOUT_MS     20
#define APP_I2C_OPERATION_TIMEOUT_MS 100

#define APP_REPORT_NORMAL_MS         60000
#define APP_REPORT_ATTENTION_MS      30000
#define APP_REPORT_ALERT_MS          10000
#define APP_ZIGBEE_MAX_CHILDREN      10
#define APP_LOCAL_TEST_DURATION_S    30
#define APP_MAX_TEST_DURATION_S      300
#define APP_TEST_SAMPLE_MS           5000
#define APP_TEST_MIN_VOLTAGE_MV      5250
#define APP_TEST_MIN_CURRENT_MA      50
#define APP_TEST_MAX_TEMPERATURE_CC  4000
#define APP_ALERT_VOLTAGE_MV         5500
#define APP_UVLO_VOLTAGE_MV          5250
#define APP_ATTENTION_VOLTAGE_MV     5850
#define APP_REPORT_RETRY_US          30000000LL
#define APP_TEST_TASK_POLL_MS        50
#define APP_COMMISSIONING_RETRY_MS   1000

#define APP_SOH_FULL_VOLTAGE_MV      6300
#define APP_SOH_AC_EMPTY_VOLTAGE_MV  5500
#define APP_SOH_AC_RANGE_MV          800
#define APP_SOH_DC_EMPTY_VOLTAGE_MV  5250
#define APP_SOH_DC_RANGE_MV          1050

#define APP_NTC_SUPPLY_MV            3300
#define APP_NTC_FIXED_RESISTANCE_OHM 10000.0f
#define APP_NTC_NOMINAL_OHM          10000.0f
#define APP_NTC_NOMINAL_KELVIN       298.15f
#define APP_NTC_BETA                 3950.0f
#define APP_CELSIUS_ZERO_KELVIN      273.15f

#define APP_ZIGBEE_CHANNEL_MASK      (1UL << 15)
