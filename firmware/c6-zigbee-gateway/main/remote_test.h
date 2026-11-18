#pragma once
#include "emergency_protocol.h"
#include "sdkconfig.h"
#include <stdbool.h>
#ifdef CONFIG_GATEWAY_ETH_ENABLED
#include "esp_err.h"
#include "mqtt_client.h"
esp_err_t remote_test_init(esp_mqtt_client_handle_t client, const char *boot);
void remote_test_mqtt(esp_mqtt_event_handle_t event);
void remote_test_report(const battery_test_report_t *report);
void remote_test_report_v2(const battery_test_report_v2_t *report);
bool remote_test_ready(void);
#else
static inline void remote_test_report(const battery_test_report_t *report) { (void)report; }
static inline void remote_test_report_v2(const battery_test_report_v2_t *report) { (void)report; }
#endif
