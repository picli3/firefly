#include "ethernet_mqtt.h"
#include "remote_test.h"
#include "remote_config.h"
#include "sdkconfig.h"
#include "gateway_settings.h"

#ifdef CONFIG_GATEWAY_ETH_ENABLED
#include <inttypes.h>
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "app_config.h"

static const char *TAG = "ethernet_mqtt";
static esp_mqtt_client_handle_t s_mqtt;
static bool s_mqtt_started;
static char s_status_topic[128];
static char s_gateway_boot[33];
static char s_will[240];
static esp_mqtt_client_config_t s_mqtt_config;
static atomic_bool s_connected;
static atomic_bool s_eth_link;
static _Atomic(esp_netif_t *) s_eth_netif;
static atomic_uint s_message_sequence;
static unsigned s_connection_sequence;
static QueueHandle_t s_samples;

typedef struct {
    uint16_t short_addr;
    emergency_telemetry_t sample;
    bool v2, config_capable;
    uint32_t config_version, boot_id;
    int64_t received_us;
} mqtt_sample_t;

static void publish_status(void)
{
    char payload[320];
    // test_max_duration_s tells the web which remote test durations this firmware accepts.
    snprintf(payload, sizeof(payload),
             "{\"schema_version\":1,\"message_id\":\"%s-status-%u\",\"online\":true,\"boot_id\":\"%s\",\"test_capable\":%s,\"test_max_duration_s\":%d}",
             s_gateway_boot, atomic_fetch_add(&s_message_sequence, 1), s_gateway_boot, remote_test_ready() ? "true" : "false",
             APP_REMOTE_TEST_MAX_S);
    if (esp_mqtt_client_enqueue(s_mqtt, s_status_topic, payload, 0, 1, 1, true) < 0) {
        ESP_LOGW(TAG, "MQTT heartbeat could not be queued");
    }
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    remote_config_mqtt((esp_mqtt_event_handle_t)data);
    if (id == MQTT_EVENT_BEFORE_CONNECT) {
        // A distinct will ID on every connection lets Firefly deduplicate
        // retransmissions without ignoring a later loss of the same gateway.
        snprintf(s_will, sizeof(s_will),
                 "{\"schema_version\":1,\"message_id\":\"%s-will-%u\",\"online\":false,\"boot_id\":\"%s\"}",
                 s_gateway_boot, ++s_connection_sequence, s_gateway_boot);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_set_config(s_mqtt, &s_mqtt_config));
    } else if (id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT connected");
        atomic_store(&s_connected, true);
        char topic[128];
        snprintf(topic,sizeof(topic),"firefly/v1/gateways/%s/commands/test",gateway_settings_active()->gateway_id);
        esp_mqtt_client_subscribe(s_mqtt,topic,1);
        snprintf(topic,sizeof(topic),"firefly/v1/gateways/%s/commands/config",gateway_settings_active()->gateway_id);
        esp_mqtt_client_subscribe(s_mqtt,topic,1);
        remote_config_reconnect();
        publish_status();
    } else if (id == MQTT_EVENT_DATA) {
        remote_test_mqtt((esp_mqtt_event_handle_t)data);
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        atomic_store(&s_connected, false);
        ESP_LOGW(TAG, "MQTT disconnected; client will reconnect");
    } else if (id == MQTT_EVENT_ERROR) {
        // Do not log the URI or credentials.
        esp_mqtt_event_handle_t event = data;
        if (event && event->error_handle) {
            const esp_mqtt_error_codes_t *error = event->error_handle;
            if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGW(TAG, "MQTT broker refused connection: CONNACK=%d", error->connect_return_code);
            } else {
                ESP_LOGW(TAG, "MQTT transport error: type=%d tls=0x%x stack=%d verify=0x%x socket=%d",
                         error->error_type, error->esp_tls_last_esp_err, error->esp_tls_stack_err,
                         error->esp_tls_cert_verify_flags, error->esp_transport_sock_errno);
            }
        } else {
            ESP_LOGW(TAG, "MQTT connection error");
        }
    }
}

static void mqtt_start_once(void)
{
    if (s_mqtt && !s_mqtt_started) {
        esp_err_t err = esp_mqtt_client_start(s_mqtt);
        if (err == ESP_OK) s_mqtt_started = true;
        else ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
    }
}

static void time_sync(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP synchronized; starting verified MQTT TLS");
    mqtt_start_once();
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *event = data;
    ESP_LOGI(TAG, "Ethernet DHCP IP=" IPSTR, IP2STR(&event->ip_info.ip));
    if (s_mqtt && gateway_settings_active()->tls) {
        ESP_LOGI(TAG, "Waiting for NTP before MQTT TLS");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_sntp_start());
    } else mqtt_start_once();
}

