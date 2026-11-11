/* WiFi station with a fallback access point + captive DNS.
 * No credentials, or no connection for 20 s -> open "WaterRower-XXXX"
 * (WPA2, password in NVS "ap_pass", default "waterrower"); the phone is sent to /setup.
 * The AP is shut down 30 s after the station got an address.
 */
#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

static const char *TAG = "wifi";
#define TZ_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3"
#define AP_IP_STR  "192.168.4.1"
#define AP_DEFAULT_PASS "waterrower"

static volatile bool sta_up, ap_on, have_creds, scanning;
static esp_timer_handle_t t_ap_start, t_ap_stop, t_reconnect;
static esp_netif_t *sta_netif, *ap_netif;
static char ap_ssid[32];

bool wifi_mgr_connected(void) { return sta_up; }
bool wifi_mgr_ap_active(void) { return ap_on; }

void wifi_mgr_apply_tz(void)
{
    char tz[64];
    if (!settings_get_str("tz", tz, sizeof tz)) strcpy(tz, TZ_DEFAULT);
    setenv("TZ", tz, 1);
    tzset();
}

static void ap_password(char *pass, size_t n)
{
    /* Never an open network: anything shorter than WPA2 allows falls back to the default. */
    if (!settings_get_str("ap_pass", pass, n) || strlen(pass) < 8) snprintf(pass, n, "%s", AP_DEFAULT_PASS);
}

bool wifi_mgr_ap_default_password(void)
{
    char pass[65];
    ap_password(pass, sizeof pass);
    return strcmp(pass, AP_DEFAULT_PASS) == 0;
}

static void apply_ap_config(void)
{
    char pass[65];
    ap_password(pass, sizeof pass);
    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, ap_ssid, strnlen(ap_ssid, sizeof ap.ap.ssid));
    ap.ap.ssid_len = strnlen(ap_ssid, sizeof ap.ap.ssid);
    memcpy(ap.ap.password, pass, strnlen(pass, sizeof ap.ap.password - 1));
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
}

bool wifi_mgr_set_ap_password(const char *pass)
{
    size_t l = pass ? strlen(pass) : 0;
    if (l < 8 || l > 63) return false;
    settings_set_str("ap_pass", pass);
    if (ap_on) apply_ap_config();
    return true;
}

static void start_ap(void)
{
    if (ap_on) return;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    apply_ap_config();
    ap_on = true;
    ESP_LOGW(TAG, "fallback AP \"%s\" up, open http://" AP_IP_STR "/", ap_ssid);
}

static void stop_ap(void)
{
    if (!ap_on) return;
    ap_on = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "fallback AP off");
}

static void cb_ap_start(void *a) { if (!sta_up) start_ap(); }
static void cb_ap_stop(void *a) { if (sta_up) stop_ap(); }
static void cb_reconnect(void *a) { if (have_creds && !sta_up) esp_wifi_connect(); }

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (have_creds) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        sta_up = false;
        /* A station that keeps scanning drags the access point off its channel
           and makes the setup page unreachable - retry slowly while it is up. */
        esp_timer_stop(t_reconnect);
        if (have_creds && !scanning) esp_timer_start_once(t_reconnect, ap_on ? 30000000 : 4000000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        sta_up = true;
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        esp_timer_stop(t_ap_start);
        if (ap_on) esp_timer_start_once(t_ap_stop, 30000000);   /* let the phone read the result */
    }
}

/* ---- captive DNS: answer every A query with the AP address -------------- */
static void dns_task(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    bind(s, (struct sockaddr *) &a, sizeof a);
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    uint8_t buf[300];
    while (1) {
        struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = recvfrom(s, buf, sizeof buf - 16, 0, (struct sockaddr *) &from, &fl);
        if (n < 12 || !ap_on) continue;
        if ((buf[2] & 0xF8) != 0 || buf[4] != 0 || buf[5] != 1) continue;   /* one standard query only */
        /* Only answer clients of the access point, never the home network. */
        if ((ntohl(from.sin_addr.s_addr) & 0xFFFFFF00u) != 0xC0A80400u) continue;
        buf[2] = 0x81; buf[3] = 0x80;       /* response, recursion available */
        buf[6] = 0; buf[7] = 1;             /* one answer */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        static const uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
        memcpy(buf + n, ans, sizeof ans);
        sendto(s, buf, n + sizeof ans, 0, (struct sockaddr *) &from, fl);
    }
}

