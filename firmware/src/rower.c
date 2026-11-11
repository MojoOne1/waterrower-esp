/* WaterRower S4 link (USB CDC-ACM host) and session recording.
 * Protocol per "Water Rower S4 S5 USB Protocol Iss 1.04"; the parsing and the
 * session rules are ported 1:1 from the former ESPHome firmware.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

static const char *TAG = "rower";

static cdc_acm_dev_hdl_t dev;
static StreamBufferHandle_t rx_sb;
static QueueHandle_t cmd_q;

/* ---- S4 values --------------------------------------------------------- */
static struct {
    float distance_m, speed_cms, stroke_rate, strokes, avg25, pull25, elapsed_s;
    float total_m, watts;
} v;

static bool rowing_idle = true, usb_mode, s4_present, session_active, want_initial_poll;
static uint32_t last_rx_ms, last_handshake_ms, last_activity_ms;
static int poll_tick, timeout_min = 5;
static char rx_line[48];
static size_t rx_len;

/* ---- session ----------------------------------------------------------- */
static char sid[24];
static void *shandle;
static uint32_t s_started, s_last_flush_ms;
static double sum_speed, sum_spm;
static uint32_t n_samples;

static uint32_t now_ms(void) { return (uint32_t) (esp_timer_get_time() / 1000); }
static bool time_valid(void) { return time(NULL) > 1700000000; }
static uint32_t epoch_or_uptime(void) { return time_valid() ? (uint32_t) time(NULL) : now_ms() / 1000; }

static void publish(void)
{
    live_t l = {0};
    l.distance_m = v.distance_m;
    l.speed_ms = v.speed_cms / 100.0f;
    l.stroke_rate = v.stroke_rate;
    l.strokes = v.strokes;
    l.duration_s = v.elapsed_s;
    l.watts = v.watts;
    l.total_distance_m = v.total_m;
    l.ratio = v.pull25 > 0 ? (v.avg25 - v.pull25) * 1.25f : 0;
    l.pull_s = v.pull25 * 0.025f;
    l.sess_avg_speed_ms = n_samples ? (float) (sum_speed / n_samples / 100.0) : 0;
    l.sess_avg_spm = n_samples ? (float) (sum_spm / n_samples) : 0;
    l.s4_connected = s4_present;
    l.usb_mode = usb_mode;
    l.session_active = session_active;
    strncpy(l.session_id, sid, sizeof l.session_id - 1);
    live_put(&l);
}

/* ---- USB --------------------------------------------------------------- */
static void s4_write(const char *s)
{
    if (!dev) return;
    cdc_acm_host_data_tx_blocking(dev, (const uint8_t *) s, strlen(s), 200);
}

static bool on_rx(const uint8_t *data, size_t len, void *arg)
{
    xStreamBufferSend(rx_sb, data, len, 0);
    return true;
}

static void on_dev_event(const cdc_acm_host_dev_event_data_t *ev, void *arg)
{
    if (ev->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
        ESP_LOGW(TAG, "S4 USB device gone");
        cdc_acm_host_close(ev->data.cdc_hdl);
        dev = NULL;
    } else if (ev->type == CDC_ACM_HOST_ERROR) {
        ESP_LOGW(TAG, "CDC error %d", ev->data.error);
    }
}

static void usb_lib_task(void *arg)
{
    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

static void try_open(void)
{
    const cdc_acm_host_device_config_t cfg = {
        .connection_timeout_ms = 500,
        .out_buffer_size = 256,
        .in_buffer_size = 512,
        .event_cb = on_dev_event,
        .data_cb = on_rx,
        .user_arg = NULL,
    };
    cdc_acm_dev_hdl_t h = NULL;
    if (cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0, &cfg, &h) == ESP_OK) {
        cdc_acm_line_coding_t lc = { .dwDTERate = 19200, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8 };
        cdc_acm_host_line_coding_set(h, &lc);
        cdc_acm_host_set_control_line_state(h, true, true);
        dev = h;
    }
}

/* ---- S4 requests ------------------------------------------------------- */
static void poll_s4(void)
{
    static const char *const req[] = {
        "IRD057\r\n", "IRD14A\r\n", "IRS1A9\r\n", "IRD140\r\n",
        "IRS142\r\n", "IRS143\r\n", "IRT1E1\r\n", "IRD088\r\n",
    };
    for (size_t i = 0; i < sizeof req / sizeof req[0]; i++) {
        s4_write(req[i]);
        vTaskDelay(pdMS_TO_TICKS(30));          /* spec: one packet per 25 ms */
    }
}

