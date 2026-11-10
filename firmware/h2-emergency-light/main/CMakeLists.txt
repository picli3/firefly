idf_component_register(SRCS "main.c"
                            "ina226.c"
                            "ntc.c"
                            "relay.c"
                            "battery_test.c"
                            "telemetry.c"
                            "zigbee_node.c"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_driver_gpio esp_driver_i2c esp_adc esp_hw_support esp_timer nvs_flash)
