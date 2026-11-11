/* Optional MQTT bridge with Home Assistant discovery.  Switched on and off at
 * runtime ("mq_on" in NVS, Web UI card "MQTT").  Topics and entity names match
 * the former ESPHome firmware, so the old dashboard keeps working.
 */
#include "app.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

typedef struct {
    const char *comp, *key, *topic, *name, *unit, *dclass, *sclass, *ecat;
    bool retain;
} ent_t;

enum { E_DIST, E_SPEED, E_SPLIT, E_SPM, E_STROKES, E_RATIO, E_PULL, E_DUR, E_DURF, E_WATTS, E_TOTAL,
       E_AVGSPD, E_AVGSPM, E_SID, E_SACT, E_S4C, E_USB, E_STATUS, E_FW, E_LAST, E_TIMEOUT,
       E_RESET, E_END, E_COUNT };

static const ent_t ENTS[E_COUNT] = {
    [E_DIST]    = {"sensor", "distance", "distance", "Distance", "m", "distance", "measurement", NULL, false},
    [E_SPEED]   = {"sensor", "speed", "speed", "Speed", "m/s", "speed", "measurement", NULL, false},
    [E_SPLIT]   = {"sensor", "split", "split_500m", "500m Split", NULL, NULL, NULL, NULL, false},
    [E_SPM]     = {"sensor", "stroke_rate", "stroke_rate", "Stroke Rate", "spm", NULL, "measurement", NULL, false},
    [E_STROKES] = {"sensor", "strokes", "strokes", "Strokes", NULL, NULL, "total_increasing", NULL, false},
    [E_RATIO]   = {"sensor", "ratio", "ratio", "Ratio", NULL, NULL, "measurement", NULL, false},
    [E_PULL]    = {"sensor", "pull_time", "pull_time", "Pull Time", "s", "duration", "measurement", NULL, false},
    [E_DUR]     = {"sensor", "duration", "duration", "Duration", "s", "duration", "measurement", NULL, false},
    [E_DURF]    = {"sensor", "duration_hms", "duration_formatted", "Duration (hh:mm:ss)", NULL, NULL, NULL, NULL, false},
    [E_WATTS]   = {"sensor", "watts", "watts", "Watts", "W", "power", "measurement", NULL, false},
    [E_TOTAL]   = {"sensor", "total_distance", "total_distance", "Total Distance", "m", "distance", "total_increasing", NULL, true},
    [E_AVGSPD]  = {"sensor", "session_avg_speed", "session_avg_speed", "Session Avg Speed", "m/s", "speed", "measurement", NULL, false},
    [E_AVGSPM]  = {"sensor", "session_avg_spm", "session_avg_stroke_rate", "Session Avg Stroke Rate", "spm", NULL, "measurement", NULL, false},
    [E_SID]     = {"sensor", "session_id", "session_id", "Session ID", NULL, NULL, NULL, NULL, true},
    [E_SACT]    = {"binary_sensor", "session_active", "session_active", "Session Active", NULL, NULL, NULL, NULL, true},
    [E_S4C]     = {"binary_sensor", "s4_connected", "s4_connected", "S4 Connected", NULL, "connectivity", NULL, "diagnostic", true},
    [E_USB]     = {"binary_sensor", "usb_mode", "usb_mode", "USB Mode", NULL, NULL, NULL, "diagnostic", true},
    [E_STATUS]  = {"sensor", "s4_status", "s4_status", "S4 Status", NULL, NULL, NULL, "diagnostic", false},
    [E_FW]      = {"sensor", "firmware_version", "firmware_version", "Firmware Version", NULL, NULL, NULL, "diagnostic", true},
    [E_LAST]    = {"sensor", "last_session", "session/last", "Last Session", NULL, NULL, NULL, NULL, true},
    [E_TIMEOUT] = {"number", "session_timeout", "session_timeout", "Session Timeout", "min", NULL, NULL, "config", true},
    [E_RESET]   = {"button", "reset", "cmd/end_session", "Reset", NULL, NULL, NULL, NULL, false},
    [E_END]     = {"button", "end_session", "cmd/end_session", "End Session", NULL, NULL, NULL, NULL, false},
};

