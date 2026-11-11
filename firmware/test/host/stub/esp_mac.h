#pragma once
#include <stdint.h>
#define ESP_MAC_WIFI_STA 0
static inline int esp_read_mac(uint8_t*m,int t){for(int i=0;i<6;i++)m[i]=i+1;return 0;}