static void eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ETHERNET_EVENT_CONNECTED) {
        atomic_store(&s_eth_link, true);
        ESP_LOGI(TAG, "Ethernet link up");
    }
    else if (id == ETHERNET_EVENT_DISCONNECTED) {
        atomic_store(&s_connected, false);
        atomic_store(&s_eth_link, false);
        ESP_LOGW(TAG, "Ethernet link down");
    }
}

static void publish_task(void *arg)
{
    mqtt_sample_t entry;
    char topic[128], payload[700];
    struct {
        uint32_t device_id, uptime_s, generation;
        bool used;
    } nodes[APP_MAX_TRACKED_NODES] = {0};
    int64_t next_heartbeat = 0;
    for (;;) {
        bool received = xQueueReceive(s_samples, &entry, pdMS_TO_TICKS(1000)) == pdTRUE;
        int64_t now = esp_timer_get_time();
        if (!atomic_load(&s_connected)) continue;
        if (now >= next_heartbeat) {
            publish_status();
            next_heartbeat = now + INT64_C(1000000) * gateway_settings_active()->heartbeat_s;
        }
        if (!received) continue;
        const emergency_telemetry_t *s = &entry.sample;
        unsigned slot;
        for (slot = 0; slot < APP_MAX_TRACKED_NODES; slot++) {
            if (!nodes[slot].used || nodes[slot].device_id == s->device_id) break;
        }
        if (slot == APP_MAX_TRACKED_NODES) {
            ESP_LOGW(TAG, "MQTT node tracking capacity reached");
            continue;
        }
        if (nodes[slot].used && s->uptime_s < nodes[slot].uptime_s) nodes[slot].generation++;
        nodes[slot].used = true;
        nodes[slot].device_id = s->device_id;
        nodes[slot].uptime_s = s->uptime_s;
        // Zigbee v1 has no node boot ID. This identifies the observed legacy
        // stream, changing on a gateway restart or an observed uptime reset.
        char node_boot[80];
        snprintf(node_boot, sizeof(node_boot), "legacy-%s-%08" PRIx32 "-%" PRIu32,
                 s_gateway_boot, s->device_id, nodes[slot].generation);
        if (entry.v2) snprintf(node_boot, sizeof(node_boot), "h2-%08" PRIx32 "-%08" PRIx32,
                               s->device_id, entry.boot_id);
        char revision[60] = "";
        if (entry.v2 && entry.config_version)
            snprintf(revision, sizeof(revision), ",\"config_version\":%" PRIu32, entry.config_version);
        snprintf(topic, sizeof(topic), "firefly/v1/gateways/%s/telemetry", gateway_settings_active()->gateway_id);
        int length = snprintf(payload, sizeof(payload),
            "{\"schema_version\":1,\"message_id\":\"%s-telemetry-%u\",\"device_id\":\"%08" PRIx32 "\","
            "\"boot_id\":\"%s\",\"sequence\":%u,\"flags\":%u,\"voltage_mv\":%u,"
            "\"current_ma\":%d,\"power_mw\":%" PRId32 ",\"temperature_centi_c\":%d,"
            "\"soh_pct\":%u,\"uptime_s\":%" PRIu32 ",\"report_interval_s\":%d,"
            "\"live\":%s,\"config_capable\":%s%s}",
            s_gateway_boot, atomic_fetch_add(&s_message_sequence, 1), s->device_id,
            node_boot, s->sequence, s->flags, s->vpack_mv, s->ipack_ma, s->power_mw,
            s->temp_centi_c, s->soh_pct, s->uptime_s, CONFIG_GATEWAY_MQTT_REPORT_INTERVAL_SECONDS,
            now - entry.received_us <= INT64_C(5000000) ? "true" : "false",
            entry.config_capable ? "true" : "false", revision);
        if (length <= 0 || length >= sizeof(payload)) {
            ESP_LOGW(TAG, "MQTT payload too large; CSV logging is independent");
            continue;
        }
        if (esp_mqtt_client_enqueue(s_mqtt, topic, payload, length, 1, 0, true) < 0) {
            ESP_LOGW(TAG, "MQTT outbox full/unavailable; CSV logging is independent");
        }
    }
}

