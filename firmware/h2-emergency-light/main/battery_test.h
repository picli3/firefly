#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "emergency_protocol.h"
#include "esp_err.h"

typedef esp_err_t (*battery_test_report_sender_t)(const battery_test_report_t *report);
typedef void (*battery_test_wake_callback_t)(void);
typedef bool (*battery_test_transport_ready_t)(void);

esp_err_t battery_test_init(uint32_t device_id, battery_test_report_sender_t report_sender,
                            battery_test_transport_ready_t transport_ready,
                            battery_test_wake_callback_t wake_telemetry);
esp_err_t battery_test_start_task(void);
esp_err_t battery_test_start(uint16_t duration_s, uint16_t test_id);
esp_err_t battery_test_schedule(const battery_test_schedule_t *schedule);
bool battery_test_is_active(void);
void battery_test_record(const emergency_telemetry_t *sample, bool voltage_valid);
