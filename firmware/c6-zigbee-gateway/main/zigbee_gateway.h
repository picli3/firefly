#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emergency_protocol.h"
#include "esp_err.h"

esp_err_t zigbee_gateway_start(void);
bool zigbee_gateway_is_ready(void);
esp_err_t zigbee_gateway_log_links(void);
esp_err_t zigbee_gateway_query_links(uint16_t short_addr, uint8_t start_index);
esp_err_t zigbee_gateway_start_manual_test(uint32_t device_id);
esp_err_t zigbee_gateway_repeat_start(uint32_t device_id);
esp_err_t zigbee_gateway_repeat_stop(void);
esp_err_t zigbee_gateway_repeat_status(char *buffer, size_t capacity);
uint8_t zigbee_gateway_send_schedule(uint16_t short_addr, const battery_test_schedule_t *schedule);