esp_err_t ethernet_mqtt_start(void)
{
    const gateway_settings_t *settings = gateway_settings_active();
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) ESP_RETURN_ON_ERROR(err, TAG, "event loop");

    const int pins[] = {CONFIG_GATEWAY_ETH_SCLK, CONFIG_GATEWAY_ETH_MOSI,
        CONFIG_GATEWAY_ETH_MISO, CONFIG_GATEWAY_ETH_CS, CONFIG_GATEWAY_ETH_INT,
        CONFIG_GATEWAY_ETH_RESET};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (pins[i] < 0) continue;
        ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(pins[i]) && pins[i] != 12 && pins[i] != 13,
                            ESP_ERR_INVALID_ARG, TAG, "Invalid GPIO or native USB conflict");
        for (unsigned j = 0; j < i; j++) {
            ESP_RETURN_ON_FALSE(pins[i] != pins[j], ESP_ERR_INVALID_ARG, TAG, "Duplicate Ethernet GPIO");
        }
        if (i != 2 && i != 4) {
            ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(pins[i]), ESP_ERR_INVALID_ARG, TAG, "Output GPIO required");
        }
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_ERR_INVALID_STATE) ESP_RETURN_ON_ERROR(err, TAG, "GPIO ISR");
    spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_GATEWAY_ETH_MOSI, .miso_io_num = CONFIG_GATEWAY_ETH_MISO,
        .sclk_io_num = CONFIG_GATEWAY_ETH_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
    spi_device_interface_config_t dev = {
        .mode = 0, .clock_speed_hz = CONFIG_GATEWAY_ETH_SPI_MHZ * 1000 * 1000, .queue_size = 20,
        .spics_io_num = CONFIG_GATEWAY_ETH_CS,
    };
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &dev);
    w5500.int_gpio_num = CONFIG_GATEWAY_ETH_INT;
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = CONFIG_GATEWAY_ETH_RESET;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!mac || !phy) {
        if (mac) mac->del(mac);
        if (phy) phy->del(phy);
        spi_bus_free(SPI2_HOST);
        return ESP_ERR_NO_MEM;
    }
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    err = esp_eth_driver_install(&cfg, &eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "W5500 driver install failed: %s", esp_err_to_name(err));
        // Failed installation frees its mediator; remove the MAC worker/ISR
        // before Zigbee starts, without calling deinit through that mediator.
        gpio_isr_handler_remove(CONFIG_GATEWAY_ETH_INT);
        mac->del(mac);
        phy->del(phy);
        spi_bus_free(SPI2_HOST);
        return err;
    }
    uint8_t address[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(address, ESP_MAC_ETH), TAG, "Ethernet MAC");
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth, ETH_CMD_S_MAC_ADDR, address), TAG, "set MAC");
    esp_netif_config_t net_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&net_cfg);
    ESP_RETURN_ON_FALSE(netif, ESP_ERR_NO_MEM, TAG, "Ethernet netif");
    atomic_store(&s_eth_netif, netif);
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth);
    ESP_RETURN_ON_FALSE(glue, ESP_ERR_NO_MEM, TAG, "Ethernet glue");
    ESP_RETURN_ON_ERROR(esp_netif_attach(netif, glue), TAG, "attach Ethernet");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event, NULL), TAG, "ETH events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_event, NULL), TAG, "IP events");

    if (settings->uri[0]) {
        const char *scheme = settings->tls ? "mqtts://" : "mqtt://";
        ESP_RETURN_ON_FALSE(strncmp(settings->uri, scheme, strlen(scheme)) == 0,
                            ESP_ERR_INVALID_ARG, TAG, "MQTT URI scheme does not match TLS setting");
        size_t id_length = strlen(settings->gateway_id);
        ESP_RETURN_ON_FALSE(id_length && id_length <= 64, ESP_ERR_INVALID_ARG, TAG, "Set registered Firefly gateway ID");
        for (size_t i = 0; i < id_length; i++) {
            unsigned char c = settings->gateway_id[i];
            ESP_RETURN_ON_FALSE(isalnum(c) || c == '_' || c == '-', ESP_ERR_INVALID_ARG, TAG, "Invalid gateway ID");
        }
        uint32_t random_boot[4];
        esp_fill_random(random_boot, sizeof(random_boot));
        snprintf(s_gateway_boot, sizeof(s_gateway_boot), "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
                 random_boot[0], random_boot[1], random_boot[2], random_boot[3]);
        snprintf(s_status_topic, sizeof(s_status_topic), "firefly/v1/gateways/%s/status", settings->gateway_id);
        snprintf(s_will, sizeof(s_will),
                 "{\"schema_version\":1,\"message_id\":\"%s-will-0\",\"online\":false,\"boot_id\":\"%s\"}",
                 s_gateway_boot, s_gateway_boot);
        s_mqtt_config = (esp_mqtt_client_config_t){
            .broker.address.uri = settings->uri,
            .broker.verification.certificate = settings->tls ? settings->ca_pem : NULL,
            .credentials.username = settings->username[0] ? settings->username : NULL,
            .credentials.client_id = settings->client_id[0] ? settings->client_id : NULL,
            .credentials.authentication.password = settings->password[0] ? settings->password : NULL,
            .session.keepalive = settings->keepalive_s,
            .session.last_will = {.topic = s_status_topic, .msg = s_will, .qos = 1, .retain = 1},
            .outbox.limit = 32768,
        };
        s_mqtt = esp_mqtt_client_init(&s_mqtt_config);
        ESP_RETURN_ON_FALSE(s_mqtt, ESP_ERR_NO_MEM, TAG, "MQTT allocation");
        ESP_RETURN_ON_ERROR(remote_test_init(s_mqtt,s_gateway_boot), TAG, "Remote test init");
        ESP_RETURN_ON_ERROR(remote_config_init(s_mqtt,s_gateway_boot), TAG, "Remote config init");
        ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL), TAG, "MQTT events");
        if (settings->tls) {
            ESP_RETURN_ON_FALSE(settings->ntp_count && settings->ntp_count <= CONFIG_LWIP_SNTP_MAX_SERVERS,
                                ESP_ERR_INVALID_ARG, TAG, "NTP server count exceeds firmware capacity");
            esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(settings->ntp[0]);
            ntp.start = false;
            ntp.sync_cb = time_sync;
            ntp.num_of_servers = settings->ntp_count;
            for (unsigned i = 0; i < settings->ntp_count; ++i) ntp.servers[i] = settings->ntp[i];
            ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&ntp), TAG, "NTP init");
        }
        QueueHandle_t samples = xQueueCreate(16, sizeof(mqtt_sample_t));
        ESP_RETURN_ON_FALSE(samples, ESP_ERR_NO_MEM, TAG, "MQTT sample queue");
        s_samples = samples;
        if (xTaskCreate(publish_task, "mqtt_samples", 4096, NULL, 3, NULL) != pdPASS) {
            s_samples = NULL;
            vQueueDelete(samples);
            return ESP_ERR_NO_MEM;
        }
    } else {
        ESP_LOGI(TAG, "MQTT URI empty: Ethernet/DHCP test only");
    }
    ESP_LOGI(TAG, "W5500 SPI=%dMHz CLK=%d MOSI=%d MISO=%d CS=%d INT=%d RESET=%d",
             CONFIG_GATEWAY_ETH_SPI_MHZ, CONFIG_GATEWAY_ETH_SCLK, CONFIG_GATEWAY_ETH_MOSI, CONFIG_GATEWAY_ETH_MISO,
             CONFIG_GATEWAY_ETH_CS, CONFIG_GATEWAY_ETH_INT, CONFIG_GATEWAY_ETH_RESET);
    return esp_eth_start(eth);
}

