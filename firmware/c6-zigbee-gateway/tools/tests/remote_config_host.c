/* Exercise actual firmware code, with queues driven deterministically by host. */
#include "../../main/remote_config.c"
#include <assert.h>
#include "gateway_settings.h"
const gateway_settings_t *gateway_settings_active(void) {
    static const gateway_settings_t cfg = {.gateway_id = "host-test"};
    return &cfg;
}
#include <stdlib.h>

static config_job_t jobs[CONFIG_COMMAND_QUEUE];
static unsigned job_count, sends, publications;
static bool queue_fail, task_fail, send_fail, enqueue_fail, reconnect_during_enqueue;
static battery_config_request_t sent;
static uint32_t sent_node;
static char last_json[1024];
static const char valid[] =
    "{\"schema_version\":1,\"command_id\":\"01234567-89ab-4cde-8f01-23456789abcd\","
    "\"device_id\":\"abcdef01\",\"version\":42,\"thresholds\":{\"attention_mv\":12000,"
    "\"alert_mv\":11000,\"critical_mv\":10000,\"test_min_mv\":10500,\"full_mv\":13500,"
    "\"ac_empty_mv\":9000,\"dc_empty_mv\":8500}}";

QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    assert(count == CONFIG_COMMAND_QUEUE && size == sizeof(config_job_t));
    return queue_fail ? NULL : (void *)1;
}
void vQueueDelete(QueueHandle_t q) { assert(q); }
int xQueueSend(QueueHandle_t q, const void *p, unsigned ticks) {
    assert(q && ticks == 0);
    if (job_count == CONFIG_COMMAND_QUEUE) return 0;
    jobs[job_count++] = *(const config_job_t *)p;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *p, unsigned ticks) {
    (void)q; (void)p; (void)ticks;
    return 0;
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, void *handle) {
    assert(fn == config_task && stack >= 4096);
    (void)name; (void)arg; (void)priority; (void)handle;
    return !task_fail;
}
esp_err_t zigbee_gateway_send_battery_config(uint32_t node, const battery_config_request_t *request) {
    ++sends;
    sent = *request;
    sent_node = node;
    return send_fail ? ESP_FAIL : ESP_OK;
}
int esp_mqtt_client_enqueue(void *client, const char *topic, const char *payload, int len,
                            int qos, int retain, bool store) {
    assert(client && !strcmp(topic, "firefly/v1/gateways/host-test/config-ack"));
    assert(len == 0 && qos == 1 && retain == 0 && store);
    if (enqueue_fail) return -1;
    ++publications;
    snprintf(last_json, sizeof(last_json), "%s", payload);
    if (reconnect_during_enqueue) {
        reconnect_during_enqueue = false;
        remote_config_reconnect();
    }
    return (int)publications;
}
static void reset(void) {
    s_jobs = NULL;
    memset(s_acks, 0, sizeof(s_acks));
    memset(&s_rx, 0, sizeof(s_rx));
    s_next_ack = 0;
    s_sequence = 0;
    sends = publications = job_count = 0;
    queue_fail = task_fail = send_fail = enqueue_fail = reconnect_during_enqueue = false;
    last_json[0] = 0;
    assert(remote_config_init((void *)1, "host-boot") == ESP_OK);
}
static void drain(void) {
    for (unsigned i = 0; i < job_count; ++i) config_command(&jobs[i]);
    job_count = 0;
    flush_acks();
}
static host_mqtt_event_t event_for(const char *payload, int n) {
    return (host_mqtt_event_t){.event_id = MQTT_EVENT_DATA, .msg_id = 7,
        .topic = s_command_topic, .topic_len = (int)strlen(s_command_topic),
        .data = (char *)payload, .data_len = n, .total_data_len = n};
}
static void deliver(const char *payload) {
    host_mqtt_event_t e = event_for(payload, (int)strlen(payload));
    unsigned before = sends;
    remote_config_mqtt(&e);
    assert(sends == before); /* No Zigbee/JSON processing in MQTT callback. */
    drain();
}
static void replace(char *out, const char *from, const char *to) {
    const char *p = strstr(valid, from);
    assert(p);
    size_t prefix = (size_t)(p - valid);
    memcpy(out, valid, prefix);
    strcpy(out + prefix, to);
    strcat(out, p + strlen(from));
}
static void invalid_replace(const char *from, const char *to) {
    char buffer[1025];
    replace(buffer, from, to);
    unsigned before = sends;
    deliver(buffer);
    assert(sends == before && publications == 0);
}
static battery_config_ack_t make_ack(void) {
    battery_config_ack_t a = {.version = EMERGENCY_CONFIG_VERSION, .device_id = 0xabcdef01,
        .revision = 42, .status = BATTERY_CONFIG_APPLIED, .reason = BATTERY_CONFIG_REASON_NONE};
    assert(parse_uuid("01234567-89ab-4cde-8f01-23456789abcd", a.command_uuid));
    return a;
}
static void parser_tests(void) {
    reset();
    deliver(valid);
    assert(sends == 1 && publications == 0 && sent_node == 0xabcdef01);
    assert(sent.version == EMERGENCY_CONFIG_VERSION && sent.revision == 42);
    assert(sent.command_uuid[0] == 0x01 && sent.command_uuid[15] == 0xcd);
    assert(sent.thresholds.attention_mv == 12000 && sent.thresholds.dc_empty_mv == 8500);
    deliver(valid);
    assert(sends == 2); /* Repeated commands reach the idempotent H2. */
    send_fail = true;
    deliver(valid);
    assert(sends == 3 && publications == 0); /* No invented rejection/applied. */
    send_fail = false;
    invalid_replace("\"version\":42", "\"version\":0");
    invalid_replace("\"version\":42", "\"version\":4294967296");
    invalid_replace("\"version\":42", "\"version\":1.5");
    invalid_replace("\"version\":42", "\"version\":1e999");
    invalid_replace("\"version\":42", "\"version\":\"42\"");
    invalid_replace("\"version\":42", "\"version\":true");
    invalid_replace("\"version\":42", "\"version\":null");
    invalid_replace("\"version\":42", "\"version\":042");
    invalid_replace("\"version\":42", "\"version\":42.");
    invalid_replace("\"version\":42", "\"version\":+42");
    invalid_replace("\"version\":42", "\"version\":.42");
    invalid_replace("\"schema_version\":1", "\"schema_version\":2");
    invalid_replace("\"version\":42", "\"version\":42,\"version\":43");
    invalid_replace("\"version\":42", "\"version\":42,\"ver\\u0073ion\":42");
    invalid_replace("\"version\":42", "\"version\":42,\"extra\":1");
    invalid_replace("\"version\":42,", "");
    invalid_replace("\"attention_mv\":12000", "\"attention_mv\":12000,\"attention_mv\":12000");
    invalid_replace("\"attention_mv\":12000", "\"attention_mv\":12000,\"extra\":1");
    invalid_replace("\"dc_empty_mv\":8500", "\"dc_empty_mv\":0");
    invalid_replace("\"dc_empty_mv\":8500", "\"dc_empty_mv\":65536");
    invalid_replace("\"dc_empty_mv\":8500", "\"dc_empty_mv\":-1");
    invalid_replace("\"dc_empty_mv\":8500", "\"dc_empty_mv\":8500.5");
    invalid_replace("\"critical_mv\":10000", "\"critical_mv\":11001");
    invalid_replace("\"alert_mv\":11000", "\"alert_mv\":12000");
    invalid_replace("\"attention_mv\":12000", "\"attention_mv\":13501");
    invalid_replace("\"test_min_mv\":10500", "\"test_min_mv\":13501");
    invalid_replace("\"ac_empty_mv\":9000", "\"ac_empty_mv\":13500");
    invalid_replace("\"dc_empty_mv\":8500", "\"dc_empty_mv\":13500");
    invalid_replace("abcdef01", "abcdef0g");
    invalid_replace("abcdef01", "abcdef0");
    invalid_replace("abcdef01", "abcdef01\\u0000junk");
    invalid_replace("\"version\"", "\"version\\u0000ignored\"");
    invalid_replace("89ab-4cde-8f01", "89ab-0cde-8f01");
    invalid_replace("89ab-4cde-8f01", "89ab-4cde-7f01");
    invalid_replace("01234567-89ab", "01234567_89ab");
    invalid_replace("\"thresholds\":{", "\"thresholds\":[");
    char buffer[1025];
    snprintf(buffer, sizeof(buffer), "%s garbage", valid);
    unsigned before = sends;
    deliver(buffer);
    snprintf(buffer, sizeof(buffer), "%s{}", valid);
    deliver(buffer);
    snprintf(buffer, sizeof(buffer), "%s\v", valid);
    deliver(buffer);
    assert(sends == before);
    strcpy(buffer, valid);
    size_t n = strlen(valid);
    buffer[n] = 0; memcpy(buffer + n + 1, "{}", 2);
    host_mqtt_event_t e = event_for(buffer, (int)n + 3);
    remote_config_mqtt(&e); drain();
    assert(sends == before);
    replace(buffer, "\"version\":42", "\"version\":4294967295");
    deliver(buffer);
    assert(sent.revision == UINT32_MAX && sends == before + 1);
    replace(buffer, "\"version\":42", "\"version\":42e0");
    deliver(buffer);
    assert(sent.revision == 42 && sends == before + 2);
    snprintf(buffer, sizeof(buffer), " \r\n%s\t ", valid);
    deliver(buffer);
    assert(sends == before + 3);
}
static void framing_tests(void) {
    reset();
    size_t n = strlen(valid);
    /* Every possible two-fragment boundary. */
    for (size_t split = 1; split < n; ++split) {
        host_mqtt_event_t e = event_for(valid, (int)n);
        e.data_len = (int)split;
        remote_config_mqtt(&e);
        assert(!job_count);
        e.topic = NULL; e.topic_len = 0;
        e.current_data_offset = (int)split;
        e.data = (char *)valid + split;
        e.data_len = (int)(n - split);
        remote_config_mqtt(&e);
        assert(job_count == 1);
        drain();
    }
    assert(sends == n - 1);
    unsigned before = sends;
    for (int scenario = 0; scenario < 12; ++scenario) {
        host_mqtt_event_t e = event_for(valid, (int)n);
        e.data_len = 20;
        remote_config_mqtt(&e);
        e.current_data_offset = 20; e.data = (char *)valid + 20;
        e.data_len = (int)n - 20; e.topic = NULL; e.topic_len = 0;
        switch (scenario) {
        case 0: e.current_data_offset++; break;
        case 1: e.total_data_len++; break;
        case 2: e.retain = true; break;
        case 3: e.msg_id++; break;
        case 4: e.data_len++; break;
        case 5: e.data_len = -1; break;
        case 6: e.data = NULL; break;
        case 7: e.topic = "wrong"; e.topic_len = 5; break;
        case 8: e.topic_len = -1; break;
        case 9: case 10: case 11: {
            host_mqtt_event_t stop = {.event_id = scenario == 9 ? MQTT_EVENT_DISCONNECTED :
                scenario == 10 ? MQTT_EVENT_CONNECTED : MQTT_EVENT_ERROR};
            remote_config_mqtt(&stop); break;
        }
        }
        remote_config_mqtt(&e);
        assert(!s_rx.active && !job_count);
    }
    assert(sends == before);
    host_mqtt_event_t e = event_for(valid, (int)n);
    e.retain = true; remote_config_mqtt(&e); assert(!job_count);
    e.retain = false; e.total_data_len = 1025; remote_config_mqtt(&e); assert(!job_count);
    e.total_data_len = 0; remote_config_mqtt(&e); assert(!job_count);
    e = event_for(valid, (int)n); e.topic = NULL;
    remote_config_mqtt(&e); assert(!job_count);
    e = event_for(valid, (int)n); e.data_len = 20; remote_config_mqtt(&e);
    remote_config_reconnect();
    e.current_data_offset = 20; e.data = (char *)valid + 20; e.data_len = (int)n - 20;
    remote_config_mqtt(&e); assert(!job_count && !s_rx.active);
    e = event_for(valid, (int)n); e.data_len = 20; remote_config_mqtt(&e);
    host_mqtt_event_t other = event_for("{}", 2);
    other.topic = "other"; other.topic_len = 5; remote_config_mqtt(&other);
    assert(!s_rx.active);
    e = event_for(valid, (int)n); remote_config_mqtt(&e); assert(job_count == 1); drain();
    char padded[1025];
    memset(padded, ' ', 1024); memcpy(padded, valid, n); padded[1024] = 0;
    deliver(padded); assert(sends == before + 2);
    e = event_for(valid, (int)n);
    for (int i = 0; i < CONFIG_COMMAND_QUEUE + 1; ++i) remote_config_mqtt(&e);
    assert(job_count == CONFIG_COMMAND_QUEUE && sends == before + 2);
    drain(); assert(sends == before + 2 + CONFIG_COMMAND_QUEUE && !publications);
}
static void ack_tests(void) {
    reset();
    battery_config_ack_t a = make_ack();
    remote_config_ack(NULL);
    remote_config_reconnect(); flush_acks(); assert(!publications);
    remote_config_ack(&a); assert(!publications);
    enqueue_fail = true; flush_acks(); assert(!publications && s_acks[0].pending);
    enqueue_fail = false; flush_acks(); assert(publications == 1 && !s_acks[0].pending);
    cJSON *j = cJSON_Parse(last_json);
    assert(j && !strcmp(cJSON_GetObjectItem(j, "status")->valuestring, "applied"));
    assert(!strcmp(cJSON_GetObjectItem(j, "reason")->valuestring, "none"));
    assert(!strcmp(cJSON_GetObjectItem(j, "command_id")->valuestring,
                   "01234567-89ab-4cde-8f01-23456789abcd"));
    assert(!strcmp(cJSON_GetObjectItem(j, "device_id")->valuestring, "abcdef01"));
    assert(cJSON_GetObjectItem(j, "version")->valuedouble == 42);
    cJSON_Delete(j);
    char original[1024]; strcpy(original, last_json);
    puts(last_json); /* Python additionally validates against actual web schema. */
    remote_config_reconnect(); flush_acks();
    assert(publications == 2 && !strcmp(last_json, original));
    remote_config_ack(&a); flush_acks();
    assert(publications == 3 && !strcmp(last_json, original) && s_sequence == 1);
    a.status = BATTERY_CONFIG_DEFERRED; remote_config_ack(&a); flush_acks();
    assert(publications == 4 && strcmp(last_json, original)); puts(last_json);
    a.status = BATTERY_CONFIG_REJECTED;
    for (int reason = BATTERY_CONFIG_REASON_INVALID; reason <= BATTERY_CONFIG_REASON_STORAGE; ++reason) {
        a.reason = (uint8_t)reason; remote_config_ack(&a); flush_acks(); puts(last_json);
    }
    a.reason = 255; remote_config_ack(&a); flush_acks();
    assert(strstr(last_json, "h2_reason_255")); puts(last_json);
    unsigned before = publications;
    a.version = 2; remote_config_ack(&a); flush_acks(); assert(publications == before);
    a.version = 1; a.revision = 0; remote_config_ack(&a); flush_acks(); assert(publications == before);
    a.revision = 42; a.status = 3; remote_config_ack(&a); flush_acks(); assert(publications == before);
    reset(); a = make_ack(); remote_config_ack(&a);
    reconnect_during_enqueue = true; flush_acks();
    assert(s_acks[0].pending); flush_acks(); assert(!s_acks[0].pending && publications == 2);
    /* Cache retains bounded latest responses; never needs a matching MQTT command
     * to forward a genuine ACK recovered by H2 after a gateway reboot. */
    reset(); a = make_ack();
    enqueue_fail = true;
    for (unsigned i = 0; i < CONFIG_ACK_CACHE + 5; ++i) {
        a.revision = i + 1; remote_config_ack(&a);
    }
    flush_acks(); assert(!publications);
    enqueue_fail = false; remote_config_reconnect(); flush_acks();
    assert(publications == CONFIG_ACK_CACHE);
    for (unsigned i = 0; i < CONFIG_ACK_CACHE; ++i) assert(s_acks[i].ack.revision >= 6);
    /* Largest boot + sequence yields message_id <=128, and uint32 revisions survive. */
    reset(); memset(s_boot, 'b', 64); s_boot[64] = 0;
    s_sequence = UINT64_MAX - 1; a = make_ack(); a.revision = UINT32_MAX;
    remote_config_ack(&a); flush_acks();
    j = cJSON_Parse(last_json); assert(j);
    assert(strlen(cJSON_GetObjectItem(j, "message_id")->valuestring) <= 128);
    assert(cJSON_GetObjectItem(j, "version")->valuedouble == UINT32_MAX);
    cJSON_Delete(j); puts(last_json);
}
static void init_tests(void) {
    s_jobs = NULL;
    assert(remote_config_init(NULL, "boot") == ESP_ERR_INVALID_ARG);
    assert(remote_config_init((void *)1, NULL) == ESP_ERR_INVALID_ARG);
    assert(remote_config_init((void *)1, "") == ESP_ERR_INVALID_ARG);
    char long_boot[66]; memset(long_boot, 'x', 65); long_boot[65] = 0;
    assert(remote_config_init((void *)1, long_boot) == ESP_ERR_INVALID_ARG);
    queue_fail = true;
    assert(remote_config_init((void *)1, "boot") == ESP_ERR_NO_MEM);
    queue_fail = false; task_fail = true;
    assert(remote_config_init((void *)1, "boot") == ESP_ERR_NO_MEM && !s_jobs);
    task_fail = false;
    assert(remote_config_init((void *)1, "boot") == ESP_OK);
    assert(remote_config_init((void *)1, "boot") == ESP_ERR_INVALID_STATE);
}
int main(void) {
    init_tests(); parser_tests(); framing_tests(); ack_tests();
    fputs("remote_config: parser, framing, worker, ACK cache and reconnect passed\n", stderr);
    return 0;
}
