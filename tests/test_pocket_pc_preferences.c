/* Execute the actual NVS configuration code with a pre-existing version-1 blob. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "pocket_config.h"
#include "nvs.h"

static pocket_config_t original;
static char stored[POCKET_PC_DISPLAY_BYTES], pending[POCKET_PC_DISPLAY_BYTES];
static bool has_preference, fail_commit;
static char page_stored[96], page_pending[96];
static bool page_has, writing_page, writing_screen, screen_has;
static char screen_stored[8],screen_pending[8];
static char brightness_stored[8], brightness_pending[8];
static bool brightness_has, writing_brightness;
static unsigned blob_writes, string_writes;

/* This test never invokes JSON updates; fail if one is accidentally introduced. */
cJSON *cJSON_Parse(const char *text) { (void)text; abort(); }
void cJSON_Delete(cJSON *item) { (void)item; abort(); }
int cJSON_IsObject(const cJSON *item) { (void)item; abort(); }
int cJSON_IsString(const cJSON *item) { (void)item; abort(); }

void *xSemaphoreCreateMutex(void) { return &original; }
int xSemaphoreTake(void *semaphore, unsigned timeout) { (void)semaphore; (void)timeout; return 1; }
int xSemaphoreGive(void *semaphore) { (void)semaphore; return 1; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "pocket_notes")); (void)mode; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size) {
    (void)handle; assert(!strcmp(key, "configuration"));
    assert(*size == sizeof(original)); memcpy(value, &original, sizeof(original)); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size) {
    (void)handle; (void)key; (void)value; (void)size; ++blob_writes; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *size) {
    if (!strcmp(key, "brightness")) {
        if (!brightness_has) return ESP_ERR_NVS_NOT_FOUND;
        assert(*size >= strlen(brightness_stored) + 1);
        strcpy(value, brightness_stored); *size = strlen(value) + 1; return ESP_OK;
    }
    (void)handle; if(!strcmp(key,"screen_timeout")) { if(!screen_has)return ESP_ERR_NVS_NOT_FOUND; strcpy(value,screen_stored);*size=strlen(value)+1;return ESP_OK; }
    if (!strcmp(key, "page_layout")) {
        if (!page_has) return ESP_ERR_NVS_NOT_FOUND;
        assert(*size >= strlen(page_stored)+1); strcpy(value,page_stored); *size=strlen(page_stored)+1; return ESP_OK;
    }
    assert(!strcmp(key, "pc_display"));
    if (!has_preference) return ESP_ERR_NVS_NOT_FOUND;
    size_t required = strlen(stored) + 1;
    if (*size < required) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(value, stored, required); *size = required; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    writing_brightness = !strcmp(key, "brightness");
    if (writing_brightness) { strcpy(brightness_pending, value); return ESP_OK; }
    (void)handle; writing_screen=!strcmp(key,"screen_timeout");writing_page = !strcmp(key,"page_layout");
    if(writing_screen) { strcpy(screen_pending,value);return ESP_OK; }
    if (writing_page) { strcpy(page_pending,value); return ESP_OK; }
    assert(!strcmp(key, "pc_display"));
    snprintf(pending, sizeof(pending), "%s", value); ++string_writes; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    if (fail_commit) return ESP_FAIL;
    if (writing_brightness) { strcpy(brightness_stored, brightness_pending); brightness_has = true; }
    else if(writing_screen) { strcpy(screen_stored,screen_pending);screen_has=true; }
    else if (writing_page) { strcpy(page_stored,page_pending); page_has=true; }
    else { strcpy(stored, pending); has_preference = true; } return ESP_OK;
}

