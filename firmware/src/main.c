#include "app.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

void app_main(void)
{
    ESP_LOGI("main", "WaterRower ESP " FW_VERSION);
    settings_init();
    live_init();
    store_init();
    wifi_mgr_init();
    rower_init();
    web_start();
    uplink_init();
    mqtt_init();
    /* Everything came up: keep this image. One that crashes before this
       point is rolled back to the previous one by the bootloader. */
    esp_ota_mark_app_valid_cancel_rollback();
}