static esp_mqtt_client_handle_t client;
static volatile bool connected, reconf = true, need_all;
static bool enabled, discovery;
static char host[100], user[64], pass[64], prefix[48], disc[32], devid[24], last_error[80];
static int port;
static char cache[E_COUNT][200];
static uint32_t last_seq, last_beat;

static void load_cfg(void)
{
    enabled = settings_get_i32("mq_on", 0) != 0;
    port = settings_get_i32("mq_port", 1883);
    discovery = settings_get_i32("mq_disc", 1) != 0;
    settings_get_str("mq_host", host, sizeof host);
    settings_get_str("mq_user", user, sizeof user);
    settings_get_str("mq_pass", pass, sizeof pass);
    if (!settings_get_str("mq_pre", prefix, sizeof prefix)) strcpy(prefix, "waterrower");
    if (!settings_get_str("mq_disc_pre", disc, sizeof disc)) strcpy(disc, "homeassistant");
    size_t l = strlen(prefix);
    while (l && prefix[l - 1] == '/') prefix[--l] = 0;
}

static void pub(const char *sub, const char *payload, bool retain)
{
    char t[160];
    snprintf(t, sizeof t, "%s/%s", prefix, sub);
    esp_mqtt_client_enqueue(client, t, payload, 0, 0, retain, false);
}

/* ---- values -------------------------------------------------------------- */
static void format_value(int id, const live_t *l, char *o, size_t n)
{
    switch (id) {
    case E_DIST:    snprintf(o, n, "%d", (int) l->distance_m); break;
    case E_SPEED:   snprintf(o, n, "%.2f", l->speed_ms); break;
    case E_SPLIT:
        if (l->speed_ms > 0.05f) { int s = (int) (500.0f / l->speed_ms + 0.5f); snprintf(o, n, "%d:%02d", s / 60, s % 60); }
        else snprintf(o, n, "--:--");
        break;
    case E_SPM:     snprintf(o, n, "%d", (int) l->stroke_rate); break;
    case E_STROKES: snprintf(o, n, "%d", (int) l->strokes); break;
    case E_RATIO:   snprintf(o, n, "%.2f", l->ratio); break;
    case E_PULL:    snprintf(o, n, "%.2f", l->pull_s); break;
    case E_DUR:     snprintf(o, n, "%d", (int) l->duration_s); break;
    case E_DURF: { int d = (int) l->duration_s; snprintf(o, n, "%02d:%02d:%02d", d / 3600, d / 60 % 60, d % 60); break; }
    case E_WATTS:   snprintf(o, n, "%d", (int) l->watts); break;
    case E_TOTAL:   snprintf(o, n, "%d", (int) l->total_distance_m); break;
    case E_AVGSPD:  snprintf(o, n, "%.2f", l->sess_avg_speed_ms); break;
    case E_AVGSPM:  snprintf(o, n, "%.1f", l->sess_avg_spm); break;
    case E_SID:     snprintf(o, n, "%s", l->session_id); break;
    case E_SACT:    snprintf(o, n, "%s", l->session_active ? "ON" : "OFF"); break;
    case E_S4C:     snprintf(o, n, "%s", l->s4_connected ? "ON" : "OFF"); break;
    case E_USB:     snprintf(o, n, "%s", l->usb_mode ? "ON" : "OFF"); break;
    case E_STATUS:
        snprintf(o, n, "%s", !l->s4_connected ? "not connected" : l->session_active ? "recording"
                 : l->usb_mode ? "connected" : "connected, handshake pending");
        break;
    case E_FW:      snprintf(o, n, "%s", FW_VERSION); break;
    case E_LAST:    live_get_last(o, n); break;
    case E_TIMEOUT: snprintf(o, n, "%d", rower_timeout_min()); break;
    default:        o[0] = 0;
    }
}

