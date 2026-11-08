#include "pocket_config.h"
#include "pocket_json.h"
#include "pocket_screen.h"
#include "pocket_setup_guard.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static SemaphoreHandle_t s_lock;
static pocket_config_t s_config;
static pocket_pc_display_t s_pc_display;
static pocket_page_layout_t s_page_layout;
static unsigned s_screen_timeout = 60;
static unsigned s_brightness = 75;
static bool config_valid(const pocket_config_t *config);

esp_err_t pocket_config_init(void) {
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) return error; /* Never erase existing user NVS automatically. */
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_config, 0, sizeof(s_config));
    s_config.version = 1;
    pocket_pc_display_default(&s_pc_display);
    pocket_page_layout_default(&s_page_layout);
    s_screen_timeout = 60;
    s_brightness = 75;
    snprintf(s_config.nas_host, sizeof(s_config.nas_host), "192.168.1.250");
    nvs_handle_t store;
    error = nvs_open("pocket_notes", NVS_READWRITE, &store);
    if (error != ESP_OK) return error;
    size_t size = sizeof(s_config);
    pocket_config_t *saved = malloc(sizeof(*saved));
    if (!saved) { nvs_close(store); return ESP_ERR_NO_MEM; }
    error = nvs_get_blob(store, "configuration", saved, &size);
    if (error == ESP_OK && size == sizeof(*saved) && saved->version == 1 && config_valid(saved)) s_config = *saved;
    else if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    else if (error == ESP_OK) error = ESP_ERR_INVALID_VERSION;
    memset(saved, 0, sizeof(*saved)); free(saved);
    if (error == ESP_OK) {
        char selection[POCKET_PC_DISPLAY_BYTES];
        size_t selection_size = sizeof(selection);
        if (nvs_get_str(store, "pc_display", selection, &selection_size) == ESP_OK)
            (void)pocket_pc_display_parse(selection, &s_pc_display);
        /* Missing/invalid optional preference falls back without modifying stored credentials. */
        selection_size = sizeof(selection);
        if (nvs_get_str(store, "page_layout", selection, &selection_size) == ESP_OK)
            (void)pocket_page_layout_parse(selection, &s_page_layout);
        char timeout[8]; size_t timeout_size = sizeof(timeout);
        char brightness[8]; size_t brightness_size = sizeof(brightness);
        if (nvs_get_str(store, "brightness", brightness, &brightness_size) == ESP_OK)
            (void)pocket_screen_brightness_parse(brightness, &s_brightness);
        if (nvs_get_str(store, "screen_timeout", timeout, &timeout_size) == ESP_OK) {
            char *end; unsigned long seconds = strtoul(timeout, &end, 10);
            if (timeout[0] && !*end && seconds <= 300 && pocket_screen_timeout_valid((unsigned)seconds)) s_screen_timeout = (unsigned)seconds;
        }
    }
    nvs_close(store);
    return error;
}

void pocket_config_get(pocket_config_t *config) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *config = s_config;
    xSemaphoreGive(s_lock);
}

void pocket_config_pc_display_get(pocket_pc_display_t *display) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *display = s_pc_display;
    xSemaphoreGive(s_lock);
}

esp_err_t pocket_config_pc_display_save(const char *selection) {
    pocket_pc_display_t display;
    if (!pocket_pc_display_parse(selection, &display)) return ESP_ERR_INVALID_ARG;
    char canonical[POCKET_PC_DISPLAY_BYTES];
    if (!pocket_pc_display_format(&display, canonical, sizeof(canonical))) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t store;
    esp_err_t error = nvs_open("pocket_notes", NVS_READWRITE, &store);
    if (error == ESP_OK) {
        error = nvs_set_str(store, "pc_display", canonical);
        if (error == ESP_OK) error = nvs_commit(store);
        nvs_close(store);
    }
    if (error == ESP_OK) s_pc_display = display;
    xSemaphoreGive(s_lock);
    return error;
}

void pocket_config_page_layout_get(pocket_page_layout_t *display) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *display = s_page_layout;
    xSemaphoreGive(s_lock);
}

