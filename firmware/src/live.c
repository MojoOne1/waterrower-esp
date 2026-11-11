#include "app.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static SemaphoreHandle_t lock;
static live_t cur;
static uint32_t seq;
static char race[1024];
static int64_t updated_us;

void live_init(void)
{
    lock = xSemaphoreCreateMutex();
    memset(&cur, 0, sizeof cur);
}

void live_get(live_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = cur;
    xSemaphoreGive(lock);
}

void live_put(const live_t *in)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    cur = *in;
    seq++;
    updated_us = esp_timer_get_time();
    xSemaphoreGive(lock);
}

uint32_t live_seq(void) { return seq; }

void live_set_race(const char *json)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (json && strlen(json) < sizeof race) strcpy(race, json); else race[0] = 0;
    seq++;
    xSemaphoreGive(lock);
}

int live_snapshot_json(char *buf, size_t len)
{
    live_t l;
    char r[sizeof race];
    live_get(&l);
    xSemaphoreTake(lock, portMAX_DELAY);
    strcpy(r, race);
    xSemaphoreGive(lock);
    return snprintf(buf, len,
        "{\"values\":{\"distance\":%d,\"speed\":%.2f,\"stroke_rate\":%d,\"strokes\":%d,"
        "\"duration\":%d,\"watts\":%d,\"total_distance\":%d,\"ratio\":%.2f,"
        "\"firmware_version\":\"%s\",\"s4_connected\":%s,\"usb_mode\":%s},"
        "\"session_id\":\"%s\",\"session_active\":%s,\"updated_at\":%lld,\"connected\":true,"
        "\"time_valid\":%s,\"race\":%s}",
        (int) l.distance_m, l.speed_ms, (int) l.stroke_rate, (int) l.strokes,
        (int) l.duration_s, (int) l.watts, (int) l.total_distance_m, l.ratio,
        FW_VERSION, l.s4_connected ? "true" : "false", l.usb_mode ? "true" : "false",
        l.session_id, l.session_active ? "true" : "false",
        (long long) (updated_us / 1000000), time(NULL) > 1700000000 ? "true" : "false",
        r[0] ? r : "null");
}
