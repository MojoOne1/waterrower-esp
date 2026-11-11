#include "app.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static nvs_handle_t h;
static const char *TAG = "settings";

void settings_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    ESP_ERROR_CHECK(nvs_open("wr", NVS_READWRITE, &h));
}

bool settings_get_str(const char *key, char *out, size_t len)
{
    size_t n = len;
    if (nvs_get_str(h, key, out, &n) != ESP_OK) { if (len) out[0] = 0; return false; }
    return out[0] != 0;
}

void settings_set_str(const char *key, const char *val)
{
    if (nvs_set_str(h, key, val ? val : "") != ESP_OK) ESP_LOGW(TAG, "set %s failed", key);
    nvs_commit(h);
}

int settings_get_i32(const char *key, int def)
{
    int32_t v;
    return nvs_get_i32(h, key, &v) == ESP_OK ? (int) v : def;
}

void settings_set_i32(const char *key, int val)
{
    nvs_set_i32(h, key, val);
    nvs_commit(h);
}

void json_esc(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    for (; *src && o + 7 < n; src++) {
        unsigned char ch = (unsigned char) *src;
        if (ch == '"' || ch == '\\') { dst[o++] = '\\'; dst[o++] = ch; }
        else if (ch < 0x20) { o += snprintf(dst + o, n - o, "\\u%04x", ch); }
        else dst[o++] = ch;
    }
    dst[o] = 0;
}
