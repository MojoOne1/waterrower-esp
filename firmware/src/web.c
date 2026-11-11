/* HTTP server: the tracker UI plus the same /api the old Docker tracker offered.
 *
 * The device sits on a home network without TLS, so the API defends itself
 * against the browser-borne attacks that reach such devices:
 *   - a request must address the device by an IP literal or a local name
 *     (DNS rebinding),
 *   - a changing request must come from the device's own page (Origin) and
 *     carry a non-form content type where it has a body (CSRF),
 *   - changing requests need the device password once one has been set.
 */
#include "app.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
#include "web_assets.h"

static const char *TAG = "web";
static httpd_handle_t srv;
#define MASK "••••••"

/* ---- responses ------------------------------------------------------------ */
static void common_headers(httpd_req_t *r)
{
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
}

static esp_err_t send_static(httpd_req_t *r, const char *type, const unsigned char *d, unsigned int n)
{
    common_headers(r);
    httpd_resp_set_type(r, type);
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, (const char *) d, n);
}

static esp_err_t send_json(httpd_req_t *r, const char *body)
{
    common_headers(r);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, body);
}

static esp_err_t ok(httpd_req_t *r) { return send_json(r, "{\"ok\":true}"); }

/* `detail` is always a literal from this file. */
static esp_err_t fail(httpd_req_t *r, const char *status, const char *detail)
{
    char b[200];
    snprintf(b, sizeof b, "{\"detail\":\"%s\"}", detail);
    httpd_resp_set_status(r, status);
    return send_json(r, b);
}

