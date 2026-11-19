#pragma once

#define APP_ZIGBEE_CHANNEL          15
#define APP_ZIGBEE_CHANNEL_MASK     (1UL << APP_ZIGBEE_CHANNEL)
#define APP_ZIGBEE_MAX_CHILDREN     50
#define APP_NETWORK_OPEN_SECONDS    180
#define APP_MAX_TRACKED_NODES       50
#define APP_COMMAND_SPACING_MS      50
#define APP_CAMPAIGN_RETRY_MS       5000
/* Remote (MQTT) battery tests: accepted duration range. The H2 node limit is
 * APP_MAX_TEST_DURATION_S = 300 s; keep both in sync. */
#define APP_REMOTE_TEST_MIN_S       30
#define APP_REMOTE_TEST_MAX_S       300
/* Margin after the test for the node report to arrive over Zigbee. */
#define APP_REMOTE_TEST_MARGIN_S    150
