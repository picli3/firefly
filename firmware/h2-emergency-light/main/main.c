#include <inttypes.h>
#include <stdint.h>

#include "battery_test.h"
#include "esp_crc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "ina226.h"
#include "ntc.h"
#include "nvs_flash.h"
#include "relay.h"
#include "telemetry.h"
#include "zigbee_node.h"

void app_main(void)
{
    uint8_t eui64[8];
    ESP_ERROR_CHECK(esp_read_mac(eui64, ESP_MAC_IEEE802154));
    // Keep the existing 32-bit payload field, using all eight address bytes.
    const uint32_t device_id = esp_crc32_le(0, eui64, sizeof(eui64));
    ESP_LOGI("emergency_node", "Device ID: %08" PRIx32, device_id);

    esp_err_t nvs_status = nvs_flash_init();
    if (nvs_status == ESP_ERR_NVS_NO_FREE_PAGES || nvs_status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_status);

    ESP_ERROR_CHECK(battery_test_init(device_id, zigbee_node_send_test_report, zigbee_node_is_joined,
                                      telemetry_wake));
    ESP_ERROR_CHECK(ina226_init());
    ESP_ERROR_CHECK(ntc_init());
    ESP_ERROR_CHECK(relay_init());
    ESP_ERROR_CHECK(telemetry_init(device_id));
    ESP_ERROR_CHECK(zigbee_node_init());

    ESP_ERROR_CHECK(zigbee_node_start());
    ESP_ERROR_CHECK(telemetry_start());
    ESP_ERROR_CHECK(battery_test_start_task());
}
