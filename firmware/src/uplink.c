/* Uplink to a WaterRower Arena server over one outbound WebSocket.
 * Protocol: see server/app/ingest.py (hello / welcome / sample / summary /
 * backfill / cmd).  One task owns every send, so a streamed backfill never
 * interleaves with live samples.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "uplink";
#define KNOWN_MAX 600
#define BACKFILL_MAX_SAMPLES 20000

typedef struct {
    uint8_t kind;                    /* 0 sample, 1 summary */
    char sid[24];
    uint32_t started;
    sample_t s;
    sum_t sum;
} umsg_t;

static QueueHandle_t q;
static esp_websocket_client_handle_t client;
static volatile bool connected, reconf = true, pong_pending, backfill_pending;
static char url[160], token[80], athlete[48], last_error[80];
static bool enabled;
static uint32_t last_sent_s;
static char (*known)[24];
static int nknown;
static char *rx;
static size_t rx_cap;

static bool cfg_ok(void) { return enabled && url[0] && token[0]; }

static void load_cfg(void)
{
    settings_get_str("arena_url", url, sizeof url);
    settings_get_str("arena_tok", token, sizeof token);
    enabled = settings_get_i32("arena_on", 0) != 0;
}

static bool is_known(const char *id)
{
    for (int i = 0; i < nknown; i++) if (!strcmp(known[i], id)) return true;
    return false;
}

static void remember(const char *id)
{
    if (known && nknown < KNOWN_MAX && !is_known(id)) { strncpy(known[nknown], id, 23); known[nknown++][23] = 0; }
}

/* ---- receive ------------------------------------------------------------ */
static void handle_msg(const char *json)
{
    cJSON *m = cJSON_Parse(json);
    if (!m) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(m, "type"));
    if (!type) { cJSON_Delete(m); return; }

    if (!strcmp(type, "welcome") || !strcmp(type, "known")) {
        cJSON *a = cJSON_GetObjectItem(m, "athlete");
        const char *dn = cJSON_GetStringValue(cJSON_GetObjectItem(a, "display_name"));
        if (dn) { strncpy(athlete, dn, sizeof athlete - 1); athlete[sizeof athlete - 1] = 0; }
        nknown = 0;
        cJSON *k = cJSON_GetObjectItem(m, "known");
        cJSON *it;
        cJSON_ArrayForEach(it, k) if (cJSON_IsString(it)) remember(it->valuestring);
        if (!strcmp(type, "welcome")) {
            connected = true;
            last_error[0] = 0;
            ESP_LOGI(TAG, "up as %s (%d sessions known)", athlete, nknown);
        }
        backfill_pending = true;
    } else if (!strcmp(type, "ping")) {
        pong_pending = true;
    } else if (!strcmp(type, "cmd")) {
        const char *c = cJSON_GetStringValue(cJSON_GetObjectItem(m, "cmd"));
        if (c && !strcmp(c, "reset")) rower_cmd(ROWER_CMD_RESET);
        else if (c && !strcmp(c, "end")) rower_cmd(ROWER_CMD_END);
        else if (c && !strcmp(c, "race")) {
            cJSON *r = cJSON_GetObjectItem(m, "race");
            if (cJSON_IsObject(r)) {
                char *s = cJSON_PrintUnformatted(r);
                if (s) { live_set_race(s); cJSON_free(s); }
            } else live_set_race(NULL);
        }
    } else if (!strcmp(type, "error") || !strcmp(type, "reject")) {
        const char *r = cJSON_GetStringValue(cJSON_GetObjectItem(m, "reason"));
        snprintf(last_error, sizeof last_error, "%s", r ? r : "rejected");
    }
    cJSON_Delete(m);
}

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_websocket_event_data_t *e = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        char hello[256];
        int n = snprintf(hello, sizeof hello,
            "{\"type\":\"hello\",\"token\":\"%s\",\"agent\":\"waterrower-esp/%s\"}", token, FW_VERSION);
        esp_websocket_client_send_text(client, hello, n, pdMS_TO_TICKS(5000));
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
        connected = false;
        backfill_pending = false;
        break;
    case WEBSOCKET_EVENT_ERROR:
        connected = false;
        snprintf(last_error, sizeof last_error, "connection error");
        break;
    case WEBSOCKET_EVENT_CLOSED:
        connected = false;
        backfill_pending = false;
        break;
    case WEBSOCKET_EVENT_DATA:
        if (e->op_code != 0x1 && e->op_code != 0x0) break;       /* text only */
        if (e->payload_len > 32768) break;
        if (e->payload_offset == 0) {
            if ((size_t) e->payload_len + 1 > rx_cap) {
                free(rx);
                rx_cap = e->payload_len + 1;
                rx = heap_caps_malloc(rx_cap, MALLOC_CAP_8BIT);
            }
        }
        if (!rx || (size_t) e->payload_offset + e->data_len > rx_cap - 1) break;
        memcpy(rx + e->payload_offset, e->data_ptr, e->data_len);
        if ((size_t) e->payload_offset + e->data_len >= (size_t) e->payload_len) {
            rx[e->payload_len] = 0;
            handle_msg(rx);
        }
        break;
    }
}

