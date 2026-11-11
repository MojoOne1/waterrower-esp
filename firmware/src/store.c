/* Session storage on a LittleFS partition.
 *   <base>/<id>.smp  append-only binary samples (sample_t, 16 bytes each)
 *   <base>/<id>.sum  binary summary (sum_t), written when the session is closed
 * A .smp without .sum is an open (or interrupted) session.
 */
#include "app.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_littlefs.h"
#include "esp_log.h"

#ifndef STORE_BASE
#define STORE_BASE "/data"
#endif
#define BASE STORE_BASE
#define MAX_SESSIONS 4000
#define MIN_FREE (300 * 1024)
static const char *TAG = "store";

static void path(char *out, size_t n, const char *id, const char *ext)
{
    snprintf(out, n, BASE "/%s.%s", id, ext);
}

void store_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t e = esp_vfs_littlefs_register(&conf);
    if (e != ESP_OK) ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(e));
    size_t total = 0, used = 0;
    store_info(&total, &used);
    ESP_LOGI(TAG, "storage %u / %u bytes used", (unsigned) used, (unsigned) total);
}

void store_info(size_t *total, size_t *used)
{
    *total = *used = 0;
    esp_littlefs_info("storage", total, used);
}

bool store_exists(const char *id)
{
    char p[64]; struct stat st;
    path(p, sizeof p, id, "smp");
    return stat(p, &st) == 0;
}

void *store_open_session(const char *id)
{
    char p[64];
    path(p, sizeof p, id, "smp");
    return fopen(p, "ab");
}

void store_append(void *h, const sample_t *s) { if (h) fwrite(s, sizeof *s, 1, (FILE *) h); }

void store_flush(void *h)
{
    if (!h) return;
    fflush((FILE *) h);
    fsync(fileno((FILE *) h));
}

void store_close_handle(void *h)
{
    if (h) { store_flush(h); fclose((FILE *) h); }
}

bool store_write_summary(const sum_t *s)
{
    char p[64];
    path(p, sizeof p, s->id, "sum");
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = fwrite(s, sizeof *s, 1, f) == 1;
    fflush(f); fsync(fileno(f)); fclose(f);
    return ok;
}

/* The stored summary if there is a sane one, otherwise one computed from the samples. */
bool store_read_summary(const char *id, sum_t *out, bool *has_file)
{
    char p[64];
    path(p, sizeof p, id, "sum");
    FILE *f = fopen(p, "rb");
    bool ok = false;
    if (f) {
        ok = fread(out, sizeof *out, 1, f) == 1;
        fclose(f);
        if (ok) {                                   /* never trust what is on disk */
            out->id[sizeof out->id - 1] = 0;
            if (strcmp(out->id, id) != 0 || out->spark_n > sizeof out->spark) ok = false;
        }
    }
    if (has_file) *has_file = ok;
    return ok ? true : store_summarize(id, out);
}

FILE *store_open_samples(const char *id)
{
    char p[64];
    path(p, sizeof p, id, "smp");
    return fopen(p, "rb");
}

bool store_summarize(const char *id, sum_t *o)
{
    FILE *f = store_open_samples(id);
    if (!f) return false;
    memset(o, 0, sizeof *o);
    snprintf(o->id, sizeof o->id, "%s", id);

    sample_t b[32];
    size_t n_total = 0, n;
    double sp = 0, spm = 0;
    sample_t first = {0}, last = {0};
    while ((n = fread(b, sizeof b[0], 32, f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            if (n_total == 0) first = b[i];
            last = b[i];
            sp += b[i].speed_cms; spm += b[i].spm;
            n_total++;
        }
    }
    if (n_total == 0) { fclose(f); return false; }
    o->started_at = first.ts;
    o->ended_at = last.ts > first.ts ? last.ts : first.ts + 1;
    o->distance_m = last.distance_m;
    o->duration_s = last.duration_s;
    o->strokes = last.strokes;
    o->avg_speed_ms = (float) (sp / n_total / 100.0);
    o->avg_spm = (float) (spm / n_total);

    /* sparkline: up to 40 buckets of mean speed */
    int pts = n_total < 40 ? (int) n_total : 40;
    double acc[40] = {0}; int cnt[40] = {0};
    rewind(f);
    size_t idx = 0;
    while ((n = fread(b, sizeof b[0], 32, f)) > 0) {
        for (size_t i = 0; i < n && idx < n_total; i++, idx++) {
            int k = (int) (idx * pts / n_total);
            if (k >= pts) k = pts - 1;
            acc[k] += b[i].speed_cms; cnt[k]++;
        }
    }
    fclose(f);
    for (int k = 0; k < pts; k++) {
        double v = cnt[k] ? acc[k] / cnt[k] / 10.0 : 0;   /* cm/s -> 0.1 m/s */
        o->spark[k] = v > 255 ? 255 : (uint8_t) (v + 0.5);
    }
    o->spark_n = (uint8_t) pts;
    return true;
}

static int cmp_desc(const void *a, const void *b) { return strcmp((const char *) b, (const char *) a); }

/* All session IDs, newest name first. The caller frees *out. */
int store_list_ids(char (**out)[24])
{
    *out = NULL;
    DIR *d = opendir(BASE);
    if (!d) return 0;
    int n = 0, cap = 0;
    char (*ids)[24] = NULL;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_SESSIONS) {
        size_t l = strlen(e->d_name);
        if (l <= 4 || l - 4 >= 24 || strcmp(e->d_name + l - 4, ".smp") != 0) continue;
        if (n == cap) {
            int ncap = cap ? cap * 2 : 64;
            void *p = realloc(ids, (size_t) ncap * 24);
            if (!p) break;
            ids = p; cap = ncap;
        }
        memcpy(ids[n], e->d_name, l - 4);
        ids[n][l - 4] = 0;
        n++;
    }
    closedir(d);
    if (n) qsort(ids, n, 24, cmp_desc);
    *out = ids;
    return n;
}