esp_err_t pocket_config_page_layout_save(const char *selection) {
    pocket_page_layout_t display;
    if (!pocket_page_layout_parse(selection, &display)) return ESP_ERR_INVALID_ARG;
    char canonical[POCKET_PAGE_LAYOUT_BYTES];
    if (!pocket_page_layout_format(&display, canonical, sizeof(canonical))) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t store;
    esp_err_t error = nvs_open("pocket_notes", NVS_READWRITE, &store);
    if (error == ESP_OK) {
        error = nvs_set_str(store, "page_layout", canonical);
        if (error == ESP_OK) error = nvs_commit(store);
        nvs_close(store);
    }
    if (error == ESP_OK) s_page_layout = display;
    xSemaphoreGive(s_lock);
    return error;
}

unsigned pocket_config_screen_timeout_get(void) {
    xSemaphoreTake(s_lock,portMAX_DELAY); unsigned result=s_screen_timeout; xSemaphoreGive(s_lock); return result;
}
esp_err_t pocket_config_screen_timeout_save(unsigned seconds) {
    if(!pocket_screen_timeout_valid(seconds)) return ESP_ERR_INVALID_ARG;
    char text[8];snprintf(text,sizeof(text),"%u",seconds);
    xSemaphoreTake(s_lock,portMAX_DELAY); nvs_handle_t store;
    esp_err_t error=nvs_open("pocket_notes",NVS_READWRITE,&store);
    if(error==ESP_OK) { error=nvs_set_str(store,"screen_timeout",text);if(error==ESP_OK)error=nvs_commit(store);nvs_close(store); }
    if(error==ESP_OK)s_screen_timeout=seconds;
    xSemaphoreGive(s_lock);return error;
}

