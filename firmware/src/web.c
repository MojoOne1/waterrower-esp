/* HTTP server: the tracker UI plus the same /api the old Docker tracker offered. */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web";

#include "web_assets.h"

static httpd_handle_t srv;

/* ---- helpers ------------------------------------------------------------ */
static esp_err_t send_static(httpd_req_t *r, const char *type, const unsigned char *d, unsigned int n)
{
    httpd_resp_set_type(r, type);
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, (const char *) d, n);
}

static esp_err_t send_json(httpd_req_t *r, const char *body)
{
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, body);
}

static esp_err_t fail(httpd_req_t *r, const char *status, const char *detail)
{
    char b[160];
    snprintf(b, sizeof b, "{\"detail\":\"%s\"}", detail);
    httpd_resp_set_status(r, status);
    return send_json(r, b);
}

static cJSON *read_json(httpd_req_t *r)
{
    if (r->content_len == 0 || r->content_len > 1024) return NULL;
    char b[1025];
    int got = 0;
    while (got < (int) r->content_len) {
        int n = httpd_req_recv(r, b + got, r->content_len - got);
        if (n <= 0) return NULL;
        got += n;
    }
    b[got] = 0;
    return cJSON_Parse(b);
}

static const char *jstr(cJSON *o, const char *k, const char *def)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return s ? s : def;
}

static bool valid_id(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 23) return false;
    for (; *s; s++) if (!((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '-')) return false;
    return true;
}

/* "/api/sessions/<id>[/export.csv]" -> id */
static bool uri_session_id(const char *uri, char *id, bool *csv)
{
    const char *p = uri + strlen("/api/sessions/");
    const char *end = strchr(p, '/');
    size_t n = end ? (size_t) (end - p) : strlen(p);
    const char *q = memchr(p, '?', n);
    if (q) n = q - p;
    if (n == 0 || n > 23) return false;
    memcpy(id, p, n); id[n] = 0;
    *csv = end && strstr(end, "export.csv");
    return valid_id(id);
}

/* ---- static ------------------------------------------------------------ */
static esp_err_t h_index(httpd_req_t *r) { return send_static(r, "text/html", asset_index_html, asset_index_html_len); }
static esp_err_t h_setup(httpd_req_t *r) { return send_static(r, "text/html", asset_setup_html, asset_setup_html_len); }
static esp_err_t h_js(httpd_req_t *r)    { return send_static(r, "application/javascript", asset_app_js, asset_app_js_len); }
static esp_err_t h_css(httpd_req_t *r)   { return send_static(r, "text/css", asset_style_css, asset_style_css_len); }

static esp_err_t h_probe(httpd_req_t *r)
{
    if (wifi_mgr_ap_active() && !wifi_mgr_connected()) {          /* captive portal */
        httpd_resp_set_status(r, "302 Found");
        httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/setup");
        return httpd_resp_send(r, NULL, 0);
    }
    httpd_resp_set_status(r, "404 Not Found");
    return httpd_resp_sendstr(r, "not found");
}

static esp_err_t h_404(httpd_req_t *r, httpd_err_code_t e) { return h_probe(r); }

/* ---- live / SSE -------------------------------------------------------------- */
static char snapbuf[1800];

static esp_err_t h_live(httpd_req_t *r)
{
    live_snapshot_json(snapbuf, sizeof snapbuf);
    return send_json(r, snapbuf);
}

#define SSE_MAX 4
static httpd_req_t *sse[SSE_MAX];
static bool sse_fresh[SSE_MAX];

static esp_err_t h_stream(httpd_req_t *r)
{
    for (int i = 0; i < SSE_MAX; i++) {
        if (!sse[i]) {
            httpd_req_t *copy = NULL;
            if (httpd_req_async_handler_begin(r, &copy) != ESP_OK) return ESP_FAIL;
            sse_fresh[i] = true;
            sse[i] = copy;            /* published last: the SSE task keys on it */
            return ESP_OK;
        }
    }
    return fail(r, "503 Service Unavailable", "too many live viewers");
}