static void poll_slow(void) { s4_write("IRD081\r\n"); }

static void handshake(void)
{
    s4_write("USB\r\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    s4_write("IV?\r\n");
}

/* ---- session ----------------------------------------------------------- */
static void start_session(void)
{
    char buf[48];
    if (time_valid()) {
        time_t t = time(NULL); struct tm tm;
        localtime_r(&t, &tm);
        snprintf(buf, sizeof buf, "%04d%02d%02dT%02d%02d%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(buf, sizeof buf, "boot-%u", (unsigned) (now_ms() / 1000));
    }
    store_make_room();
    shandle = store_open_session(buf);
    if (!shandle) { ESP_LOGE(TAG, "cannot open session file"); return; }
    snprintf(sid, sizeof sid, "%.23s", buf);
    s_started = epoch_or_uptime();
    s_last_flush_ms = now_ms();
    sum_speed = sum_spm = 0; n_samples = 0;
    session_active = true;
    ESP_LOGI(TAG, "session start %s", sid);
    publish();
}

static void end_session(void)
{
    if (!session_active) return;
    session_active = false;
    store_close_handle(shandle);
    shandle = NULL;
    sum_t s;
    if (store_summarize(sid, &s)) {
        s.started_at = s_started;
        store_write_summary(&s);
        uplink_summary(&s);
        char lj[200];
        snprintf(lj, sizeof lj,
            "{\"session_id\":\"%s\",\"distance_m\":%u,\"duration_s\":%u,\"strokes\":%u,\"avg_speed_ms\":%.2f,\"avg_spm\":%.1f}",
            s.id, (unsigned) s.distance_m, (unsigned) s.duration_s, (unsigned) s.strokes, s.avg_speed_ms, s.avg_spm);
        live_set_last(lj);
        ESP_LOGI(TAG, "session end %s: %u m, %u s", sid, (unsigned) s.distance_m, (unsigned) s.duration_s);
    } else {
        store_delete(sid);
    }
    poll_slow();
    vTaskDelay(pdMS_TO_TICKS(300));
    publish();
}

static void record_sample(void)
{
    sample_t s = {
        .ts = epoch_or_uptime(),
        .distance_m = (uint16_t) v.distance_m,
        .speed_cms = (uint16_t) v.speed_cms,
        .strokes = (uint16_t) v.strokes,
        .duration_s = (uint16_t) v.elapsed_s,
        .watts = (uint16_t) v.watts,
        .spm = (uint8_t) v.stroke_rate,
    };
    sum_speed += v.speed_cms; sum_spm += v.stroke_rate; n_samples++;
    store_append(shandle, &s);
    uplink_sample(sid, s_started, &s);
    if (now_ms() - s_last_flush_ms > 10000) { store_flush(shandle); s_last_flush_ms = now_ms(); }
}

/* ---- RX parsing -------------------------------------------------------- */
static int bcd(int b) { return ((b >> 4) & 0x0F) * 10 + (b & 0x0F); }
static long hex(const char *s, int n) { char t[8]; memcpy(t, s, n); t[n] = 0; return strtol(t, NULL, 16); }

static void mark_activity(bool stroke_start)
{
    last_activity_ms = now_ms();
    rowing_idle = false;
    if (stroke_start && !session_active) start_session();
}

static void process_line(const char *line, size_t n)
{
    last_rx_ms = now_ms();
    if (!strcmp(line, "SS") || !strcmp(line, "SE") || (n == 3 && line[0] == 'P' && strcmp(line, "PING"))) {
        mark_activity(!strcmp(line, "SS"));
        return;
    }
    if (!strcmp(line, "_WR_")) {
        if (!usb_mode) ESP_LOGI(TAG, "S4 link up (USB mode)");
        usb_mode = true;
        want_initial_poll = true;
        return;
    }
    if (!strcmp(line, "PING")) {
        if (!rowing_idle && now_ms() - last_activity_ms > 2000) {
            rowing_idle = true;
            v.speed_cms = v.stroke_rate = v.watts = 0;
            publish();
        }
        return;
    }
    if (!strcmp(line, "OK")) return;

    if (n >= 8 && !strncmp(line, "IDS", 3)) {
        long val = hex(line + 6, 2);
        if (!strncmp(line + 3, "1A9", 3)) v.stroke_rate = rowing_idle ? 0 : val;
        else if (!strncmp(line + 3, "142", 3)) v.avg25 = val;
        else if (!strncmp(line + 3, "143", 3)) v.pull25 = val;
    } else if (n >= 10 && !strncmp(line, "IDD", 3)) {
        long val = hex(line + 6, 4);
        if (!strncmp(line + 3, "057", 3)) v.distance_m = val;
        else if (!strncmp(line + 3, "140", 3)) v.strokes = val;
        else if (!strncmp(line + 3, "081", 3)) v.total_m = val;
        else if (!strncmp(line + 3, "088", 3)) v.watts = rowing_idle ? 0 : val;
        else if (!strncmp(line + 3, "14A", 3)) v.speed_cms = rowing_idle ? 0 : val;
    } else if (n >= 12 && !strncmp(line, "IDT", 3)) {
        if (!strncmp(line + 3, "1E1", 3))
            v.elapsed_s = bcd(hex(line + 6, 2)) * 3600 + bcd(hex(line + 8, 2)) * 60 + bcd(hex(line + 10, 2));
    }
}

static void feed(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = (char) b[i];
        if (c != '\r' && c != '\n') {
            if (rx_len < sizeof rx_line - 1) rx_line[rx_len++] = c; else rx_len = 0;
            continue;
        }
        if (rx_len) { rx_line[rx_len] = 0; process_line(rx_line, rx_len); rx_len = 0; }
    }
}

static void drain(TickType_t wait)
{
    uint8_t buf[128];
    size_t n;
    while ((n = xStreamBufferReceive(rx_sb, buf, sizeof buf, wait)) > 0) { feed(buf, n); wait = 0; }
}

/* ---- commands ---------------------------------------------------------- */
static void do_reset(void)
{
    if (session_active) end_session();
    vTaskDelay(pdMS_TO_TICKS(50));
    s4_write("RESET\r\n");
    float keep_total = v.total_m;          /* lifetime counter is not part of the display */
    memset(&v, 0, sizeof v);
    v.total_m = keep_total;
    usb_mode = false;       /* RESET ends USB mode; the link manager redoes the handshake */
    publish();
}

/* ---- housekeeping, once per second ------------------------------------- */
static void second_tick(void)
{
    uint32_t now = now_ms();
    bool present = dev != NULL;
    bool changed = false;
    if (present != s4_present) {
        s4_present = present;
        changed = true;
        if (present) { ESP_LOGI(TAG, "S4 connected"); last_handshake_ms = 0; }
        else {
            ESP_LOGW(TAG, "S4 disconnected");
            usb_mode = false;
            if (session_active) end_session();
        }
    }
    if (present) {
        if (usb_mode && now - last_rx_ms > 10000) { ESP_LOGW(TAG, "no data for 10 s, USB mode lost"); usb_mode = false; changed = true; }
        if (!usb_mode && now - last_handshake_ms > 5000) { last_handshake_ms = now; handshake(); }
    }
    if (session_active) {
        poll_tick = (poll_tick + 1) % 5;
        poll_s4();
        if (poll_tick == 0) poll_slow();
        drain(pdMS_TO_TICKS(120));
        record_sample();
        publish();
        if (now - last_activity_ms > (uint32_t) timeout_min * 60000u) end_session();
    } else if (changed) {
        publish();
    }
}

static void rower_task(void *arg)
{
    uint32_t next = now_ms() + 1000;
    while (1) {
        static uint32_t last_open_ms;
        if (!dev && now_ms() - last_open_ms > 2000) { last_open_ms = now_ms(); try_open(); }
        drain(pdMS_TO_TICKS(50));
        rower_cmd_t c;
        while (xQueueReceive(cmd_q, &c, 0) == pdTRUE) {
            if (c == ROWER_CMD_END) end_session();
            else if (c == ROWER_CMD_RESET) do_reset();
        }
        if (want_initial_poll) {
            want_initial_poll = false;
            poll_s4(); poll_slow();
            drain(pdMS_TO_TICKS(300));
            publish();
        }
        if ((int32_t) (now_ms() - next) >= 0) {
            next += 1000;
            if ((int32_t) (now_ms() - next) > 0) next = now_ms() + 1000;
            second_tick();
        }
    }
}

void rower_cmd(rower_cmd_t c) { if (cmd_q) xQueueSend(cmd_q, &c, 0); }
int rower_timeout_min(void) { return timeout_min; }
void rower_set_timeout_min(int m) { timeout_min = m < 1 ? 1 : m > 60 ? 60 : m; }

void rower_init(void)
{
    rx_sb = xStreamBufferCreate(2048, 1);
    cmd_q = xQueueCreate(4, sizeof(rower_cmd_t));
    timeout_min = settings_get_i32("sess_to", 5);
    store_recover(NULL);

    const usb_host_config_t host_cfg = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL);
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));
    xTaskCreate(rower_task, "rower", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "ready");
}