bool store_delete(const char *id)
{
    char p[64];
    path(p, sizeof p, id, "smp"); bool a = unlink(p) == 0;
    path(p, sizeof p, id, "sum"); unlink(p);
    return a;
}

void store_recover(void)
{
    char (*ids)[24];
    int n = store_list_ids(&ids);
    for (int i = 0; i < n; i++) {
        sum_t s; bool has;
        if (!store_read_summary(ids[i], &s, &has)) { store_delete(ids[i]); continue; }   /* empty file */
        if (!has) { store_write_summary(&s); ESP_LOGI(TAG, "recovered %s", ids[i]); }
    }
    free(ids);
}

/* Keep MIN_FREE available by dropping the oldest sessions (by start time). */
void store_make_room(void)
{
    for (int round = 0; round < 50; round++) {
        size_t total, used;
        store_info(&total, &used);
        if (total == 0 || total - used > MIN_FREE) return;
        char (*ids)[24];
        int n = store_list_ids(&ids);
        if (n == 0) { free(ids); return; }
        int oldest = n - 1; uint32_t t_old = UINT32_MAX;
        for (int i = 0; i < n; i++) {
            sum_t s;
            if (store_read_summary(ids[i], &s, NULL) && s.started_at < t_old) { t_old = s.started_at; oldest = i; }
        }
        ESP_LOGW(TAG, "storage nearly full, deleting oldest session %s", ids[oldest]);
        store_delete(ids[oldest]);
        free(ids);
    }
}

void store_set_synced(const char *id, bool synced)
{
    sum_t s; bool has;
    if (!store_read_summary(id, &s, &has) || !has || s.synced == synced) return;
    s.synced = synced;
    store_write_summary(&s);
}

void store_clear_synced_all(void)
{
    char (*ids)[24];
    int n = store_list_ids(&ids);
    for (int i = 0; i < n; i++) store_set_synced(ids[i], false);
    free(ids);
}

int sum_to_json(const sum_t *s, bool spark, char *buf, size_t len)
{
    char ended[16];
    if (s->ended_at) snprintf(ended, sizeof ended, "%u", (unsigned) s->ended_at);
    else strcpy(ended, "null");
    int n = snprintf(buf, len,
        "{\"session_id\":\"%s\",\"started_at\":%u,\"ended_at\":%s,\"distance_m\":%u,"
        "\"duration_s\":%u,\"strokes\":%u,\"avg_speed_ms\":%.2f,\"avg_spm\":%.1f",
        s->id, (unsigned) s->started_at, ended,
        (unsigned) s->distance_m, (unsigned) s->duration_s, (unsigned) s->strokes,
        s->avg_speed_ms, s->avg_spm);
    if (spark && n > 0 && (size_t) n < len) {
        n += snprintf(buf + n, len - n, ",\"sparkline\":[");
        for (int i = 0; i < s->spark_n && i < (int) sizeof s->spark && (size_t) n < len; i++)
            n += snprintf(buf + n, len - n, "%s%.1f", i ? "," : "", s->spark[i] / 10.0);
        if ((size_t) n < len) n += snprintf(buf + n, len - n, "]");
    }
    if ((size_t) n < len) n += snprintf(buf + n, len - n, "}");
    if ((size_t) n >= len) n = len ? (int) len - 1 : 0;
    return n;
}
