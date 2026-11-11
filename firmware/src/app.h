#pragma once
/* Shared declarations for the WaterRower ESP firmware. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

/* ---- settings (NVS) ---------------------------------------------------- */
void settings_init(void);
bool settings_get_str(const char *key, char *out, size_t len);  /* false if unset */
void settings_set_str(const char *key, const char *val);
int  settings_get_i32(const char *key, int def);
void json_esc(char *dst, size_t n, const char *src);   /* escape for use inside a JSON string */
void settings_set_i32(const char *key, int val);

/* ---- live state -------------------------------------------------------- */
typedef struct {
    float distance_m, speed_ms, stroke_rate, strokes, duration_s, watts;
    float total_distance_m, ratio, pull_s, sess_avg_speed_ms, sess_avg_spm;
    bool  s4_connected, usb_mode, session_active;
    char  session_id[24];
} live_t;

void     live_init(void);
void     live_get(live_t *out);
void     live_put(const live_t *in);
uint32_t live_seq(void);
void     live_set_race(const char *json);          /* NULL clears */
void     live_set_last(const char *json);          /* summary of the last closed session */
void     live_get_last(char *out, size_t n);
/* {"values":{...},"session_id":..,"race":..} - returns length */
int      live_snapshot_json(char *buf, size_t len);

/* ---- session storage --------------------------------------------------- */
typedef struct __attribute__((packed)) {
    uint32_t ts;          /* epoch seconds (or uptime if the clock is unset) */
    uint16_t distance_m;
    uint16_t speed_cms;
    uint16_t strokes;
    uint16_t duration_s;
    uint16_t watts;
    uint8_t  spm;
    uint8_t  pad;
} sample_t;

typedef struct {
    char     id[24];
    uint32_t started_at, ended_at;       /* ended_at == 0: still open */
    uint32_t distance_m, duration_s, strokes;
    float    avg_speed_ms, avg_spm;
    uint8_t  spark[40];                  /* speed, 0.1 m/s steps, 0..25.5 */
    uint8_t  spark_n;
} sum_t;

void  store_init(void);
void  store_recover(const char *active_id);          /* close orphaned files */
void *store_open_session(const char *id);            /* append handle */
void  store_append(void *h, const sample_t *s);
void  store_flush(void *h);
void  store_close_handle(void *h);
bool  store_write_summary(const sum_t *s);
bool  store_read_summary(const char *id, sum_t *out, bool *has_file);
bool  store_summarize(const char *id, sum_t *out);   /* from the samples */
int   store_list_ids(char (*ids)[24], int max);      /* newest first */
bool  store_delete(const char *id);
bool  store_exists(const char *id);
FILE *store_open_samples(const char *id);            /* read, caller closes */
void  store_make_room(void);
void  store_info(size_t *total, size_t *used);
int   sum_to_json(const sum_t *s, bool sparkline, char *buf, size_t len);

/* ---- rower (S4 link + sessions) ---------------------------------------- */
typedef enum { ROWER_CMD_END = 1, ROWER_CMD_RESET = 2 } rower_cmd_t;
void rower_init(void);
void rower_cmd(rower_cmd_t c);
void rower_set_timeout_min(int minutes);
int  rower_timeout_min(void);

/* ---- wifi -------------------------------------------------------------- */
void wifi_mgr_init(void);
bool wifi_mgr_connected(void);
bool wifi_mgr_ap_active(void);
void wifi_mgr_set_credentials(const char *ssid, const char *pass);
int  wifi_mgr_status_json(char *buf, size_t len);
int  wifi_mgr_scan_json(char *buf, size_t len);
void wifi_mgr_apply_tz(void);

/* ---- arena uplink ------------------------------------------------------ */
void uplink_init(void);
void uplink_reconfigure(void);
void uplink_sample(const char *sid, uint32_t started_at, const sample_t *s);
void uplink_summary(const sum_t *s);
int  uplink_status_json(char *buf, size_t len);

/* ---- MQTT (optional, Home Assistant discovery) -------------------------- */
void mqtt_init(void);
void mqtt_reconfigure(void);
int  mqtt_status_json(char *buf, size_t len);

/* ---- web --------------------------------------------------------------- */
void web_start(void);