static void sse_task(void *arg)
{
    static char chunk[1900];
    uint32_t last_seq = 0, last_tx = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(250));
        uint32_t now = (uint32_t) (esp_timer_get_time() / 1000000);
        bool changed = live_seq() != last_seq;
        bool any = false;
        for (int i = 0; i < SSE_MAX; i++) any |= sse[i] != NULL;
        if (!any) { last_seq = live_seq(); continue; }
        int n = 0;
        if (changed || now - last_tx >= 15) {
            last_seq = live_seq();
            last_tx = now;
            int j = snprintf(chunk, sizeof chunk, "data: ");
            j += live_snapshot_json(chunk + j, sizeof chunk - j - 4);
            n = j + snprintf(chunk + j, 4, "\n\n");
        }
        for (int i = 0; i < SSE_MAX; i++) {
            if (!sse[i]) continue;
            esp_err_t e = ESP_OK;
            if (sse_fresh[i]) {
                httpd_resp_set_type(sse[i], "text/event-stream");
                httpd_resp_set_hdr(sse[i], "Cache-Control", "no-cache");
                live_snapshot_json(snapbuf, sizeof snapbuf);
                static char first[1900];
                int m = snprintf(first, sizeof first, "data: %s\n\n", snapbuf);
                e = httpd_resp_send_chunk(sse[i], first, m);
                sse_fresh[i] = false;
            } else if (n) {
                e = httpd_resp_send_chunk(sse[i], chunk, n);
            }
            if (e != ESP_OK) {
                httpd_req_async_handler_complete(sse[i]);
                sse[i] = NULL;
            }
        }
    }
}