void wifi_mgr_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(sta_netif, "waterrower");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ap_ssid, sizeof ap_ssid, "WaterRower-%02X%02X", mac[4], mac[5]);

    esp_timer_create_args_t a = { .callback = cb_ap_start, .name = "ap_start" };
    esp_timer_create(&a, &t_ap_start);
    a.callback = cb_ap_stop; a.name = "ap_stop";
    esp_timer_create(&a, &t_ap_stop);
    a.callback = cb_reconnect; a.name = "reconnect";
    esp_timer_create(&a, &t_reconnect);

    wifi_mgr_apply_tz();
    char ssid[33], pass[65];
    have_creds = settings_get_str("wifi_ssid", ssid, sizeof ssid);
    settings_get_str("wifi_pass", pass, sizeof pass);

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (have_creds) {
        wifi_config_t sta = {0};
        memcpy(sta.sta.ssid, ssid, strnlen(ssid, sizeof sta.sta.ssid));
        memcpy(sta.sta.password, pass, strnlen(pass, sizeof sta.sta.password));
        if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) have_creds = false;   /* unusable -> setup AP */
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    if (have_creds) esp_timer_start_once(t_ap_start, 20000000);
    else start_ap();

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);

    mdns_init();
    mdns_hostname_set("waterrower");
    mdns_instance_name_set("WaterRower");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
}

void wifi_mgr_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;
    wifi_config_t sta = {0};
    memcpy(sta.sta.ssid, ssid, strnlen(ssid, sizeof sta.sta.ssid));
    if (pass) memcpy(sta.sta.password, pass, strnlen(pass, sizeof sta.sta.password));
    settings_set_str("wifi_ssid", ssid);
    settings_set_str("wifi_pass", pass ? pass : "");
    have_creds = true;
    sta_up = false;
    esp_timer_stop(t_reconnect);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
}

int wifi_mgr_status_json(char *buf, size_t len)
{
    char raw[65] = "", ssid[200], tz[140], aps[80];
    settings_get_str("wifi_ssid", raw, 33);
    json_esc(ssid, sizeof ssid, raw);
    if (!settings_get_str("tz", raw, sizeof raw)) strcpy(raw, TZ_DEFAULT);
    json_esc(tz, sizeof tz, raw);
    json_esc(aps, sizeof aps, ap_ssid);
    esp_netif_ip_info_t ip = {0};
    if (sta_up) esp_netif_get_ip_info(sta_netif, &ip);
    wifi_ap_record_t ap = {0};
    int rssi = (sta_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    return snprintf(buf, len,
        "{\"ssid\":\"%s\",\"connected\":%s,\"ip\":\"" IPSTR "\",\"rssi\":%d,"
        "\"ap_active\":%s,\"ap_ssid\":\"%s\",\"ap_default_password\":%s,\"tz\":\"%s\",\"session_timeout_min\":%d}",
        ssid, sta_up ? "true" : "false", IP2STR(&ip.ip), rssi,
        ap_on ? "true" : "false", aps, wifi_mgr_ap_default_password() ? "true" : "false",
        tz, settings_get_i32("sess_to", 5));
}

int wifi_mgr_scan_json(char *buf, size_t len)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    uint16_t cnt = 20;
    static wifi_ap_record_t recs[20];
    int n = 0;
    /* A station that is busy connecting refuses to scan: pause it for the duration. */
    scanning = true;
    esp_timer_stop(t_reconnect);
    if (!sta_up) esp_wifi_disconnect();
    if (esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_records(&cnt, recs) == ESP_OK) n = cnt;
    else esp_wifi_clear_ap_list();
    scanning = false;
    if (have_creds && !sta_up) esp_timer_start_once(t_reconnect, 3000000);

    int o = snprintf(buf, len, "[");
    for (int i = 0; i < n && (size_t) o + 260 < len; i++) {
        if (!recs[i].ssid[0]) continue;
        char esc[200];
        json_esc(esc, sizeof esc, (const char *) recs[i].ssid);
        o += snprintf(buf + o, len - o, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                      o > 1 ? "," : "", esc, recs[i].rssi,
                      recs[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }
    o += snprintf(buf + o, len - o, "]");
    return o;
}
