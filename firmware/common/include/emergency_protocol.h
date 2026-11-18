#pragma once

#include <stdint.h>

#define EMERGENCY_ENDPOINT                1
#define EMERGENCY_CLUSTER_ID              0xFC00
#define EMERGENCY_CMD_TELEMETRY           0x01
#define EMERGENCY_CMD_START_TEST          0x02
#define EMERGENCY_CMD_SCHEDULE_TEST       0x03
#define EMERGENCY_CMD_TEST_REPORT         0x04
#define EMERGENCY_CMD_BATTERY_CONFIG      0x05
#define EMERGENCY_CMD_BATTERY_CONFIG_ACK  0x06
#define EMERGENCY_TELEMETRY_VERSION       1
#define EMERGENCY_TEST_PROTOCOL_VERSION   1
#define EMERGENCY_TELEMETRY_VERSION_V2    2
#define EMERGENCY_TEST_PROTOCOL_VERSION_V2 2
#define EMERGENCY_CONFIG_VERSION          1

#define EMERGENCY_FLAG_AC_PRESENT         (1U << 0)
#define EMERGENCY_FLAG_ALERT              (1U << 1)
#define EMERGENCY_FLAG_UVLO               (1U << 2)
#define EMERGENCY_FLAG_SENSOR_VALID       (1U << 3)
#define EMERGENCY_FLAG_SELF_TEST          (1U << 4)

typedef struct __attribute__((packed)) {
    uint8_t payload_version;
    uint8_t flags;
    uint16_t sequence;
    uint32_t device_id;
    uint16_t vpack_mv;
    int16_t ipack_ma;
    int32_t power_mw;
    int16_t temp_centi_c;
    uint8_t soh_pct;
    uint8_t reserved;
    uint32_t uptime_s;
} emergency_telemetry_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t group_index;
    uint16_t test_id;
    uint32_t start_delay_s;
    uint16_t duration_s;
    uint16_t reserved;
} battery_test_schedule_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t result;
    uint16_t test_id;
    uint32_t device_id;
    uint16_t voltage_before_mv;
    uint16_t voltage_min_mv;
    int16_t current_avg_ma;
    int16_t current_peak_ma;
    int32_t power_avg_mw;
    int16_t temperature_max_centi_c;
    uint16_t duration_s;
    uint8_t sample_count;
    uint8_t flags;
} battery_test_report_t;

/* v1 frames and their reserved bytes retain their original interpretation. */
typedef struct __attribute__((packed)) {
    emergency_telemetry_t sample;
    uint32_t config_version; /* 0: inherited, unconfirmed defaults */
    uint8_t config_capable;
    uint32_t boot_id;
} emergency_telemetry_v2_t;

typedef struct __attribute__((packed)) {
    battery_test_report_t report;
    uint32_t config_version; /* snapshot taken when the test starts */
    uint8_t config_capable;
    uint32_t boot_id;
} battery_test_report_v2_t;

typedef struct __attribute__((packed)) {
    uint16_t attention_mv, alert_mv, critical_mv, test_min_mv;
    uint16_t full_mv, ac_empty_mv, dc_empty_mv;
} battery_thresholds_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t command_uuid[16]; /* RFC4122 octet order */
    uint32_t revision;
    battery_thresholds_t thresholds;
} battery_config_request_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t command_uuid[16];
    uint32_t device_id;
    uint32_t revision;
    uint8_t status;
    uint8_t reason;
} battery_config_ack_t;

enum { BATTERY_CONFIG_APPLIED, BATTERY_CONFIG_DEFERRED, BATTERY_CONFIG_REJECTED };
enum {
    BATTERY_CONFIG_REASON_NONE,
    BATTERY_CONFIG_REASON_INVALID,
    BATTERY_CONFIG_REASON_STALE,
    BATTERY_CONFIG_REASON_CONFLICT,
    BATTERY_CONFIG_REASON_STORAGE,
};

static inline int battery_thresholds_valid(const battery_thresholds_t *t)
{
    return t->critical_mv && t->alert_mv && t->attention_mv && t->test_min_mv &&
           t->full_mv && t->ac_empty_mv && t->dc_empty_mv &&
           t->critical_mv <= t->alert_mv && t->alert_mv < t->attention_mv &&
           t->attention_mv <= t->full_mv && t->ac_empty_mv < t->full_mv &&
           t->dc_empty_mv < t->full_mv && t->test_min_mv <= t->full_mv;
}

typedef enum {
    BATTERY_TEST_PASS,
    BATTERY_TEST_LOW_VOLTAGE,
    BATTERY_TEST_LOW_CURRENT,
    BATTERY_TEST_HIGH_TEMPERATURE,
    BATTERY_TEST_AC_NOT_PRESENT,
    BATTERY_TEST_SENSOR_INVALID,
    BATTERY_TEST_ABORTED,
} battery_test_result_t;
