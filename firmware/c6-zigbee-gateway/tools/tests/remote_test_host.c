// Exercises the actual firmware parser and persistence ordering with host ports.
#include "../../main/remote_test.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
const gateway_settings_t *gateway_settings_active(void) {
    static const gateway_settings_t cfg = {.gateway_id = "host-test"};
    return &cfg;
}
static record_t durable, staging;
static int64_t durable_highwater, staging_highwater;
static int starts, commits, fail_commit, queued, last_duration;
static esp_err_t send_result;
static char last_json[1800];
const char *esp_err_to_name(esp_err_t e) { return e ? "ESP_FAIL" : "ESP_OK"; }
int esp_mqtt_client_enqueue(void *c, const char *t, const char *d, int len, int q, int r,
                            bool store) {
    (void)c;
    (void)t;
    (void)len;
    (void)q;
    (void)r;
    (void)store;
    snprintf(last_json, sizeof(last_json), "%s", d);
    return 1;
}
QueueHandle_t xQueueCreate(unsigned n, unsigned size) {
    (void)n;
    (void)size;
    return (void *)1;
}
void vQueueDelete(QueueHandle_t q) { (void)q; }
int xQueueSend(QueueHandle_t q, const void *v, unsigned t) {
    (void)q;
    (void)v;
    (void)t;
    queued++;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *v, unsigned t) {
    (void)q;
    (void)v;
    (void)t;
    return 0;
}
int xTaskCreate(void (*fn)(void *), const char *n, unsigned s, void *a, unsigned p, void *h) {
    (void)fn;
    (void)n;
    (void)s;
    (void)a;
    (void)p;
    (void)h;
    return pdPASS;
}
esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) {
    (void)n;
    (void)m;
    *h = 1;
    staging = durable;
    staging_highwater = durable_highwater;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t n) {
    (void)h;
    (void)k;
    assert(n == sizeof(record_t));
    memcpy(&staging, v, n);
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *n) {
    (void)h;
    (void)k;
    if (!durable.version)
        return ESP_FAIL;
    size_t size = durable.version == 1 ? sizeof(record_v1_t) : sizeof(durable);
    assert(*n >= size);
    memcpy(v, &durable, size);
    *n = size;
    return ESP_OK;
}
esp_err_t nvs_set_i64(nvs_handle_t h, const char *k, int64_t n) {
    (void)h;
    (void)k;
    staging_highwater = n;
    return ESP_OK;
}
esp_err_t nvs_get_i64(nvs_handle_t h, const char *k, int64_t *n) {
    (void)h;
    (void)k;
    *n = durable_highwater;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    if (++commits == fail_commit)
        return ESP_FAIL;
    durable = staging;
    durable_highwater = staging_highwater;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t zigbee_gateway_start_remote_test(uint32_t device, uint16_t test_id, uint16_t duration_s) {
    assert(durable.state == 1);
    assert(durable.report.duration_s == duration_s);
    last_duration = duration_s;
    assert(durable.device == device);
    assert(durable.test_id == test_id);
    assert(durable_highwater == durable.issued_ms);
    starts++;
    return send_result;
}
bool test_campaign_reserve_remote(uint16_t s) {
    (void)s;
    return true;
}
void test_campaign_remote_done(void) {}
const char *telemetry_log_test_result(uint8_t r) {
    (void)r;
    return "PASS";
}
static void reset(void) {
    memset(&durable, 0, sizeof(durable));
    memset(&s_record, 0, sizeof(s_record));
    durable_highwater = s_highwater = 0;
    starts = commits = fail_commit = queued = 0;
    send_result = ESP_OK;
    assert(remote_test_init((void *)1, "host-boot") == ESP_OK);
}
static void make_d(char *out, size_t n, int id, int64_t issued, int64_t expiry, const char *duration) {
    snprintf(out, n,
             "{\"schema_version\":1,\"command_id\":\"00000000-0000-4000-8000-%012d\",\"device_id\":"
             "\"abcdef01\",\"test_id\":%d,\"duration_s\":%s,\"issued_at_ms\":%" PRId64
             ",\"expires_at_ms\":%" PRId64 "}",
             id, 32768 + id, duration, issued, expiry);
}
static void make(char *out, size_t n, int id, int64_t issued, int64_t expiry) {
    make_d(out, n, id, issued, expiry, "30");
}
static void status(const char *expected) {
    cJSON *j = cJSON_Parse(last_json);
    assert(j);
    assert(!strcmp(cJSON_GetObjectItem(j, "status")->valuestring, expected));
    cJSON_Delete(j);
}
int main(void) {
    char a[512], b[512];
    int64_t now = (int64_t)time(NULL) * 1000;
    reset();
    make(a, sizeof(a), 1, now - 1000, now + 29000);
    command(a);
    assert(starts == 1);
    status("accepted");
    command(a);
    assert(starts == 1);
    status("accepted");
    // A rejected busy order remains rejected on duplicate after the first finishes.
    make(b, sizeof(b), 2, now - 900, now + 29000);
    command(b);
    assert(starts == 1);
    status("rejected");
    s_record.state = 4;
    command(b);
    assert(starts == 1);
    status("rejected");
    make(b, sizeof(b), 3, now - 2000, now - 1000);
    command(b);
    assert(starts == 1);
    status("rejected");
    reset();
    fail_commit = 2;
    command(a);
    assert(starts == 0);
    status("rejected");
    assert(durable_highwater == now - 1000);
    fail_commit = 0;
    s_highwater = 0;
    memset(&s_record, 0, sizeof(s_record));
    remote_test_init((void *)1, "reboot");
    command(a);
    assert(starts == 0);
    status("rejected");
    // Power loss after Zigbee send and before the final commit leaves an uncertain
    // durable record. Reboot+duplicate acknowledges uncertainty without execution.
    reset();
    fail_commit = 3;
    command(a);
    assert(starts == 1);
    assert(durable.state == 1);
    fail_commit = 0;
    s_highwater = 0;
    memset(&s_record, 0, sizeof(s_record));
    remote_test_init((void *)1, "reboot");
    command(a);
    assert(starts == 1);
    status("unknown");
    reset();
    send_result = ESP_FAIL;
    command(a);
    assert(starts == 1);
    status("rejected");
    command(a);
    assert(starts == 1);
    reset();
    char topic[] = "firefly/v1/gateways/host-test/commands/test";
    host_mqtt_event_t event = {.topic = topic,
                               .topic_len = (int)strlen(topic),
                               .data = a,
                               .data_len = (int)strlen(a),
                               .total_data_len = (int)strlen(a),
                               .retain = true};
    remote_test_mqtt(&event);
    assert(queued == 0);
    event.retain = false;
    event.data_len = 40;
    remote_test_mqtt(&event);
    assert(queued == 0);
    event.current_data_offset = 40;
    event.data = a + 40;
    event.data_len = event.total_data_len - 40;
    event.topic = NULL;
    event.topic_len = 0;
    remote_test_mqtt(&event);
    assert(queued == 1);
    event.current_data_offset = 0;
    event.total_data_len = RX_MAX + 1;
    remote_test_mqtt(&event);
    assert(queued == 1);
    // Upgrade keeps the old uncertain request: it must never trigger another test.
    reset(); command(a); assert(starts == 1);
    durable.version = 1; durable.state = 1;
    memset(&s_record, 0, sizeof(s_record));
    remote_test_init((void *)1, "upgrade");
    assert(s_record.version == RECORD_VERSION && !s_record.has_meta);
    command(a); assert(starts == 1); status("unknown");
    // A result carries its H2 revision, even when replayed after a gateway reboot.
    s_record.state = 4;
    s_record.report = (battery_test_report_t){.version = 2, .device_id = 0xabcdef01,
        .test_id = 32768, .duration_s = 30};
    s_record.has_meta = true; s_record.config_version = 7; s_record.node_boot = 42;
    assert(store_record());
    memset(&s_record, 0, sizeof(s_record));
    remote_test_init((void *)1, "after-result");
    assert(s_record.has_meta && s_record.config_version == 7 && s_record.node_boot == 42);
    command(a); assert(starts == 1);
    cJSON *result = cJSON_Parse(last_json);
    assert(cJSON_GetObjectItem(result, "config_version")->valuedouble == 7);
    assert(!strcmp(cJSON_GetObjectItem(result, "boot_id")->valuestring, "h2-abcdef01-0000002a"));
    cJSON_Delete(result);
    publish_report(&s_record.report, NULL, true, false, 0, 0);
    result = cJSON_Parse(last_json);
    assert(!cJSON_GetObjectItem(result, "config_version"));
    cJSON_Delete(result);
    // Duration range: 30..300 s accepted and forwarded; anything else rejected unsent.
    const char *ok_durations[] = {"30", "60", "300"};
    for (int i = 0; i < 3; i++) {
        reset();
        make_d(b, sizeof(b), 10 + i, now - 1000, now + 29000, ok_durations[i]);
        command(b);
        assert(starts == 1);
        assert(last_duration == atoi(ok_durations[i]));
        status("accepted");
    }
    const char *bad_durations[] = {"29", "301", "45.5", "0", "-30"};
    for (int i = 0; i < 5; i++) {
        reset();
        make_d(b, sizeof(b), 20 + i, now - 1000, now + 29000, bad_durations[i]);
        last_json[0] = 0;
        command(b);
        assert(starts == 0);
        assert(last_json[0] == 0); /* invalid commands are dropped without ack */
    }
    // Busy window follows the pending test duration (+150 s margin).
    reset();
    make_d(a, sizeof(a), 30, now - 1000, now + 29000, "300");
    command(a);
    assert(starts == 1);
    s_record.issued_ms = now - 400000; /* 400 s ago: still inside 300+150 */
    make(b, sizeof(b), 31, now - 500, now + 29000);
    command(b);
    assert(starts == 1);
    status("rejected");
    s_record.issued_ms = now - 460000; /* 460 s ago: window over */
    make(b, sizeof(b), 32, now - 400, now + 29000);
    command(b);
    assert(starts == 2);
    status("accepted");
    // Records from older firmware (duration 0 in the pending report) keep the 180 s window.
    reset();
    command(a);
    s_record.report.duration_s = 0;
    s_record.issued_ms = now - 170000;
    make(b, sizeof(b), 33, now - 300, now + 29000);
    command(b);
    assert(starts == 1);
    status("rejected");
    s_record.issued_ms = now - 190000;
    make(b, sizeof(b), 34, now - 200, now + 29000);
    command(b);
    assert(starts == 2);
    puts("remote_test: persistence, replay, expiry, MQTT framing, v1 upgrade, result revision and duration range passed");
    return 0;
}