/* ---- sessions ---------------------------------------------------------------- */
static esp_err_t h_sessions(httpd_req_t *r)
{
    static char ids[256][24];
    int n = store_list_ids(ids, 256);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_send_chunk(r, "[", 1);
    char buf[700];
    live_t lv; live_get(&lv);
    for (int i = 0; i < n; i++) {
        sum_t s; bool has;
        if (!store_read_summary(ids[i], &s, &has)) continue;
        if (!has) s.ended_at = 0;                        /* still open */
        int l = 0;
        if (i) buf[l++] = ',';
        l += sum_to_json(&s, true, buf + l, sizeof buf - l);
        httpd_resp_send_chunk(r, buf, l);
    }
    httpd_resp_send_chunk(r, "]", 1);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static int sample_json(const char *id, const sample_t *s, char *b, size_t n)
{
    return snprintf(b, n,
        "{\"session_id\":\"%s\",\"ts\":%u,\"distance_m\":%u,\"speed_ms\":%.2f,\"stroke_rate\":%u,"
        "\"strokes\":%u,\"duration_s\":%u,\"watts\":%u}",
        id, (unsigned) s->ts, s->distance_m, s->speed_cms / 100.0, s->spm, s->strokes, s->duration_s, s->watts);
}

static esp_err_t h_session(httpd_req_t *r)
{
    char id[24]; bool csv;
    if (!uri_session_id(r->uri, id, &csv)) return fail(r, "400 Bad Request", "bad id");

    if (r->method == HTTP_DELETE) {
        live_t lv; live_get(&lv);
        if (lv.session_active && !strcmp(lv.session_id, id)) return fail(r, "409 Conflict", "session is running");
        store_delete(id);
        return send_json(r, "{\"ok\":true}");
    }
    FILE *f = store_open_samples(id);
    if (!f) return fail(r, "404 Not Found", "Session not found");

    char b[300];
    int l;
    if (csv) {
        httpd_resp_set_type(r, "text/csv");
        snprintf(b, sizeof b, "attachment; filename=\"%s.csv\"", id);
        httpd_resp_set_hdr(r, "Content-Disposition", b);
        httpd_resp_send_chunk(r, "session_id,ts,distance_m,speed_ms,stroke_rate,strokes,duration_s,watts\r\n", HTTPD_RESP_USE_STRLEN);
    } else {
        sum_t s; bool has;
        store_read_summary(id, &s, &has);
        if (!has) s.ended_at = 0;
        httpd_resp_set_type(r, "application/json");
        httpd_resp_send_chunk(r, "{\"session\":", HTTPD_RESP_USE_STRLEN);
        char sb[600];
        l = sum_to_json(&s, false, sb, sizeof sb);
        httpd_resp_send_chunk(r, sb, l);
        httpd_resp_send_chunk(r, ",\"samples\":[", HTTPD_RESP_USE_STRLEN);
    }
    static char out[3000];
    sample_t buf[24];
    size_t got; bool first = true;
    while ((got = fread(buf, sizeof buf[0], 24, f)) > 0) {
        int o = 0;
        for (size_t k = 0; k < got; k++) {
            if (csv) {
                o += snprintf(out + o, sizeof out - o, "%s,%u,%u,%.2f,%u,%u,%u,%u\r\n", id, (unsigned) buf[k].ts,
                              buf[k].distance_m, buf[k].speed_cms / 100.0, buf[k].spm, buf[k].strokes,
                              buf[k].duration_s, buf[k].watts);
            } else {
                if (!first) out[o++] = ',';
                first = false;
                o += sample_json(id, &buf[k], out + o, sizeof out - o);
            }
        }
        if (httpd_resp_send_chunk(r, out, o) != ESP_OK) { fclose(f); return ESP_FAIL; }
    }
    fclose(f);
    if (!csv) httpd_resp_send_chunk(r, "]}", 2);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t h_end(httpd_req_t *r)   { rower_cmd(ROWER_CMD_END);   return send_json(r, "{\"ok\":true}"); }
static esp_err_t h_reset(httpd_req_t *r) { rower_cmd(ROWER_CMD_RESET); return send_json(r, "{\"ok\":true}"); }

static esp_err_t h_version(httpd_req_t *r)
{
    return send_json(r, "{\"version\":\"" FW_VERSION "\",\"commit\":\"\"}");
}

/* ---- settings ---------------------------------------------------------------- */
static esp_err_t h_arena(httpd_req_t *r)
{
    static char b[400];
    if (r->method == HTTP_POST) {
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        char url[160];
        snprintf(url, sizeof url, "%s", jstr(j, "arena_url", ""));
        size_t l = strlen(url);
        while (l && (url[l - 1] == '/' || url[l - 1] == ' ')) url[--l] = 0;
        char tok[80];
        const char *t = jstr(j, "arena_token", "");
        if (!strcmp(t, "••••••")) settings_get_str("arena_tok", tok, sizeof tok);
        else snprintf(tok, sizeof tok, "%s", t);
        bool on = cJSON_IsTrue(cJSON_GetObjectItem(j, "arena_enabled"));
        cJSON_Delete(j);
        if (on && (!url[0] || !tok[0])) return fail(r, "400 Bad Request", "Arena needs both an address and a token");
        settings_set_str("arena_url", url);
        settings_set_str("arena_tok", tok);
        settings_set_i32("arena_on", on);
        uplink_reconfigure();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    uplink_status_json(b, sizeof b);
    return send_json(r, b);
}

static esp_err_t h_wifi(httpd_req_t *r)
{
    static char b[600];
    if (r->method == HTTP_POST) {
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        char ssid[33], pass[65];
        snprintf(ssid, sizeof ssid, "%s", jstr(j, "ssid", ""));
        snprintf(pass, sizeof pass, "%s", jstr(j, "password", ""));
        cJSON_Delete(j);
        if (!ssid[0]) return fail(r, "400 Bad Request", "SSID missing");
        wifi_mgr_set_credentials(ssid, pass);
        return send_json(r, "{\"ok\":true}");
    }
    wifi_mgr_status_json(b, sizeof b);
    return send_json(r, b);
}

static esp_err_t h_scan(httpd_req_t *r)
{
    static char b[2200];
    wifi_mgr_scan_json(b, sizeof b);
    return send_json(r, b);
}

static esp_err_t h_system(httpd_req_t *r)
{
    static char b[600];
    if (r->method == HTTP_POST) {
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        const char *tz = jstr(j, "tz", "");
        if (tz[0] && strlen(tz) < 63) { settings_set_str("tz", tz); wifi_mgr_apply_tz(); }
        cJSON *t = cJSON_GetObjectItem(j, "session_timeout_min");
        if (cJSON_IsNumber(t)) { settings_set_i32("sess_to", (int) t->valuedouble); rower_set_timeout_min((int) t->valuedouble); }
        cJSON_Delete(j);
    }
    wifi_mgr_status_json(b, sizeof b);
    return send_json(r, b);
}

/* The browser knows the time even when there is no NTP (e.g. no internet). */
static esp_err_t h_time(httpd_req_t *r)
{
    cJSON *j = read_json(r);
    if (!j) return fail(r, "400 Bad Request", "invalid body");
    cJSON *e = cJSON_GetObjectItem(j, "epoch");
    if (cJSON_IsNumber(e) && time(NULL) < 1700000000 && e->valuedouble > 1700000000) {
        struct timeval tv = { .tv_sec = (time_t) e->valuedouble };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock set from browser");
    }
    cJSON_Delete(j);
    return send_json(r, "{\"ok\":true}");
}

static void restart_task(void *a) { vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart(); }

static esp_err_t h_ota(httpd_req_t *r)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t h;
    if (!p || esp_ota_begin(p, OTA_SIZE_UNKNOWN, &h) != ESP_OK) return fail(r, "500 Internal Server Error", "OTA begin failed");
    static char buf[2048];
    int left = r->content_len;
    bool ok = left > 0;
    while (left > 0) {
        int n = httpd_req_recv(r, buf, left < (int) sizeof buf ? left : (int) sizeof buf);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0 || esp_ota_write(h, buf, n) != ESP_OK) { ok = false; break; }
        left -= n;
    }
    if (!ok || esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(p) != ESP_OK) {
        esp_ota_abort(h);
        return fail(r, "400 Bad Request", "invalid firmware image");
    }
    send_json(r, "{\"ok\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* ---- start --------------------------------------------------------------------- */
void web_start(void)
{
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 28;
    c.max_open_sockets = 8;
    c.lru_purge_enable = true;
    c.stack_size = 8192;
    c.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&srv, &c));

#define R(u, m, h) { .uri = u, .method = m, .handler = h, .user_ctx = NULL }
    const httpd_uri_t routes[] = {
        R("/", HTTP_GET, h_index), R("/setup", HTTP_GET, h_setup),
        R("/static/app.js*", HTTP_GET, h_js), R("/static/style.css*", HTTP_GET, h_css),
        R("/api/version", HTTP_GET, h_version),
        R("/api/live", HTTP_GET, h_live), R("/api/stream", HTTP_GET, h_stream),
        R("/api/session/end", HTTP_POST, h_end), R("/api/session/reset", HTTP_POST, h_reset),
        R("/api/sessions", HTTP_GET, h_sessions),
        R("/api/sessions/*", HTTP_GET, h_session), R("/api/sessions/*", HTTP_DELETE, h_session),
        R("/api/arena", HTTP_GET, h_arena), R("/api/arena", HTTP_POST, h_arena),
        R("/api/wifi", HTTP_GET, h_wifi), R("/api/wifi", HTTP_POST, h_wifi),
        R("/api/wifi/scan", HTTP_GET, h_scan),
        R("/api/system", HTTP_GET, h_system), R("/api/system", HTTP_POST, h_system),
        R("/api/time", HTTP_POST, h_time), R("/api/ota", HTTP_POST, h_ota),
        /* captive-portal probes of Android / iOS / Windows */
        R("/generate_204", HTTP_GET, h_probe), R("/hotspot-detect.html", HTTP_GET, h_probe),
        R("/connecttest.txt", HTTP_GET, h_probe), R("/ncsi.txt", HTTP_GET, h_probe),
    };
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) httpd_register_uri_handler(srv, &routes[i]);
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, h_404);
    xTaskCreate(sse_task, "sse", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "web server up");
}