/* ---- client lifecycle (uplink task only) --------------------------------- */
static void destroy_client(void)
{
    if (client) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        client = NULL;
    }
    connected = false;
    backfill_pending = false;
}

static void create_client(void)
{
    char uri[200];
    const char *u = url;
    if (!strncmp(u, "https://", 8)) snprintf(uri, sizeof uri, "wss://%s", u + 8);
    else if (!strncmp(u, "http://", 7)) snprintf(uri, sizeof uri, "ws://%s", u + 7);
    else if (!strncmp(u, "ws://", 5) || !strncmp(u, "wss://", 6)) snprintf(uri, sizeof uri, "%s", u);
    else snprintf(uri, sizeof uri, "wss://%s", u);
    size_t l = strlen(uri);
    while (l && uri[l - 1] == '/') uri[--l] = 0;
    strncat(uri, "/ws/uplink", sizeof uri - strlen(uri) - 1);

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
        .task_stack = 6144,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 15000,
        .ping_interval_sec = 20,
        .pingpong_timeout_sec = 30,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    client = esp_websocket_client_init(&cfg);
    if (!client) { snprintf(last_error, sizeof last_error, "init failed"); return; }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    last_error[0] = 0;
    ESP_LOGI(TAG, "connecting to %s", uri);
    esp_websocket_client_start(client);
}

/* ---- sending -------------------------------------------------------------- */
static bool send_text(const char *s, int n)
{
    if (!client || !esp_websocket_client_is_connected(client)) return false;
    int r = esp_websocket_client_send_text(client, s, n, pdMS_TO_TICKS(5000));
    if (r < 0) return false;
    last_sent_s = (uint32_t) (esp_timer_get_time() / 1000000);
    return true;
}

static void send_queued(const umsg_t *m)
{
    char b[400];
    int n;
    if (m->kind == 0) {
        n = snprintf(b, sizeof b,
            "{\"type\":\"sample\",\"session\":\"%s\",\"started_at\":%u,\"t\":%u,\"distance_m\":%u,"
            "\"speed_ms\":%.2f,\"stroke_rate\":%u,\"strokes\":%u,\"watts\":%u}",
            m->sid, (unsigned) m->started, (unsigned) m->s.duration_s, (unsigned) m->s.distance_m,
            m->s.speed_cms / 100.0, (unsigned) m->s.spm, (unsigned) m->s.strokes, (unsigned) m->s.watts);
    } else {
        n = snprintf(b, sizeof b,
            "{\"type\":\"summary\",\"session\":\"%s\",\"started_at\":%u,\"distance_m\":%u,"
            "\"duration_s\":%u,\"strokes\":%u,\"avg_speed_ms\":%.2f,\"avg_spm\":%.1f}",
            m->sid, (unsigned) m->sum.started_at, (unsigned) m->sum.distance_m,
            (unsigned) m->sum.duration_s, (unsigned) m->sum.strokes, m->sum.avg_speed_ms, m->sum.avg_spm);
        remember(m->sid);
    }
    send_text(b, n);
}

