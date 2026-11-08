#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "pocket_model.h"
#include "pocket_pc_display.h"

typedef struct {
    unsigned version;
    char wifi_ssid[33], wifi_password[65];
    char companion_url[193], companion_token[129];
    char nas_host[64], nas_community[65];
    char genshin_uid[16], genshin_region[32];
    char genshin_cookie[512], genshin_widget_cookie[512];
    char genshin_device_id[65], genshin_device_fp[65];
    char home_url[193], home_token[513];
} pocket_config_t;

esp_err_t pocket_config_init(void);
void pocket_config_get(pocket_config_t *config);
esp_err_t pocket_config_save(const pocket_config_t *config);
/* Transactional partial update; never logs the supplied JSON. */
esp_err_t pocket_config_update(const char *json);
/* Separate NVS key: preserve the version-1 configuration blob and all credentials. */
void pocket_config_pc_display_get(pocket_pc_display_t *display);
esp_err_t pocket_config_pc_display_save(const char *selection);
void pocket_config_page_layout_get(pocket_page_layout_t *layout);
esp_err_t pocket_config_page_layout_save(const char *selection);
unsigned pocket_config_screen_timeout_get(void);
esp_err_t pocket_config_screen_timeout_save(unsigned seconds);
unsigned pocket_config_brightness_get(void);
esp_err_t pocket_config_brightness_save(unsigned percent);
esp_err_t pocket_cache_load(pocket_page_t page, pocket_snapshot_t *snapshot);
esp_err_t pocket_cache_save(pocket_page_t page, const pocket_snapshot_t *snapshot);