void ethernet_mqtt_telemetry(uint16_t short_addr, const emergency_telemetry_t *sample)
{
    if (!s_samples || !atomic_load(&s_connected)) return;
    mqtt_sample_t entry = {.short_addr = short_addr, .sample = *sample, .received_us = esp_timer_get_time()};
    if (xQueueSend(s_samples, &entry, 0) != pdTRUE) {
        ESP_LOGW(TAG, "MQTT sample queue full; CSV logging is independent");
    }
}

void ethernet_mqtt_telemetry_v2(uint16_t short_addr, const emergency_telemetry_v2_t *sample)
{
    if (!s_samples || !atomic_load(&s_connected)) return;
    mqtt_sample_t entry = {.short_addr = short_addr, .sample = sample->sample, .v2 = true,
        .config_capable = sample->config_capable == 1, .config_version = sample->config_version,
        .boot_id = sample->boot_id, .received_us = esp_timer_get_time()};
    if (xQueueSend(s_samples, &entry, 0) != pdTRUE)
        ESP_LOGW(TAG, "MQTT sample queue full; CSV logging is independent");
}
void ethernet_mqtt_status(char *out, size_t size)
{
    esp_netif_ip_info_t ip = {0};
    esp_netif_t *netif = atomic_load(&s_eth_netif);
    if (netif) esp_netif_get_ip_info(netif, &ip);
    snprintf(out, size, "+FFSTATUS: ethernet=1,link=%u,ip=" IPSTR ",mqtt_connected=%u,gateway_id=%s\r\nOK\r\n",
             atomic_load(&s_eth_link), IP2STR(&ip.ip), atomic_load(&s_connected), gateway_settings_active()->gateway_id);
}
#else
#include <stdio.h>
esp_err_t ethernet_mqtt_start(void) { return ESP_OK; }
void ethernet_mqtt_telemetry(uint16_t short_addr, const emergency_telemetry_t *sample) {}
void ethernet_mqtt_telemetry_v2(uint16_t short_addr, const emergency_telemetry_v2_t *sample) {}
void ethernet_mqtt_status(char *out, size_t size) { snprintf(out, size, "+FFSTATUS: ethernet=0,mqtt_connected=0\r\nOK\r\n"); }
#endif
