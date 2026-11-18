#pragma once

#include "emergency_protocol.h"
#include "sdkconfig.h"

#ifdef CONFIG_GATEWAY_ETH_ENABLED
#include "esp_err.h"
#include "mqtt_client.h"

/* Call once, before MQTT starts. boot must be unique per boot, 1..64 bytes.
 * Integration: add remote_config.c to SRCS; subscribe commands/config at QoS 1;
 * pass DATA events to remote_config_mqtt(); forwarding connection/error events
 * also resets partial RX. On CONNECTED call remote_config_reconnect() from the
 * same serialized MQTT callback (it resets RX and schedules ACK replay).
 * Route only validated H2
 * config-ACK frames here, after verifying the Zigbee source's device_id.
 * All entrypoints are task-context APIs (not ISR); MQTT events are serialized.
 */
esp_err_t remote_config_init(esp_mqtt_client_handle_t client, const char *boot);
void remote_config_mqtt(esp_mqtt_event_handle_t event);
void remote_config_ack(const battery_config_ack_t *ack);
void remote_config_reconnect(void);

/* ACKs: bounded RAM cache of the last 16 distinct H2 responses, FIFO eviction.
 * Reconnect replays cached ACKs with their original message_id. Queue failures
 * retry every second. Neither reboot nor eviction is durable; web retries must
 * reach the idempotent H2 again to recover its actual persisted result.
 * No gateway-generated applied/deferred/rejected responses, even on send error.
 * device_id uses the telemetry convention: eight lowercase hexadecimal digits.
 * command_id is the original 16 UUID octets formatted canonically in lowercase.
 */
#else
static inline void remote_config_ack(const battery_config_ack_t *ack) { (void)ack; }
#endif
