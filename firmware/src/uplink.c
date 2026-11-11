/* Uplink to a WaterRower Arena server over one outbound WebSocket.
 * Protocol: see docs/protokoll.md in MojoOne1/waterrower-arena (hello / welcome / sample / summary /
 * backfill / ack / cmd).
 *
 * One task owns the client and every send, so a streamed backfill never
 * interleaves with live samples.  What has reached the arena is recorded per
 * session in its summary file ("synced"), so nothing depends on the server's
 * own list and a half-delivered session is simply sent again (the server
 * ignores samples it already has).
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "uplink";
#define BACKFILL_MAX_SAMPLES 20000
#define RX_MAX 24576                 /* the welcome lists up to 500 session IDs */
#define RETRY_MIN_S 3
#define RETRY_MAX_S 60

typedef struct {
    uint8_t kind;                    /* 0 sample, 1 summary */
    char sid[24];
    uint32_t started;
    sample_t s;
    sum_t sum;
} umsg_t;

static QueueHandle_t q;
static SemaphoreHandle_t lock;       /* guards the strings shown by uplink_status_json */
static esp_websocket_client_handle_t client;
static volatile bool connected, dropped, reconf = true, pong_pending, backfill_pending;
static volatile uint32_t hold_s;     /* the arena said no: wait this long before the next attempt */
static char url[160], token[96], athlete[48], last_error[80];
static bool enabled;
static uint32_t last_sent_s;
static char *rx;
#define SKIP_MAX 16
static char skip[SKIP_MAX][24];      /* sent on this connection, waiting for the arena's ack */
static int nskip;

static uint32_t now_s(void) { return (uint32_t) (esp_timer_get_time() / 1000000); }
static bool cfg_ok(void) { return enabled && url[0] && token[0]; }

static void set_error(const char *s)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(last_error, sizeof last_error, "%s", s ? s : "");
    xSemaphoreGive(lock);
}

static void load_cfg(void)
{
    settings_get_str("arena_url", url, sizeof url);
    settings_get_str("arena_tok", token, sizeof token);
    enabled = settings_get_i32("arena_on", 0) != 0;
    if (settings_get_i32("arena_resync", 0)) {       /* another arena: it has none of our sessions */
        store_clear_synced_all();
        settings_set_i32("arena_resync", 0);
    }
}

/* ---- receive (websocket task) -------------------------------------------- */
static void handle_msg(const char *json)
{
    cJSON *m = cJSON_Parse(json);
    if (!m) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(m, "type"));
    if (!type) { cJSON_Delete(m); return; }

    if (!strcmp(type, "welcome")) {
        const char *dn = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(m, "athlete"), "display_name"));
        xSemaphoreTake(lock, portMAX_DELAY);
        snprintf(athlete, sizeof athlete, "%s", dn ? dn : "");
        last_error[0] = 0;
        xSemaphoreGive(lock);
        nskip = 0;
        connected = true;
        backfill_pending = true;
        ESP_LOGI(TAG, "up as %s", athlete);
    } else if (!strcmp(type, "ack")) {
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(m, "session"));
        if (s && strlen(s) < 24 && cJSON_IsTrue(cJSON_GetObjectItem(m, "stored"))) store_set_synced(s, true);
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
    }
    cJSON_Delete(m);
}

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_websocket_event_data_t *e = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        char tok[200], hello[320];
        json_esc(tok, sizeof tok, token);
        int n = snprintf(hello, sizeof hello,
            "{\"type\":\"hello\",\"token\":\"%s\",\"agent\":\"waterrower-esp/%s\"}", tok, FW_VERSION);
        esp_websocket_client_send_text(client, hello, n, pdMS_TO_TICKS(5000));
        break;
    }
    case WEBSOCKET_EVENT_ERROR:
        if (!last_error[0]) set_error("connection failed");
        /* fall through */
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_FINISH:
        connected = false;
        backfill_pending = false;
        dropped = true;                     /* the uplink task reconnects, with backoff */
        live_set_race(NULL);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (e->op_code == 0x8) {            /* close frame: status code + reason */
            if (e->data_len >= 2) {
                char why[64];
                int code = ((uint8_t) e->data_ptr[0] << 8) | (uint8_t) e->data_ptr[1];
                snprintf(why, sizeof why, "closed by arena (%d) %.*s", code,
                         e->data_len - 2 > 30 ? 30 : e->data_len - 2, e->data_ptr + 2);
                set_error(why);
                /* Refused (4401/4403: bad token) or locked out (4429). Knocking
                   again every few seconds only extends the lockout, which is
                   per address and would hit every rower in the house. */
                if (code == 4401 || code == 4403 || code == 4429) hold_s = 20 * 60;
            }
            break;
        }
        if (e->op_code != 0x1 && e->op_code != 0x0) break;       /* text only */
        if (!rx || e->payload_len <= 0 || e->payload_len >= RX_MAX) break;
        if (e->payload_offset < 0 || e->data_len < 0 || e->payload_offset + e->data_len > e->payload_len) break;
        memcpy(rx + e->payload_offset, e->data_ptr, e->data_len);
        if (e->payload_offset + e->data_len == e->payload_len) {
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
    dropped = false;
    live_set_race(NULL);
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
        .task_stack = 8192,
        .disable_auto_reconnect = true,          /* reconnecting is ours: it backs off */
        .network_timeout_ms = 15000,
        .ping_interval_sec = 20,
        .pingpong_timeout_sec = 45,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    client = esp_websocket_client_init(&cfg);
    if (!client) { set_error("init failed"); dropped = true; return; }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    ESP_LOGI(TAG, "connecting to %s", uri);
    if (esp_websocket_client_start(client) != ESP_OK) dropped = true;
}