/* ---- request guards --------------------------------------------------------- */
static bool hdr(httpd_req_t *r, const char *name, char *out, size_t n)
{
    out[0] = 0;
    return httpd_req_get_hdr_value_str(r, name, out, n) == ESP_OK;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

/* Names under which a browser may legitimately reach this device. A page on
   the internet can point its own domain at our address (DNS rebinding), but
   it cannot make the browser send an IP literal or a name from a reserved
   local zone as the Host. */
static bool host_allowed(const char *host)
{
    char h[72];
    snprintf(h, sizeof h, "%s", host);
    char *colon = strrchr(h, ':');
    if (colon && !strchr(h, ']')) *colon = 0;                  /* strip the port */
    if (!h[0]) return false;

    bool ip = true;
    int dots = 0;
    for (const char *p = h; *p; p++) {
        if (*p == '.') dots++;
        else if (!isdigit((unsigned char) *p)) ip = false;
    }
    if (ip && dots == 3) return true;

    if (!strcasecmp(h, "waterrower") || !strcasecmp(h, "waterrower.local")) return true;
    if (!strncasecmp(h, "waterrower.", 11) &&
        (ends_with(h, ".lan") || ends_with(h, ".home.arpa") || ends_with(h, ".internal"))) return true;
    return false;
}

/* ---- device password (salted SHA-256 in NVS: "<salt hex>$<hash hex>") -------- */
static void hex(const uint8_t *in, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", in[i]);
}

static bool pw_hash(const char *salt_hex, const char *pw, char out_hex[65])
{
    uint8_t buf[160], digest[32];
    size_t sl = strlen(salt_hex), pl = strlen(pw), dl = 0;
    if (sl + pl > sizeof buf) return false;
    memcpy(buf, salt_hex, sl);
    memcpy(buf + sl, pw, pl);
    if (psa_hash_compute(PSA_ALG_SHA_256, buf, sl + pl, digest, sizeof digest, &dl) != PSA_SUCCESS || dl != 32) return false;
    hex(digest, 32, out_hex);
    return true;
}

static bool pw_is_set(void)
{
    char s[120];
    return settings_get_str("adm", s, sizeof s);
}

static void pw_store(const char *pw)
{
    if (!pw[0]) { settings_set_str("adm", ""); return; }
    uint8_t salt[16];
    char salt_hex[33], h[65], rec[120];
    esp_fill_random(salt, sizeof salt);
    hex(salt, sizeof salt, salt_hex);
    if (!pw_hash(salt_hex, pw, h)) return;
    snprintf(rec, sizeof rec, "%s$%s", salt_hex, h);
    settings_set_str("adm", rec);
}

static bool pw_check(const char *pw)
{
    char rec[120], h[65];
    if (!settings_get_str("adm", rec, sizeof rec)) return true;
    char *d = strchr(rec, '$');
    if (!d) return false;
    *d++ = 0;
    if (!pw_hash(rec, pw, h) || strlen(d) != 64) return false;
    unsigned diff = 0;                                         /* constant time */
    for (int i = 0; i < 64; i++) diff |= (unsigned char) h[i] ^ (unsigned char) d[i];
    return diff == 0;
}

static bool authorized(httpd_req_t *r)
{
    if (!pw_is_set()) return true;
    char a[200];
    if (!hdr(r, "Authorization", a, sizeof a) || strncasecmp(a, "Basic ", 6)) return false;
    unsigned char dec[150];
    size_t n = 0;
    if (mbedtls_base64_decode(dec, sizeof dec - 1, &n, (const unsigned char *) a + 6, strlen(a + 6)) != 0) return false;
    dec[n] = 0;
    char *colon = strchr((char *) dec, ':');                   /* any user name; the password counts */
    return colon && pw_check(colon + 1);
}

typedef enum { G_READ, G_WRITE, G_WRITE_JSON, G_WRITE_BIN } guard_t;

/* True when the request may proceed; otherwise the answer has been sent. */
static bool guard(httpd_req_t *r, guard_t g)
{
    char host[80], v[160];
    if (hdr(r, "Host", host, sizeof host) && !host_allowed(host)) {
        fail(r, "403 Forbidden", "Open this device by its IP address or as waterrower.local");
        return false;
    }
    if (g == G_READ) return true;

    if (hdr(r, "Origin", v, sizeof v)) {                       /* sent by browsers on every POST/DELETE */
        const char *o = !strncasecmp(v, "http://", 7) ? v + 7 : NULL;
        if (!o || strcasecmp(o, host)) { fail(r, "403 Forbidden", "Cross-site request refused"); return false; }
    }
    if (hdr(r, "Sec-Fetch-Site", v, sizeof v) && strcasecmp(v, "same-origin") && strcasecmp(v, "none")) {
        fail(r, "403 Forbidden", "Cross-site request refused");
        return false;
    }
    /* A content type a plain HTML form cannot send, so the browser has to ask first. */
    if (g == G_WRITE_JSON || g == G_WRITE_BIN) {
        const char *want = g == G_WRITE_JSON ? "application/json" : "application/octet-stream";
        if (!hdr(r, "Content-Type", v, sizeof v) || strncasecmp(v, want, strlen(want))) {
            fail(r, "415 Unsupported Media Type", "Unexpected content type");
            return false;
        }
    }
    if (!authorized(r)) {
        vTaskDelay(pdMS_TO_TICKS(1000));                       /* at most one guess a second */
        httpd_resp_set_hdr(r, "WWW-Authenticate", "Basic realm=\"WaterRower\", charset=\"UTF-8\"");
        fail(r, "401 Unauthorized", "Device password required");
        return false;
    }
    return true;
}

/* ---- request bodies ----------------------------------------------------------- */
static cJSON *read_json(httpd_req_t *r)
{
    if (r->content_len == 0 || r->content_len > 1024) return NULL;
    char b[1025];
    int got = 0, timeouts = 0;
    while (got < (int) r->content_len) {
        int n = httpd_req_recv(r, b + got, r->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (n <= 0) return NULL;
        got += n;
    }
    b[got] = 0;
    cJSON *j = cJSON_Parse(b);
    if (j && !cJSON_IsObject(j)) { cJSON_Delete(j); j = NULL; }
    return j;
}

/* A string member, or NULL when it is absent, not a string, or does not fit in `max` bytes. */
static const char *jstr(cJSON *o, const char *k, size_t max)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return s && strlen(s) < max ? s : NULL;
}

static bool printable(const char *s)
{
    for (; *s; s++) if ((unsigned char) *s < 0x20 || *s == 0x7f) return false;
    return true;
}

static bool topic_chars(const char *s)
{
    if (!s[0]) return false;
    for (; *s; s++) if (!isalnum((unsigned char) *s) && !strchr("_-/", *s)) return false;
    return true;
}

static bool valid_id(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 23) return false;
    for (; *s; s++) if (!isalnum((unsigned char) *s) && *s != '-') return false;
    return true;
}

