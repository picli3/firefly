#include "esp_log.h"
#include "node_registry.h"
#include "nvs_flash.h"
#include "test_campaign.h"
#include "zigbee_gateway.h"
#include "telemetry_csv.h"
#include "csv_serial.h"
#include "ethernet_mqtt.h"
#include "gateway_settings.h"

void app_main(void)
{
    esp_err_t status = nvs_flash_init();
    /* Never silently erase provisioning, Zigbee state or test deduplication. */
    ESP_ERROR_CHECK(status);
    esp_err_t provision_status = gateway_settings_init();
    if (provision_status != ESP_OK)
        ESP_LOGE("gateway", "Saved network settings unavailable; recover using AT provisioning (%s)", esp_err_to_name(provision_status));
    ESP_ERROR_CHECK(node_registry_init());
    esp_err_t csv_status = telemetry_csv_init();
    if (csv_status != ESP_OK) {
        ESP_LOGE("gateway", "CSV storage unavailable: %s; Zigbee will continue", esp_err_to_name(csv_status));
    }
    ESP_ERROR_CHECK(csv_serial_start());
    ESP_ERROR_CHECK(test_campaign_init(zigbee_gateway_send_schedule, zigbee_gateway_is_ready));
    esp_err_t network_status = ethernet_mqtt_start();
    if (network_status != ESP_OK) {
        ESP_LOGE("gateway", "Ethernet unavailable: %s; Zigbee/CSV continue", esp_err_to_name(network_status));
    }
    ESP_ERROR_CHECK(zigbee_gateway_start());
}