/* ---- sending -------------------------------------------------------------- */
static bool send_text(const char *s, int n)
{
    if (!client || !connected) return false;
    if (esp_websocket_client_send_text(client, s, n, pdMS_TO_TICKS(5000)) < 0) return false;
    last_sent_s = now_s();
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
        send_text(b, n);
    } else {
        n = snprintf(b, sizeof b,
            "{\"type\":\"summary\",\"session\":\"%s\",\"started_at\":%u,\"distance_m\":%u,"
            "\"duration_s\":%u,\"strokes\":%u,\"avg_speed_ms\":%.2f,\"avg_spm\":%.1f}",
            m->sid, (unsigned) m->sum.started_at, (unsigned) m->sum.distance_m,
            (unsigned) m->sum.duration_s, (unsigned) m->sum.strokes, m->sum.avg_speed_ms, m->sum.avg_spm);
        /* Only a session streamed live from its first sample is complete over
           there; anything else stays unsynced and is backfilled in full. */
        if (send_text(b, n) && m->started) store_set_synced(m->sid, true);
    }
}

static bool skipped(const char *id)
{
    for (int i = 0; i < nskip; i++) if (!strcmp(skip[i], id)) return true;
    return false;
}

/* Drop the entries the arena has acknowledged in the meantime. */
static void compact_skip(void)
{
    int k = 0;
    for (int i = 0; i < nskip; i++) {
        sum_t s; bool has;
        if (store_read_summary(skip[i], &s, &has) && has && !s.synced) {
            if (k != i) strcpy(skip[k], skip[i]);
            k++;
        }
    }
    nskip = k;
}

/* Stream one finished, unsynced session as a single fragmented message.
   It is marked synced when the arena acknowledges it. */
