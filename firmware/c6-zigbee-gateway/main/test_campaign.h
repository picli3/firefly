#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "emergency_protocol.h"
#include "esp_err.h"

typedef uint8_t (*test_schedule_sender_t)(uint16_t short_addr, const battery_test_schedule_t *schedule);
typedef bool (*zigbee_ready_provider_t)(void);

esp_err_t test_campaign_init(test_schedule_sender_t sender, zigbee_ready_provider_t ready_provider);
void test_campaign_process_report(const battery_test_report_t *report);
