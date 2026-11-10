#include "ina226.h"

#include <stdbool.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#define INA226_REG_CONFIG        0x00
#define INA226_REG_BUS_VOLTAGE   0x02
#define INA226_REG_POWER         0x03
#define INA226_REG_CURRENT       0x04
#define INA226_REG_CALIBRATION   0x05
#define INA226_CONFIG_CONTINUOUS 0x4527

static const char *TAG = "ina226";
static i2c_master_dev_handle_t s_device;
static bool s_ready;
static uint8_t s_address = APP_INA226_ADDRESS;

static esp_err_t write_register(uint8_t reg, uint16_t value)
{
    uint8_t data[] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_transmit(s_device, data, sizeof(data), APP_I2C_OPERATION_TIMEOUT_MS);
}

static esp_err_t read_register(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_device, &reg, 1, data, sizeof(data),
                        APP_I2C_OPERATION_TIMEOUT_MS), TAG,
                        "INA226 read failed");
    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

esp_err_t ina226_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = APP_I2C_SDA_GPIO,
        .scl_io_num = APP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "I2C init failed");

    bool any_device_found = false;
    bool ina226_found = false;
    ESP_LOGI(TAG, "Scanning I2C bus on SDA=GPIO%d SCL=GPIO%d", APP_I2C_SDA_GPIO, APP_I2C_SCL_GPIO);
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        if (i2c_master_probe(bus, address, APP_I2C_PROBE_TIMEOUT_MS) == ESP_OK) {
            any_device_found = true;
            ESP_LOGI(TAG, "I2C device found at 0x%02x", address);
            if (!ina226_found && address >= 0x40 && address <= 0x4f) {
                s_address = address;
                ina226_found = true;
            }
        }
    }
    if (!any_device_found) {
        ESP_LOGW(TAG, "No I2C devices found; line levels SDA=%d SCL=%d", gpio_get_level(APP_I2C_SDA_GPIO),
                 gpio_get_level(APP_I2C_SCL_GPIO));
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_address,
        .scl_speed_hz = APP_I2C_FREQUENCY_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_device), TAG, "INA226 add failed");
    esp_err_t status = write_register(INA226_REG_CALIBRATION, APP_INA226_CALIBRATION);
    if (status == ESP_OK) status = write_register(INA226_REG_CONFIG, INA226_CONFIG_CONTINUOUS);
    if (status == ESP_OK) {
        s_ready = true;
        ESP_LOGI(TAG, "INA226 detected at address 0x%02x", s_address);
    } else {
        ESP_LOGW(TAG, "INA226 not detected in range 0x40-0x4f; continuing with invalid sensor telemetry");
    }

    gpio_config_t alert_cfg = {
        .pin_bit_mask = (1ULL << APP_INA226_ALERT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&alert_cfg);
}

esp_err_t ina226_read_measurement(ina226_measurement_t *measurement)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    uint16_t bus_raw;
    uint16_t current_raw;
    uint16_t power_raw;
    ESP_RETURN_ON_ERROR(read_register(INA226_REG_BUS_VOLTAGE, &bus_raw), TAG, "Bus voltage read failed");
    ESP_RETURN_ON_ERROR(read_register(INA226_REG_CURRENT, &current_raw), TAG, "Current read failed");
    ESP_RETURN_ON_ERROR(read_register(INA226_REG_POWER, &power_raw), TAG, "Power read failed");
    measurement->voltage_mv = (uint16_t)(((uint32_t)bus_raw * 5U) / 4U);
    measurement->current_ma = (int16_t)current_raw / 10;
    measurement->power_mw = (int32_t)power_raw * 5 / 2;
    return ESP_OK;
}