static void backfill_one(void)
{
    live_t lv;
    live_get(&lv);
    if (lv.session_active) return;                           /* live data first */
    if (nskip == SKIP_MAX) {
        compact_skip();
        if (nskip == SKIP_MAX) { backfill_pending = false; return; }   /* no acks: try again on the next connect */
    }

    char (*ids)[24];
    int n = store_list_ids(&ids);
    char id[24] = "";
    sum_t s;
    for (int i = n - 1; i >= 0 && !id[0]; i--) {             /* oldest first */
        bool has;
        if (skipped(ids[i])) continue;
        if (store_read_summary(ids[i], &s, &has) && has && s.ended_at && !s.synced) strcpy(id, ids[i]);
    }
    free(ids);
    if (!id[0]) { backfill_pending = false; return; }        /* nothing left */

    FILE *f = store_open_samples(id);
    long cnt = 0;
    if (f) {
        fseek(f, 0, SEEK_END);
        cnt = ftell(f) / (long) sizeof(sample_t);
        rewind(f);
    }
    if (!f || cnt <= 0 || cnt > BACKFILL_MAX_SAMPLES) {      /* the arena would refuse it every time */
        if (f) fclose(f);
        ESP_LOGW(TAG, "%s cannot be sent to the arena (%ld samples)", id, cnt);
        store_set_synced(id, true);
        return;
    }

    ESP_LOGI(TAG, "backfilling %s (%ld samples)", id, cnt);
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
    while (ok && connected && (got = fread(b, sizeof b[0], 32, f)) > 0) {
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
    if (ok) ok = connected && esp_websocket_client_send_cont_msg(client, "]}", 2, pdMS_TO_TICKS(5000)) >= 0;
    if (ok) ok = esp_websocket_client_send_fin(client, pdMS_TO_TICKS(5000)) >= 0;
    if (ok) {
        last_sent_s = now_s();
        strcpy(skip[nskip++], id);                           /* until the ack arrives */
    } else {
        ESP_LOGW(TAG, "backfill of %s failed", id);
        dropped = true;                                      /* a broken fragment poisons the stream: start over */
    }
    vTaskDelay(pdMS_TO_TICKS(500));                          /* leave room for live data */
}

static void uplink_task(void *arg)
{
    umsg_t m;
    uint32_t retry_s = RETRY_MIN_S, retry_at = 0;
    while (1) {
        if (reconf) {
            reconf = false;
            destroy_client();
            load_cfg();
            set_error("");
            hold_s = 0;
            retry_s = RETRY_MIN_S;
            retry_at = 0;
        }
        if (dropped) {                                       /* connection lost or refused */
            destroy_client();
            retry_at = now_s() + (hold_s ? hold_s : retry_s);
            hold_s = 0;
            retry_s = retry_s * 2 > RETRY_MAX_S ? RETRY_MAX_S : retry_s * 2;
        }
        if (!client) {
            if (cfg_ok() && wifi_mgr_connected() && now_s() >= retry_at) create_client();
            else { vTaskDelay(pdMS_TO_TICKS(500)); xQueueReset(q); }
            continue;
        }
        if (connected) retry_s = RETRY_MIN_S;
        if (xQueueReceive(q, &m, pdMS_TO_TICKS(200)) == pdTRUE) { if (connected) send_queued(&m); }
        else if (pong_pending) { pong_pending = false; send_text("{\"type\":\"pong\"}", 15); }
        else if (backfill_pending && connected) backfill_one();
    }
}

void uplink_reconfigure(void) { reconf = true; }

/* false: this sample did not go out, so the session is not complete over there. */
bool uplink_sample(const char *sid, uint32_t started_at, const sample_t *s)
{
    if (!q || !connected) return false;
    umsg_t m = { .kind = 0, .started = started_at, .s = *s };
    snprintf(m.sid, sizeof m.sid, "%s", sid);
    return xQueueSend(q, &m, 0) == pdTRUE;
}

void uplink_summary(const sum_t *s, bool complete)
{
    if (!q) return;
    if (!connected || !complete) { backfill_pending = connected; return; }   /* the backfill delivers it */
    umsg_t m = { .kind = 1, .started = 1, .sum = *s };
    snprintf(m.sid, sizeof m.sid, "%s", s->id);
    if (xQueueSend(q, &m, pdMS_TO_TICKS(200)) != pdTRUE) backfill_pending = true;
}

bool uplink_connected(void) { return connected; }

int uplink_status_json(char *buf, size_t len)
{
    char u[330], a[200], e[200];
    xSemaphoreTake(lock, portMAX_DELAY);
    json_esc(u, sizeof u, url);
    json_esc(a, sizeof a, connected ? athlete : "");
    json_esc(e, sizeof e, last_error);
    xSemaphoreGive(lock);
    return snprintf(buf, len,
        "{\"url\":\"%s\",\"enabled\":%s,\"token_set\":%s,\"connected\":%s,\"athlete\":\"%s\",\"error\":\"%s\",\"last_sent\":%u}",
        u, enabled ? "true" : "false", token[0] ? "true" : "false", connected ? "true" : "false",
        a, e, (unsigned) last_sent_s);
}

void uplink_init(void)
{
    lock = xSemaphoreCreateMutex();
    rx = malloc(RX_MAX);
    q = xQueueCreate(120, sizeof(umsg_t));
    load_cfg();
    xTaskCreate(uplink_task, "uplink", 8192, NULL, 4, NULL);
}
