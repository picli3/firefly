#pragma once

#include <stdbool.h>

#include "emergency_protocol.h"
#include "esp_err.h"

esp_err_t zigbee_node_init(void);
esp_err_t zigbee_node_start(void);
bool zigbee_node_is_joined(void);
esp_err_t zigbee_node_send_telemetry(const emergency_telemetry_v2_t *sample);
esp_err_t zigbee_node_send_test_report(const battery_test_report_v2_t *report);
esp_err_t zigbee_node_send_config_ack(const battery_config_ack_t *ack);