/* "/api/sessions/<id>[/export.csv]" -> id */
static bool uri_session_id(const char *uri, char *id, bool *csv)
{
    const char *p = uri + strlen("/api/sessions/");
    size_t n = strcspn(p, "/?");
    if (n == 0 || n > 23) return false;
    memcpy(id, p, n); id[n] = 0;
    *csv = !strncmp(p + n, "/export.csv", 11);
    return valid_id(id);
}

/* ---- static ------------------------------------------------------------ */
static esp_err_t h_index(httpd_req_t *r)
{
    httpd_resp_set_hdr(r, "Content-Security-Policy", asset_index_csp);
    return send_static(r, "text/html", asset_index_html, asset_index_html_len);
}
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
#define SNAP_MAX 1800

static esp_err_t h_live(httpd_req_t *r)
{
    if (!guard(r, G_READ)) return ESP_OK;
    static char b[SNAP_MAX];                      /* httpd task only */
    live_snapshot_json(b, sizeof b);
    return send_json(r, b);
}

#define SSE_MAX 4
static httpd_req_t *volatile sse[SSE_MAX];
static volatile bool sse_fresh[SSE_MAX];

static esp_err_t h_stream(httpd_req_t *r)
{
    if (!guard(r, G_READ)) return ESP_OK;
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
    static char chunk[SNAP_MAX + 16];             /* this task only */
    uint32_t last_seq = 0, last_tx = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(250));
        uint32_t now = (uint32_t) (esp_timer_get_time() / 1000000);
        bool any = false, fresh = false;
        for (int i = 0; i < SSE_MAX; i++) { any |= sse[i] != NULL; fresh |= sse[i] && sse_fresh[i]; }
        if (!any) { last_seq = live_seq(); continue; }
        bool due = live_seq() != last_seq || now - last_tx >= 15;
        if (!due && !fresh) continue;
        last_seq = live_seq();
        int n = snprintf(chunk, sizeof chunk, "data: ");
        n += live_snapshot_json(chunk + n, SNAP_MAX);
        n += snprintf(chunk + n, sizeof chunk - n, "\n\n");
        if (due) last_tx = now;
        for (int i = 0; i < SSE_MAX; i++) {
            httpd_req_t *req = sse[i];
            if (!req || (!due && !sse_fresh[i])) continue;
            if (sse_fresh[i]) {
                httpd_resp_set_type(req, "text/event-stream");
                httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
                httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
                sse_fresh[i] = false;
            }
            if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
                sse[i] = NULL;
                httpd_req_async_handler_complete(req);
            }
        }
    }
}