unsigned pocket_config_brightness_get(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned result = s_brightness;
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t pocket_config_brightness_save(unsigned percent) {
    if (percent < 10 || percent > 100) return ESP_ERR_INVALID_ARG;
    char text[4];
    snprintf(text, sizeof(text), "%u", percent);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t store;
    esp_err_t error = nvs_open("pocket_notes", NVS_READWRITE, &store);
    if (error == ESP_OK) {
        error = nvs_set_str(store, "brightness", text);
        if (error == ESP_OK) error = nvs_commit(store);
        nvs_close(store);
    }
    if (error == ESP_OK) s_brightness = percent;
    xSemaphoreGive(s_lock);
    return error;
}

static esp_err_t store_locked(const pocket_config_t *config) {
    nvs_handle_t store;
    esp_err_t error = nvs_open("pocket_notes", NVS_READWRITE, &store);
    if (error == ESP_OK) {
        error = nvs_set_blob(store, "configuration", config, sizeof(*config));
        if (error == ESP_OK) error = nvs_commit(store);
        nvs_close(store);
    }
    if (error == ESP_OK) s_config = *config;
    return error;
}

esp_err_t pocket_config_save(const pocket_config_t *config) {
    if (!config_valid(config)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t error = store_locked(config);
    xSemaphoreGive(s_lock);
    return error;
}

typedef struct { const char *key; size_t offset, size; } field_t;
#define FIELD(name) {#name, offsetof(pocket_config_t, name), sizeof(((pocket_config_t *)0)->name)}
static const field_t FIELDS[] = {
    FIELD(wifi_ssid), FIELD(wifi_password), FIELD(companion_url), FIELD(companion_token),
    FIELD(nas_host), FIELD(nas_community), FIELD(genshin_uid), FIELD(genshin_region),
    FIELD(genshin_cookie), FIELD(genshin_widget_cookie), FIELD(genshin_device_id),
    FIELD(genshin_device_fp), FIELD(home_url), FIELD(home_token),
};

static bool config_valid(const pocket_config_t *config) {
    if (config->version != 1) return false;
    for (unsigned i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i) {
        const char *value = (const char *)config + FIELDS[i].offset;
        if (!memchr(value, '\0', FIELDS[i].size) || strpbrk(value, "\r\n")) return false;
    }
    if (!pocket_setup_wifi_valid(config->wifi_ssid, config->wifi_password)) return false;
    const char *urls[] = {config->companion_url, config->home_url};
    for (unsigned i = 0; i < 2; ++i) {
        if (urls[i][0] && strncmp(urls[i], "http://", 7) && strncmp(urls[i], "https://", 8)) return false;
        if (strpbrk(urls[i], "@?#")) return false;
    }
    for (const char *p = config->genshin_uid; *p; ++p) if (*p < '0' || *p > '9') return false;
    for (const char *p = config->genshin_region; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    return true;
}

esp_err_t pocket_config_update(const char *json) {
    if (!pocket_json_flat_object(json, strlen(json))) return ESP_ERR_INVALID_ARG;
    cJSON *document = cJSON_Parse(json);
    if (!cJSON_IsObject(document)) { cJSON_Delete(document); return ESP_ERR_INVALID_ARG; }
    pocket_config_t *next = malloc(sizeof(*next));
    if (!next) { cJSON_Delete(document); return ESP_ERR_NO_MEM; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *next = s_config;
    esp_err_t error = ESP_OK;
    cJSON *value;
    cJSON_ArrayForEach(value, document) {
        bool found = false;
        for (unsigned i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i) {
            const field_t *field = &FIELDS[i];
            if (strcmp(value->string, field->key) != 0) continue;
            found = true;
            if (!cJSON_IsString(value) || strlen(value->valuestring) >= field->size ||
                strpbrk(value->valuestring, "\r\n") != NULL) { error = ESP_ERR_INVALID_ARG; break; }
            snprintf((char *)next + field->offset, field->size, "%s", value->valuestring);
        }
        if (!found || error != ESP_OK) { error = ESP_ERR_INVALID_ARG; break; }
    }
    if (error == ESP_OK) error = config_valid(next) ? store_locked(next) : ESP_ERR_INVALID_ARG;
    xSemaphoreGive(s_lock);
    memset(next, 0, sizeof(*next));
    free(next);
    cJSON_Delete(document);
    return error;
}

typedef struct { uint32_t version; pocket_snapshot_t snapshot; } cache_t;

static bool cache_valid(const pocket_snapshot_t *snapshot) {
    if (!snapshot->has_data || snapshot->updated_at <= 0 || snapshot->updated_at >= 4102444800LL) return false;
    for (unsigned i = 0; i < POCKET_ROW_COUNT; ++i)
        if (!memchr(snapshot->value[i], '\0', POCKET_VALUE_BYTES)) return false;
    return memchr(snapshot->message, '\0', sizeof(snapshot->message)) != NULL;
}

esp_err_t pocket_cache_load(pocket_page_t page, pocket_snapshot_t *snapshot) {
    if ((unsigned)page >= POCKET_PAGE_COUNT) return ESP_ERR_INVALID_ARG;
    char key[12]; snprintf(key, sizeof(key), "cache%u", (unsigned)page);
    cache_t cached; size_t size = sizeof(cached);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t store;
    esp_err_t error = nvs_open("pocket_notes", NVS_READONLY, &store);
    if (error == ESP_OK) { error = nvs_get_blob(store, key, &cached, &size); nvs_close(store); }
    xSemaphoreGive(s_lock);
    if (error == ESP_OK && size == sizeof(cached) && cached.version == 1 && cache_valid(&cached.snapshot)) {
        *snapshot = cached.snapshot;
        snapshot->status = POCKET_STALE;
        snprintf(snapshot->message, sizeof(snapshot->message), "缓存数据，等待更新");
        return ESP_OK;
    }
    return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
}

esp_err_t pocket_cache_save(pocket_page_t page, const pocket_snapshot_t *snapshot) {
    if ((unsigned)page >= POCKET_PAGE_COUNT || !cache_valid(snapshot)) return ESP_ERR_INVALID_ARG;
    cache_t cached = {.version = 1, .snapshot = *snapshot};
    char key[12]; snprintf(key, sizeof(key), "cache%u", (unsigned)page);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t store;
    esp_err_t error = nvs_open("pocket_notes", NVS_READWRITE, &store);
    if (error == ESP_OK) {
        error = nvs_set_blob(store, key, &cached, sizeof(cached));
        if (error == ESP_OK) error = nvs_commit(store);
        nvs_close(store);
    }
    xSemaphoreGive(s_lock);
    return error;
}