int main(void) {
    _Static_assert(sizeof(pocket_config_t) == 2464, "Keep the accepted version-1 blob layout");
    original.version = 1;
    strcpy(original.wifi_ssid, "fixture-network"); strcpy(original.wifi_password, "fixture-password");
    strcpy(original.companion_url, "http://192.168.1.20:8765"); strcpy(original.companion_token, "fixture-pairing");
    strcpy(original.genshin_uid, "123456789"); strcpy(original.genshin_cookie, "fixture-cookie");
    strcpy(original.home_token, "fixture-home-token");
    assert(pocket_config_init() == ESP_OK);
    pocket_pc_display_t display;
    char encoded[POCKET_PC_DISPLAY_BYTES];
    pocket_config_pc_display_get(&display);
    assert(pocket_pc_display_format(&display, encoded, sizeof(encoded)));
    assert(!strcmp(encoded, "cpu,gpu,gpu_temp,memory,disk,status"));
    assert(pocket_config_pc_display_save("gpu_temp,memory") == ESP_OK);
    assert(!strcmp(stored, "gpu_temp,memory") && string_writes == 1 && blob_writes == 0);
    assert(pocket_config_init() == ESP_OK);  /* Simulated reboot restores only the new key. */
    pocket_config_pc_display_get(&display);
    assert(display.count == 2 && display.metric[0] == POCKET_PC_GPU_TEMP);
    assert(pocket_config_pc_display_save("gpu,gpu") == ESP_ERR_INVALID_ARG && string_writes == 1);
    fail_commit = true;
    assert(pocket_config_pc_display_save("cpu") == ESP_FAIL);
    pocket_config_pc_display_get(&display);
    assert(display.count == 2 && !strcmp(stored, "gpu_temp,memory"));
    pocket_config_t current; pocket_config_get(&current);
    assert(!memcmp(&current, &original, sizeof(original)) && blob_writes == 0);
    fail_commit = false; strcpy(stored, "unsupported");
    assert(pocket_config_init() == ESP_OK);
    pocket_config_pc_display_get(&display);
    assert(display.count == 6 && display.metric[0] == POCKET_PC_CPU);
    assert(blob_writes == 0);
    assert(pocket_config_page_layout_save("pc,-genshin,codex,-nas,home") == ESP_OK);
    assert(pocket_config_init() == ESP_OK);
    pocket_page_layout_t layout; pocket_config_page_layout_get(&layout);
    assert(layout.order[0] == POCKET_PC && !layout.enabled[POCKET_GENSHIN]);
    assert(pocket_config_page_layout_save("-pc,-genshin,-codex,-nas,-home") == ESP_ERR_INVALID_ARG);
    fail_commit=true;
    assert(pocket_config_page_layout_save("genshin,codex,nas,home,pc") == ESP_FAIL);
    pocket_config_page_layout_get(&layout); assert(layout.order[0] == POCKET_PC);
    pocket_config_get(&current); assert(!memcmp(&current,&original,sizeof(current)) && blob_writes == 0);
    fail_commit=false;strcpy(page_stored,"bad");assert(pocket_config_init()==ESP_OK);
    pocket_config_page_layout_get(&layout);assert(layout.order[0]==POCKET_GENSHIN && layout.enabled[POCKET_GENSHIN]);
    assert(pocket_config_screen_timeout_get()==60);
    assert(pocket_config_screen_timeout_save(120)==ESP_OK);
    assert(pocket_config_init()==ESP_OK && pocket_config_screen_timeout_get()==120);
    assert(pocket_config_screen_timeout_save(7)==ESP_ERR_INVALID_ARG);
    fail_commit=true;assert(pocket_config_screen_timeout_save(30)==ESP_FAIL);
    assert(pocket_config_screen_timeout_get()==120);fail_commit=false;
    pocket_config_get(&current);assert(!memcmp(&current,&original,sizeof(current)) && blob_writes==0);
    assert(pocket_config_brightness_get() == 75);
    assert(pocket_config_brightness_save(10) == ESP_OK);
    assert(pocket_config_init() == ESP_OK && pocket_config_brightness_get() == 10);
    assert(pocket_config_brightness_save(100) == ESP_OK);
    assert(pocket_config_init() == ESP_OK && pocket_config_brightness_get() == 100);
    assert(pocket_config_brightness_save(0) == ESP_ERR_INVALID_ARG);
    assert(pocket_config_brightness_save(101) == ESP_ERR_INVALID_ARG);
    fail_commit = true; assert(pocket_config_brightness_save(30) == ESP_FAIL);
    assert(pocket_config_brightness_get() == 100); fail_commit = false;
    strcpy(brightness_stored, "bad");
    assert(pocket_config_init() == ESP_OK && pocket_config_brightness_get() == 75);
    pocket_config_get(&current); assert(!memcmp(&current, &original, sizeof(current)) && blob_writes == 0);
    puts("PC, page, screen and brightness preferences: PASS (version-1 blob, restart, validation, commit failure, invalid optional key)");
    return 0;
}