/* ---- sessions ---------------------------------------------------------------- */
static esp_err_t h_sessions(httpd_req_t *r)
{
    if (!guard(r, G_READ)) return ESP_OK;
    char (*ids)[24];
    int n = store_list_ids(&ids);
    common_headers(r);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(r, "[", 1);
    char buf[700];
    bool first = true;
    for (int i = 0; i < n && e == ESP_OK; i++) {
        sum_t s; bool has;
        if (!store_read_summary(ids[i], &s, &has)) continue;
        if (!has) s.ended_at = 0;                        /* still open */
        int l = 0;
        if (!first) buf[l++] = ',';
        first = false;
        l += sum_to_json(&s, true, buf + l, sizeof buf - l);
        e = httpd_resp_send_chunk(r, buf, l);
    }
    free(ids);
    if (e != ESP_OK) return ESP_FAIL;
    httpd_resp_send_chunk(r, "]", 1);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t h_session(httpd_req_t *r)
{
    char id[24]; bool csv;
    if (!guard(r, r->method == HTTP_DELETE ? G_WRITE : G_READ)) return ESP_OK;
    if (!uri_session_id(r->uri, id, &csv)) return fail(r, "400 Bad Request", "bad id");

    if (r->method == HTTP_DELETE) {
        live_t lv; live_get(&lv);
        if (lv.session_active && !strcmp(lv.session_id, id)) return fail(r, "409 Conflict", "session is running");
        store_delete(id);
        return ok(r);
    }
    FILE *f = store_open_samples(id);
    if (!f) return fail(r, "404 Not Found", "Session not found");

    char b[120];
    common_headers(r);
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (csv) {
        httpd_resp_set_type(r, "text/csv");
        snprintf(b, sizeof b, "attachment; filename=\"%s.csv\"", id);
        httpd_resp_set_hdr(r, "Content-Disposition", b);
        httpd_resp_send_chunk(r, "session_id,ts,distance_m,speed_ms,stroke_rate,strokes,duration_s,watts\r\n", HTTPD_RESP_USE_STRLEN);
    } else {
        sum_t s; bool has;
        char sb[400];
        if (!store_read_summary(id, &s, &has)) { fclose(f); return fail(r, "404 Not Found", "Session not found"); }
        if (!has) s.ended_at = 0;
        httpd_resp_set_type(r, "application/json");
        httpd_resp_send_chunk(r, "{\"session\":", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(r, sb, sum_to_json(&s, false, sb, sizeof sb));
        httpd_resp_send_chunk(r, ",\"samples\":[", HTTPD_RESP_USE_STRLEN);
    }
    static char out[3600];                        /* httpd task only; 24 rows of at most ~145 bytes */
    sample_t buf[24];
    size_t got; bool first = true;
    while ((got = fread(buf, sizeof buf[0], 24, f)) > 0) {
        int o = 0;
        for (size_t k = 0; k < got; k++) {
            const sample_t *s = &buf[k];
            if (csv) {
                o += snprintf(out + o, sizeof out - o, "%s,%u,%u,%.2f,%u,%u,%u,%u\r\n", id, (unsigned) s->ts,
                              s->distance_m, s->speed_cms / 100.0, s->spm, s->strokes, s->duration_s, s->watts);
            } else {
                o += snprintf(out + o, sizeof out - o,
                    "%s{\"session_id\":\"%s\",\"ts\":%u,\"distance_m\":%u,\"speed_ms\":%.2f,\"stroke_rate\":%u,"
                    "\"strokes\":%u,\"duration_s\":%u,\"watts\":%u}", first ? "" : ",",
                    id, (unsigned) s->ts, s->distance_m, s->speed_cms / 100.0, s->spm, s->strokes, s->duration_s, s->watts);
                first = false;
            }
        }
        if (httpd_resp_send_chunk(r, out, o) != ESP_OK) { fclose(f); return ESP_FAIL; }
    }
    fclose(f);
    if (!csv) httpd_resp_send_chunk(r, "]}", 2);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t h_end(httpd_req_t *r)   { if (!guard(r, G_WRITE)) return ESP_OK; rower_cmd(ROWER_CMD_END);   return ok(r); }
static esp_err_t h_reset(httpd_req_t *r) { if (!guard(r, G_WRITE)) return ESP_OK; rower_cmd(ROWER_CMD_RESET); return ok(r); }

static esp_err_t h_version(httpd_req_t *r)
{
    if (!guard(r, G_READ)) return ESP_OK;
    return send_json(r, "{\"version\":\"" FW_VERSION "\",\"commit\":\"\"}");
}

/* Lets the page obtain the device password before a large upload. */
static esp_err_t h_auth(httpd_req_t *r) { if (!guard(r, G_WRITE)) return ESP_OK; return ok(r); }

/* ---- settings ---------------------------------------------------------------- */
static bool url_ok(const char *u)
{
    if (!printable(u) || strchr(u, ' ') || strchr(u, '"') || strchr(u, '\\')) return false;
    const char *rest = strstr(u, "://");
    if (rest) {
        if (strncmp(u, "http://", 7) && strncmp(u, "https://", 8) && strncmp(u, "ws://", 5) && strncmp(u, "wss://", 6)) return false;
        rest += 3;
    } else rest = u;
    return rest[0] && !strchr(rest, '@');             /* no credentials in the address */
}

static esp_err_t h_arena(httpd_req_t *r)
{
    static char b[800];
    if (r->method == HTTP_POST) {
        if (!guard(r, G_WRITE_JSON)) return ESP_OK;
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        char url[160] = "", tok[96] = "", old_url[160], old_tok[96];
        const char *u = jstr(j, "arena_url", sizeof url), *t = jstr(j, "arena_token", sizeof tok);
        bool on = cJSON_IsTrue(cJSON_GetObjectItem(j, "arena_enabled"));
        bool bad = !u || !t;
        if (!bad) { snprintf(url, sizeof url, "%s", u); snprintf(tok, sizeof tok, "%s", t); }
        cJSON_Delete(j);
        if (bad) return fail(r, "400 Bad Request", "Address or token too long");
        size_t l = strlen(url);
        while (l && (url[l - 1] == '/' || url[l - 1] == ' ')) url[--l] = 0;
        settings_get_str("arena_url", old_url, sizeof old_url);
        settings_get_str("arena_tok", old_tok, sizeof old_tok);
        if (!strcmp(tok, MASK)) {
            /* The stored token belongs to the stored address. Sending it to a
               different one would hand it to whoever runs that server. */
            if (strcmp(url, old_url)) return fail(r, "400 Bad Request", "Enter the token again when changing the address");
            strcpy(tok, old_tok);
        }
        if (url[0] && !url_ok(url)) return fail(r, "400 Bad Request", "Invalid arena address");
        if (!printable(tok)) return fail(r, "400 Bad Request", "Invalid token");
        if (on && (!url[0] || !tok[0])) return fail(r, "400 Bad Request", "Arena needs both an address and a token");
        if (strcmp(url, old_url) || strcmp(tok, old_tok)) settings_set_i32("arena_resync", 1);
        settings_set_str("arena_url", url);
        settings_set_str("arena_tok", tok);
        settings_set_i32("arena_on", on);
        uplink_reconfigure();
        vTaskDelay(pdMS_TO_TICKS(300));
    } else if (!guard(r, G_READ)) return ESP_OK;
    uplink_status_json(b, sizeof b);
    return send_json(r, b);
}

static esp_err_t h_mqtt(httpd_req_t *r)
{
    static char b[1100];
    if (r->method == HTTP_POST) {
        if (!guard(r, G_WRITE_JSON)) return ESP_OK;
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        char host[100] = "", pre[48] = "waterrower", dpre[32] = "homeassistant", user[64] = "", pass[64] = "";
        const char *s;
        const char *err = NULL;
        bool on = cJSON_IsTrue(cJSON_GetObjectItem(j, "enabled"));
        bool disc = !cJSON_IsFalse(cJSON_GetObjectItem(j, "discovery"));
        cJSON *port = cJSON_GetObjectItem(j, "port");
        int p = cJSON_IsNumber(port) ? (int) port->valuedouble : 1883;

        if ((s = jstr(j, "host", sizeof host)) && printable(s) && !strchr(s, ' ') && !strchr(s, '@')) snprintf(host, sizeof host, "%s", s);
        else if (cJSON_GetObjectItem(j, "host")) err = "Invalid broker address";
        if ((s = jstr(j, "username", sizeof user)) && printable(s)) snprintf(user, sizeof user, "%s", s);
        else if (cJSON_GetObjectItem(j, "username")) err = "Invalid user name";
        if (cJSON_GetObjectItem(j, "prefix")) {
            if ((s = jstr(j, "prefix", sizeof pre)) && topic_chars(s)) snprintf(pre, sizeof pre, "%s", s);
            else err = "Topic prefix: letters, digits, _ - / only";
        }
        if (cJSON_GetObjectItem(j, "discovery_prefix")) {
            if ((s = jstr(j, "discovery_prefix", sizeof dpre)) && topic_chars(s)) snprintf(dpre, sizeof dpre, "%s", s);
            else err = "Discovery prefix: letters, digits, _ - / only";
        } else if (!settings_get_str("mq_disc_pre", dpre, sizeof dpre)) strcpy(dpre, "homeassistant");
        s = jstr(j, "password", sizeof pass);
        if (s && !strcmp(s, MASK)) settings_get_str("mq_pass", pass, sizeof pass);      /* left unchanged */
        else if (s && printable(s)) snprintf(pass, sizeof pass, "%s", s);
        else if (cJSON_GetObjectItem(j, "password")) err = "Invalid password";
        cJSON_Delete(j);

        if (!err && (p < 1 || p > 65535)) err = "Invalid port";
        if (!err && on && !host[0]) err = "Broker address missing";
        if (err) return fail(r, "400 Bad Request", err);
        settings_set_str("mq_host", host); settings_set_str("mq_user", user); settings_set_str("mq_pass", pass);
        settings_set_str("mq_pre", pre); settings_set_str("mq_disc_pre", dpre);
        settings_set_i32("mq_port", p); settings_set_i32("mq_disc", disc); settings_set_i32("mq_on", on);
        mqtt_reconfigure();
        vTaskDelay(pdMS_TO_TICKS(400));
    } else if (!guard(r, G_READ)) return ESP_OK;
    mqtt_status_json(b, sizeof b);
    return send_json(r, b);
}

static int status_json(char *b, size_t n)
{
    int o = wifi_mgr_status_json(b, n);
    if (o > 1 && (size_t) o < n && b[o - 1] == '}')
        o += snprintf(b + o - 1, n - o + 1, ",\"password_set\":%s}", pw_is_set() ? "true" : "false") - 1;
    return o;
}

static esp_err_t h_wifi(httpd_req_t *r)
{
    static char b[900];
    if (r->method == HTTP_POST) {
        if (!guard(r, G_WRITE_JSON)) return ESP_OK;
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        char ssid[33] = "", pass[65] = "";
        const char *s = jstr(j, "ssid", sizeof ssid), *p = jstr(j, "password", sizeof pass);
        bool bad = !s || !s[0] || (cJSON_GetObjectItem(j, "password") && !p);
        if (!bad) { snprintf(ssid, sizeof ssid, "%s", s); snprintf(pass, sizeof pass, "%s", p ? p : ""); }
        cJSON_Delete(j);
        if (bad) return fail(r, "400 Bad Request", "WiFi name missing or too long");
        if (pass[0] && strlen(pass) < 8) return fail(r, "400 Bad Request", "WiFi password needs 8 to 63 characters");
        wifi_mgr_set_credentials(ssid, pass);
        return ok(r);
    }
    if (!guard(r, G_READ)) return ESP_OK;
    status_json(b, sizeof b);
    return send_json(r, b);
}

static esp_err_t h_scan(httpd_req_t *r)
{
    static char b[5600];
    static int64_t last_us;
    if (!guard(r, G_READ)) return ESP_OK;
    /* A scan takes the radio off the channel for seconds: not more than one every 5 s. */
    if (!last_us || esp_timer_get_time() - last_us > 5000000) {
        wifi_mgr_scan_json(b, sizeof b);
        last_us = esp_timer_get_time();
    }
    return send_json(r, b);
}

static esp_err_t h_system(httpd_req_t *r)
{
    static char b[900];
    if (r->method == HTTP_POST) {
        if (!guard(r, G_WRITE_JSON)) return ESP_OK;
        cJSON *j = read_json(r);
        if (!j) return fail(r, "400 Bad Request", "invalid body");
        const char *err = NULL, *s;
        char tz[64] = "", ap[65] = "", pw[100] = "";
        bool has_ap = false, has_pw = false;
        if (cJSON_GetObjectItem(j, "tz")) {
            if ((s = jstr(j, "tz", sizeof tz)) && printable(s)) snprintf(tz, sizeof tz, "%s", s);
            else err = "Invalid time zone";
        }
        if (cJSON_GetObjectItem(j, "ap_password")) {
            s = jstr(j, "ap_password", sizeof ap);
            if (s && strlen(s) >= 8 && printable(s)) { snprintf(ap, sizeof ap, "%s", s); has_ap = true; }
            else err = "Hotspot password needs 8 to 63 characters";
        }
        if (cJSON_GetObjectItem(j, "admin_password")) {            /* "" removes it */
            s = jstr(j, "admin_password", sizeof pw);
            if (s && (!s[0] || strlen(s) >= 8) && !strchr(s, ':') && printable(s)) { snprintf(pw, sizeof pw, "%s", s); has_pw = true; }
            else err = "Device password needs 8 to 99 characters, without a colon";
        }
        cJSON *t = cJSON_GetObjectItem(j, "session_timeout_min");
        int to = cJSON_IsNumber(t) ? (int) t->valuedouble : 0;
        if (cJSON_IsNumber(t) && (to < 1 || to > 60)) err = "Session timeout: 1 to 60 minutes";
        cJSON_Delete(j);
        if (err) return fail(r, "400 Bad Request", err);
        if (tz[0]) { settings_set_str("tz", tz); wifi_mgr_apply_tz(); }
        if (to) { settings_set_i32("sess_to", to); rower_set_timeout_min(to); }
        if (has_ap) wifi_mgr_set_ap_password(ap);
        if (has_pw) pw_store(pw);
    } else if (!guard(r, G_READ)) return ESP_OK;
    status_json(b, sizeof b);
    return send_json(r, b);
}

/* The browser knows the time even when there is no NTP (e.g. no internet).
   Only ever accepted while the device has no time of its own. */
static esp_err_t h_time(httpd_req_t *r)
{
    if (!guard(r, G_WRITE_JSON)) return ESP_OK;
    cJSON *j = read_json(r);
    if (!j) return fail(r, "400 Bad Request", "invalid body");
    cJSON *e = cJSON_GetObjectItem(j, "epoch");
    if (cJSON_IsNumber(e) && time(NULL) < 1700000000 && e->valuedouble > 1700000000 && e->valuedouble < 4102444800.0) {
        struct timeval tv = { .tv_sec = (time_t) e->valuedouble };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock set from browser");
    }
    cJSON_Delete(j);
    return ok(r);
}

static void restart_task(void *a) { vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart(); }

static esp_err_t h_ota(httpd_req_t *r)
{
    if (!guard(r, G_WRITE_BIN)) return ESP_FAIL;               /* answer sent; drop the unread upload */
    live_t lv; live_get(&lv);
    if (lv.session_active) { fail(r, "409 Conflict", "A session is being recorded - end it first"); return ESP_FAIL; }
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (!p) { fail(r, "500 Internal Server Error", "No update partition"); return ESP_FAIL; }
    if (r->content_len < 1024 || r->content_len > p->size) { fail(r, "400 Bad Request", "Not a firmware image for this device"); return ESP_FAIL; }
    esp_ota_handle_t h;
    if (esp_ota_begin(p, OTA_SIZE_UNKNOWN, &h) != ESP_OK) { fail(r, "500 Internal Server Error", "OTA begin failed"); return ESP_FAIL; }

    static char buf[2048];
    int left = r->content_len, timeouts = 0;
    bool good = true;
    while (left > 0) {
        int n = httpd_req_recv(r, buf, left < (int) sizeof buf ? left : (int) sizeof buf);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 6) continue;
        if (n <= 0 || esp_ota_write(h, buf, n) != ESP_OK) { good = false; break; }
        timeouts = 0;
        left -= n;
    }
    if (!good) { esp_ota_abort(h); fail(r, "400 Bad Request", "Upload failed or invalid firmware image"); return ESP_FAIL; }
    if (esp_ota_end(h) != ESP_OK) return fail(r, "400 Bad Request", "Invalid firmware image");   /* checksum */

    /* Refuse an image that is valid but built for something else. */
    esp_app_desc_t d;
    if (esp_ota_get_partition_description(p, &d) != ESP_OK ||
        strcmp(d.project_name, esp_app_get_description()->project_name) != 0)
        return fail(r, "400 Bad Request", "This is not WaterRower firmware");
    if (esp_ota_set_boot_partition(p) != ESP_OK) return fail(r, "500 Internal Server Error", "Could not activate the update");
    ok(r);
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* ---- start --------------------------------------------------------------------- */
void web_start(void)
{
    psa_crypto_init();
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 40;
    c.max_open_sockets = 8;
    c.lru_purge_enable = true;
    c.stack_size = 8192;
    c.recv_wait_timeout = 10;
    c.send_wait_timeout = 5;
    c.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&srv, &c));

#define R(u, m, h) { .uri = u, .method = m, .handler = h, .user_ctx = NULL }
    const httpd_uri_t routes[] = {
        R("/", HTTP_GET, h_index), R("/setup", HTTP_GET, h_setup),
        R("/static/app.js*", HTTP_GET, h_js), R("/static/style.css*", HTTP_GET, h_css),
        R("/api/version", HTTP_GET, h_version), R("/api/auth", HTTP_POST, h_auth),
        R("/api/live", HTTP_GET, h_live), R("/api/stream", HTTP_GET, h_stream),
        R("/api/session/end", HTTP_POST, h_end), R("/api/session/reset", HTTP_POST, h_reset),
        R("/api/sessions", HTTP_GET, h_sessions),
        R("/api/sessions/*", HTTP_GET, h_session), R("/api/sessions/*", HTTP_DELETE, h_session),
        R("/api/arena", HTTP_GET, h_arena), R("/api/arena", HTTP_POST, h_arena),
        R("/api/mqtt", HTTP_GET, h_mqtt), R("/api/mqtt", HTTP_POST, h_mqtt),
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
