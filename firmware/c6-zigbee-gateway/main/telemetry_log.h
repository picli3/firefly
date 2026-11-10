#pragma once

#include <stdint.h>

#include "emergency_protocol.h"

void telemetry_log_sample(uint16_t short_addr, const emergency_telemetry_t *sample);
void telemetry_log_test_report(uint16_t short_addr, const battery_test_report_t *report);
const char *telemetry_log_test_result(uint8_t result);
