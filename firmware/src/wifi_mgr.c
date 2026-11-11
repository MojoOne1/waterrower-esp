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

static bool sta_up, ap_on, have_creds;
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

static void start_ap(void)
{
    if (ap_on) return;
    char pass[64];
    if (!settings_get_str("ap_pass", pass, sizeof pass)) strcpy(pass, "waterrower");
    wifi_config_t ap = {0};
    strncpy((char *) ap.ap.ssid, ap_ssid, sizeof ap.ap.ssid);
    ap.ap.ssid_len = strlen(ap_ssid);
    strncpy((char *) ap.ap.password, pass, sizeof ap.ap.password);
    ap.ap.max_connection = 4;
    ap.ap.authmode = strlen(pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
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
        if (have_creds) esp_timer_start_once(t_reconnect, 4000000);
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
        strncpy((char *) sta.sta.ssid, ssid, sizeof sta.sta.ssid);
        strncpy((char *) sta.sta.password, pass, sizeof sta.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
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
    settings_set_str("wifi_ssid", ssid);
    settings_set_str("wifi_pass", pass ? pass : "");
    have_creds = ssid && ssid[0];
    if (!have_creds) return;
    wifi_config_t sta = {0};
    strncpy((char *) sta.sta.ssid, ssid, sizeof sta.sta.ssid);
    strncpy((char *) sta.sta.password, pass ? pass : "", sizeof sta.sta.password);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    sta_up = false;
    esp_wifi_disconnect();
    esp_wifi_connect();
}

int wifi_mgr_status_json(char *buf, size_t len)
{
    char raw[33] = "", ssid[80], tz[64];
    settings_get_str("wifi_ssid", raw, sizeof raw);
    json_esc(ssid, sizeof ssid, raw);
    if (!settings_get_str("tz", tz, sizeof tz)) strcpy(tz, TZ_DEFAULT);
    esp_netif_ip_info_t ip = {0};
    if (sta_up) esp_netif_get_ip_info(sta_netif, &ip);
    wifi_ap_record_t ap = {0};
    int rssi = (sta_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    return snprintf(buf, len,
        "{\"ssid\":\"%s\",\"connected\":%s,\"ip\":\"" IPSTR "\",\"rssi\":%d,"
        "\"ap_active\":%s,\"ap_ssid\":\"%s\",\"tz\":\"%s\",\"session_timeout_min\":%d}",
        ssid, sta_up ? "true" : "false", IP2STR(&ip.ip), rssi,
        ap_on ? "true" : "false", ap_ssid, tz, settings_get_i32("sess_to", 5));
}

int wifi_mgr_scan_json(char *buf, size_t len)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    int n = 0;
    uint16_t cnt = 20;
    wifi_ap_record_t recs[20];
    if (esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_records(&cnt, recs) == ESP_OK) n = cnt;
    int o = snprintf(buf, len, "[");
    for (int i = 0; i < n && (size_t) o < len - 80; i++) {
        if (!recs[i].ssid[0]) continue;
        char esc[80];
        json_esc(esc, sizeof esc, (const char *) recs[i].ssid);
        o += snprintf(buf + o, len - o, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                      o > 1 ? "," : "", esc, recs[i].rssi,
                      recs[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }
    o += snprintf(buf + o, len - o, "]");
    return o;
}
