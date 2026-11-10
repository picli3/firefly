#include "esp_log.h"
#include "node_registry.h"
#include "nvs_flash.h"
#include "test_campaign.h"
#include "zigbee_gateway.h"
#include "telemetry_csv.h"
#include "csv_serial.h"

void app_main(void)
{
    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(status);
    ESP_ERROR_CHECK(node_registry_init());
    esp_err_t csv_status = telemetry_csv_init();
    if (csv_status != ESP_OK) {
        ESP_LOGE("gateway", "CSV storage unavailable: %s; Zigbee will continue", esp_err_to_name(csv_status));
    }
    ESP_ERROR_CHECK(csv_serial_start());
    ESP_ERROR_CHECK(test_campaign_init(zigbee_gateway_send_schedule, zigbee_gateway_is_ready));
    ESP_ERROR_CHECK(zigbee_gateway_start());
}
