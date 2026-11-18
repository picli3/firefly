#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define GATEWAY_CONFIG_MAX_JSON 8192
#define GATEWAY_CONFIG_MAX_NTP 3
typedef struct {
    char gateway_id[65], uri[300], host[254], client_id[129];
    char username[65], password[257], ca_pem[4097];
    char ntp[GATEWAY_CONFIG_MAX_NTP][254];
    unsigned ntp_count, port, keepalive_s, heartbeat_s;
    bool tls;
} gateway_settings_t;

/* Active settings are immutable until reboot; committed updates never change
 * pointers/topics belonging to the currently running MQTT client. */
esp_err_t gateway_settings_init(void);
const gateway_settings_t *gateway_settings_active(void);
esp_err_t gateway_settings_commit(const char *json, size_t length);
esp_err_t gateway_settings_summary(char *out, size_t size);