/* Stream one finished session the arena has never seen, as one fragmented message. */
static void backfill_one(void)
{
    static char ids[300][24];
    int n = store_list_ids(ids, 300);
    for (int i = n - 1; i >= 0; i--) {                       /* oldest first */
        if (is_known(ids[i])) continue;
        sum_t s; bool has_file;
        if (!store_read_summary(ids[i], &s, &has_file) || !has_file || !s.ended_at) { remember(ids[i]); continue; }
        FILE *f = store_open_samples(ids[i]);
        if (!f) { remember(ids[i]); continue; }
        fseek(f, 0, SEEK_END);
        long cnt = ftell(f) / (long) sizeof(sample_t);
        rewind(f);
        if (cnt <= 0 || cnt > BACKFILL_MAX_SAMPLES) { fclose(f); remember(ids[i]); continue; }

        ESP_LOGI(TAG, "backfilling %s (%ld samples)", ids[i], cnt);
        static char buf[4096];
        int len = snprintf(buf, sizeof buf,
            "{\"type\":\"backfill\",\"session\":\"%s\",\"started_at\":%u,\"ended_at\":%u,"
            "\"summary\":{\"distance_m\":%u,\"duration_s\":%u,\"strokes\":%u,\"avg_speed_ms\":%.2f,\"avg_spm\":%.1f},"
            "\"samples\":[",
            s.id, (unsigned) s.started_at, (unsigned) s.ended_at, (unsigned) s.distance_m,
            (unsigned) s.duration_s, (unsigned) s.strokes, s.avg_speed_ms, s.avg_spm);
        bool ok = esp_websocket_client_send_text_partial(client, buf, len, pdMS_TO_TICKS(5000)) >= 0;
        sample_t b[32];
        bool first = true;
        size_t got;
        while (ok && (got = fread(b, sizeof b[0], 32, f)) > 0) {
            len = 0;
            for (size_t k = 0; k < got; k++) {
                len += snprintf(buf + len, sizeof buf - len,
                    "%s{\"t\":%u,\"distance_m\":%u,\"speed_ms\":%.2f,\"stroke_rate\":%u,\"strokes\":%u,\"watts\":%u}",
                    first ? "" : ",", b[k].duration_s, b[k].distance_m, b[k].speed_cms / 100.0,
                    b[k].spm, b[k].strokes, b[k].watts);
                first = false;
            }
            ok = esp_websocket_client_send_cont_msg(client, buf, len, pdMS_TO_TICKS(5000)) >= 0;
        }
        fclose(f);
        if (ok) ok = esp_websocket_client_send_cont_msg(client, "]}", 2, pdMS_TO_TICKS(5000)) >= 0;
        if (ok) ok = esp_websocket_client_send_fin(client, pdMS_TO_TICKS(5000)) >= 0;
        if (ok) remember(ids[i]); else ESP_LOGW(TAG, "backfill of %s failed", ids[i]);
        vTaskDelay(pdMS_TO_TICKS(500));                       /* leave room for live data */
        return;
    }
    backfill_pending = false;                                 /* nothing left to send */
}

static void uplink_task(void *arg)
{
    umsg_t m;
    while (1) {
        if (reconf) {
            reconf = false;
            destroy_client();
            load_cfg();
            if (cfg_ok()) create_client();
        }
        if (!client) { vTaskDelay(pdMS_TO_TICKS(500)); xQueueReset(q); continue; }
        if (xQueueReceive(q, &m, pdMS_TO_TICKS(200)) == pdTRUE) { if (connected) send_queued(&m); }
        else if (pong_pending) { pong_pending = false; send_text("{\"type\":\"pong\"}", 15); }
        else if (backfill_pending && connected) backfill_one();
    }
}

void uplink_reconfigure(void) { reconf = true; }

void uplink_sample(const char *sid, uint32_t started_at, const sample_t *s)
{
    if (!q || !connected) return;
    umsg_t m = { .kind = 0, .started = started_at, .s = *s };
    strncpy(m.sid, sid, sizeof m.sid - 1);
    xQueueSend(q, &m, 0);
}

void uplink_summary(const sum_t *s)
{
    if (!q || !connected) return;       /* otherwise the backfill picks it up on the next connect */
    umsg_t m = { .kind = 1, .sum = *s };
    strncpy(m.sid, s->id, sizeof m.sid - 1);
    xQueueSend(q, &m, pdMS_TO_TICKS(200));
}

int uplink_status_json(char *buf, size_t len)
{
    return snprintf(buf, len,
        "{\"url\":\"%s\",\"enabled\":%s,\"token_set\":%s,\"connected\":%s,\"athlete\":\"%s\",\"error\":\"%s\",\"last_sent\":%u}",
        url, enabled ? "true" : "false", token[0] ? "true" : "false", connected ? "true" : "false",
        connected ? athlete : "", last_error, (unsigned) last_sent_s);
}

void uplink_init(void)
{
    known = heap_caps_calloc(KNOWN_MAX, 24, MALLOC_CAP_8BIT);
    q = xQueueCreate(40, sizeof(umsg_t));
    load_cfg();
    xTaskCreate(uplink_task, "uplink", 8192, NULL, 4, NULL);
}
