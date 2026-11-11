#include "app.h"
#include "esp_log.h"

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
}
