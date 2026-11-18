idf_component_register(SRCS "main.c"
                            "ethernet_mqtt.c"
                            "gateway_settings.c"
                            "at_provisioning.c"
                            "remote_test.c"
                            "remote_config.c"
                            "node_registry.c"
                            "telemetry_log.c"
                            "telemetry_csv.c"
                            "csv_serial.c"
                            "csv_format.c"
                            "test_campaign.c"
                            "zigbee_gateway.c"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_timer nvs_flash spiffs esp_partition esp_driver_usb_serial_jtag
                                esp_eth esp_netif esp_event esp_driver_spi esp_driver_gpio esp_hw_support esp_system mqtt json mbedtls)

if(CONFIG_GATEWAY_ETH_ENABLED AND CONFIG_GATEWAY_MQTT_TLS)
    if(NOT CONFIG_GATEWAY_MQTT_CA_FILE OR NOT EXISTS "${CONFIG_GATEWAY_MQTT_CA_FILE}")
        message(FATAL_ERROR "MQTT TLS requires an existing GATEWAY_MQTT_CA_FILE (public PEM)")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${CONFIG_GATEWAY_MQTT_CA_FILE}" TEXT RENAME_TO gateway_mqtt_ca)
endif()