static void publish_states(bool force)
{
    live_t l;
    live_get(&l);
    char v[200];
    for (int i = 0; i < E_COUNT; i++) {
        if (ENTS[i].comp[0] == 'b' && ENTS[i].comp[1] == 'u') continue;    /* buttons: no state */
        format_value(i, &l, v, sizeof v);
        if (!v[0]) continue;                                               /* e.g. no last session yet */
        if (!force && !strcmp(v, cache[i])) continue;
        strcpy(cache[i], v);
        pub(ENTS[i].topic, v, ENTS[i].retain);
    }
    if (l.session_active) {
        char j[240];
        snprintf(j, sizeof j,
            "{\"session_id\":\"%s\",\"distance_m\":%d,\"speed_ms\":%.2f,\"stroke_rate\":%d,\"strokes\":%d,"
            "\"duration_s\":%d,\"watts\":%d}",
            l.session_id, (int) l.distance_m, l.speed_ms, (int) l.stroke_rate, (int) l.strokes,
            (int) l.duration_s, (int) l.watts);
        pub("live", j, false);
    }
}

/* ---- discovery ------------------------------------------------------------ */
static void ap(char *b, size_t cap, int *n, const char *fmt, ...)
{
    if ((size_t) *n >= cap) return;
    va_list a; va_start(a, fmt);
    *n += vsnprintf(b + *n, cap - *n, fmt, a);
    va_end(a);
}

static void publish_discovery(void)
{
    static char topic[200], b[900];
    for (int i = 0; i < E_COUNT; i++) {
        const ent_t *e = &ENTS[i];
        snprintf(topic, sizeof topic, "%s/%s/%s/%s/config", disc, e->comp, devid, e->key);
        int n = 0;
        ap(b, sizeof b, &n, "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"availability_topic\":\"%s/status\"",
           e->name, devid, e->key, prefix);
        if (!strcmp(e->comp, "button")) {
            ap(b, sizeof b, &n, ",\"command_topic\":\"%s/%s\",\"payload_press\":\"%s\"", prefix, e->topic,
               i == E_RESET ? "reset" : "end");
        } else {
            ap(b, sizeof b, &n, ",\"state_topic\":\"%s/%s\"", prefix, e->topic);
        }
        if (!strcmp(e->comp, "number"))
            ap(b, sizeof b, &n, ",\"command_topic\":\"%s/cmd/session_timeout\",\"min\":1,\"max\":60,\"step\":1,\"mode\":\"box\"", prefix);
        if (e->unit) ap(b, sizeof b, &n, ",\"unit_of_measurement\":\"%s\"", e->unit);
        if (e->dclass) ap(b, sizeof b, &n, ",\"device_class\":\"%s\"", e->dclass);
        if (e->sclass) ap(b, sizeof b, &n, ",\"state_class\":\"%s\"", e->sclass);
        if (e->ecat) ap(b, sizeof b, &n, ",\"entity_category\":\"%s\"", e->ecat);
        if (i == E_SPEED || i == E_AVGSPD || i == E_RATIO || i == E_PULL)
            ap(b, sizeof b, &n, ",\"suggested_display_precision\":2");
        ap(b, sizeof b, &n,
           ",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"WaterRower\",\"manufacturer\":\"WaterRower\","
           "\"model\":\"S4 USB bridge (ESP32-S3)\",\"sw_version\":\"%s\"}}", devid, FW_VERSION);
        esp_mqtt_client_enqueue(client, topic, b, 0, 0, true, false);
        vTaskDelay(pdMS_TO_TICKS(40));                       /* do not flood the broker */
    }
    ESP_LOGI(TAG, "discovery published (%d entities)", E_COUNT);
}

