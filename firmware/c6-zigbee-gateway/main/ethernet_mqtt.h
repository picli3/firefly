#pragma once
#include <stddef.h>

#include "esp_err.h"
#include "emergency_protocol.h"

esp_err_t ethernet_mqtt_start(void);
void ethernet_mqtt_telemetry(uint16_t short_addr, const emergency_telemetry_t *sample);
void ethernet_mqtt_telemetry_v2(uint16_t short_addr, const emergency_telemetry_v2_t *sample);
void ethernet_mqtt_status(char *out, size_t size);
