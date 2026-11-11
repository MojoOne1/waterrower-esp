#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { const char *base_path, *partition_label; int format_if_mount_failed, dont_mount; } esp_vfs_littlefs_conf_t;
static inline int esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c){return 0;}
extern size_t fake_used;
static inline int esp_littlefs_info(const char *l, size_t *t, size_t *u){*t=1000000;*u=fake_used;return 0;}