/* ---- events ------------------------------------------------------------------ */
static void on_cmd(const char *topic, int tlen, const char *data, int dlen)
{
    char t[160], d[32];
    snprintf(t, sizeof t, "%.*s", tlen, topic);
    snprintf(d, sizeof d, "%.*s", dlen > 31 ? 31 : dlen, data);
    size_t pl = strlen(prefix);
    if (strncmp(t, prefix, pl) || t[pl] != '/') return;
    const char *sub = t + pl + 1;
    if (!strcmp(sub, "cmd/end_session")) rower_cmd(!strcmp(d, "reset") ? ROWER_CMD_RESET : ROWER_CMD_END);
    else if (!strcmp(sub, "cmd/session_timeout")) {
        int m = atoi(d);
        if (m >= 1 && m <= 60) { settings_set_i32("sess_to", m); rower_set_timeout_min(m); cache[E_TIMEOUT][0] = 0; }
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t) id) {
    case MQTT_EVENT_CONNECTED: {
        char t[160];
        connected = true;
        last_error[0] = 0;
        snprintf(t, sizeof t, "%s/status", prefix);
        esp_mqtt_client_publish(client, t, "online", 0, 1, 1);
        snprintf(t, sizeof t, "%s/cmd/#", prefix);
        esp_mqtt_client_subscribe(client, t, 0);
        need_all = true;
        ESP_LOGI(TAG, "connected");
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        connected = false;
        break;
    case MQTT_EVENT_ERROR:
        if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            snprintf(last_error, sizeof last_error, "login refused");
        else if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            snprintf(last_error, sizeof last_error, "broker not reachable");
        break;
    case MQTT_EVENT_DATA:
        if (e->topic && e->total_data_len == e->data_len) on_cmd(e->topic, e->topic_len, e->data, e->data_len);
        break;
    default: break;
    }
}

static void destroy_client(void)
{
    if (client) {
        if (connected) { char t[160]; snprintf(t, sizeof t, "%s/status", prefix); esp_mqtt_client_publish(client, t, "offline", 0, 1, 1); }
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }
    connected = false;
}

static void create_client(void)
{
    char uri[160], lwt[120], cid[40];
    if (strstr(host, "://")) snprintf(uri, sizeof uri, "%s", host);
    else snprintf(uri, sizeof uri, "mqtt://%s:%d", host, port);
    snprintf(lwt, sizeof lwt, "%s/status", prefix);
    snprintf(cid, sizeof cid, "waterrower-%s", devid + 11);
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.username = user[0] ? user : NULL,
        .credentials.client_id = cid,
        .credentials.authentication.password = pass[0] ? pass : NULL,
        .session.last_will = { .topic = lwt, .msg = "offline", .msg_len = 7, .qos = 1, .retain = 1 },
        .session.keepalive = 30,
        .buffer.size = 2048,
        .network.reconnect_timeout_ms = 5000,
    };
    if (!strncmp(uri, "mqtts://", 8)) cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    client = esp_mqtt_client_init(&cfg);         /* copies the strings */
    if (!client) { snprintf(last_error, sizeof last_error, "init failed"); return; }
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, on_event, NULL);
    ESP_LOGI(TAG, "connecting to %s", uri);
    esp_mqtt_client_start(client);
}

static void mqtt_task(void *arg)
{
    while (1) {
        if (reconf) {
            reconf = false;
            destroy_client();
            load_cfg();
            memset(cache, 0, sizeof cache);
            if (enabled && host[0]) create_client();
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!client || !connected) continue;
        if (need_all) {
            need_all = false;
            if (discovery) publish_discovery();
            publish_states(true);
            last_seq = live_seq();
            last_beat = (uint32_t) (esp_timer_get_time() / 1000000);
            continue;
        }
        uint32_t now = (uint32_t) (esp_timer_get_time() / 1000000);
        if (live_seq() != last_seq || now - last_beat >= 30) {
            last_seq = live_seq();
            publish_states(now - last_beat >= 30);
            if (now - last_beat >= 30) last_beat = now;
        }
    }
}

void mqtt_reconfigure(void) { reconf = true; }

int mqtt_status_json(char *buf, size_t len)
{
    char h[220], p[80];
    json_esc(h, sizeof h, host);
    json_esc(p, sizeof p, prefix);
    return snprintf(buf, len,
        "{\"enabled\":%s,\"host\":\"%s\",\"port\":%d,\"username\":\"%s\",\"password\":\"%s\",\"prefix\":\"%s\","
        "\"discovery\":%s,\"connected\":%s,\"error\":\"%s\"}",
        enabled ? "true" : "false", h, port, user, pass[0] ? "••••••" : "", p,
        discovery ? "true" : "false", connected ? "true" : "false", last_error);
}

void mqtt_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(devid, sizeof devid, "waterrower_%02x%02x%02x", mac[3], mac[4], mac[5]);
    load_cfg();
    xTaskCreate(mqtt_task, "mqtt", 6144, NULL, 3, NULL);
}
