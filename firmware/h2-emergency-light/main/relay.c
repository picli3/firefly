#include "relay.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "relay";

static int relay_level(bool active)
{
    return active ? APP_RELAY_ACTIVE_LEVEL : !APP_RELAY_ACTIVE_LEVEL;
}

esp_err_t relay_init(void)
{
    gpio_config_t ac_cfg = {
        .pin_bit_mask = (1ULL << APP_AC_PRESENT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&ac_cfg), "relay", "AC GPIO config failed");

    int inactive_level = relay_level(false);
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_RELAY_CONTROL_GPIO, inactive_level), TAG,
                        "Relay initial level failed");
    gpio_config_t relay_cfg = {
        .pin_bit_mask = (1ULL << APP_RELAY_CONTROL_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&relay_cfg), TAG, "Relay GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_RELAY_CONTROL_GPIO, inactive_level), TAG,
                        "Relay inactive level failed");
    ESP_LOGI(TAG, "Relay initialized: GPIO%d active_level=%d requested_level=%d readback=%d",
             APP_RELAY_CONTROL_GPIO, APP_RELAY_ACTIVE_LEVEL, inactive_level,
             gpio_get_level(APP_RELAY_CONTROL_GPIO));

    gpio_config_t button_cfg = {
        .pin_bit_mask = (1ULL << APP_TEST_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&button_cfg);
}

bool relay_ac_present(void)
{
    return gpio_get_level(APP_AC_PRESENT_GPIO) != 0;
}

bool relay_test_button_pressed(void)
{
    return gpio_get_level(APP_TEST_BUTTON_GPIO) == 0;
}

void relay_set_test_active(bool active)
{
    int requested_level = relay_level(active);
    esp_err_t status = gpio_set_level(APP_RELAY_CONTROL_GPIO, requested_level);
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d: %s", APP_RELAY_CONTROL_GPIO, esp_err_to_name(status));
        return;
    }
    ESP_LOGI(TAG, "Relay test_active=%u requested_level=%d readback=%d", active,
             requested_level, gpio_get_level(APP_RELAY_CONTROL_GPIO));
}
