#include "pocket_model.h"
#include "pocket_screen.h"
#include "pocket_confirm.h"
#include "pocket_ui.h"
#include "pocket_config.h"
#include "pocket_genshin.h"
#include "pocket_home.h"
#include "pocket_services.h"
#include "pocket_setup.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef enum { FETCH, LOGIN, VERIFY, HOME_FETCH, HOME_EXECUTE, SETTINGS_OPEN, SETTINGS_CLOSE, DISPLAY_SAVE } operation_t;
typedef struct {
    operation_t operation;
    pocket_page_t page;
    unsigned generation, offset, action, display_save, display_value;
    pocket_home_entity_t entity;
} command_t;
typedef struct {
    operation_t operation;
    pocket_page_t page;
    bool progress;
    bool command_accepted;
    unsigned generation;
    esp_err_t error;
    int api_code;
    pocket_snapshot_t snapshot;
    pocket_pc_data_t pc_data;
    pocket_home_batch_t home;
    char qr[1024];
} result_t;
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } pocket_key_t;
typedef enum { OVERVIEW, DEVICES, ACTIONS, CONFIRM } home_mode_t;

static const char *TAG = "pocket";
static QueueHandle_t s_commands, s_results, s_keys;
static EventGroupHandle_t s_network;
static atomic_uint s_login_generation;
static pocket_model_t s_model;
static pocket_pc_display_t s_pc_display;
static pocket_pc_data_t s_pc_data;
static unsigned s_pc_subpage;
static pocket_screen_t s_screen;
static pocket_display_menu_t s_display_menu;
static pocket_home_batch_t s_home;
static unsigned s_entity_index, s_action_index;
static home_mode_t s_home_mode;
static char s_qr[1024], s_home_feedback[64];
static unsigned s_wifi_retries;
static bool s_storage_ready;
static pocket_confirm_t s_confirmation;
static int64_t s_home_updated_ms;
static pocket_home_entity_t s_confirm_entity;
static unsigned s_confirm_action;
static bool s_genshin_verification_needed;
static unsigned s_home_generation;
static atomic_bool s_suppress_initial_key;
static int64_t s_initial_key_deadline;
static atomic_bool s_wifi_configured;

static void network_event(void *context, esp_event_base_t base, int32_t id, void *event) {
    (void)context; (void)event;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retries = 0; xEventGroupSetBits(s_network, 1);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (atomic_load(&s_wifi_configured)) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_network, 1);
        if (atomic_load(&s_wifi_configured) && s_wifi_retries++ < 5) esp_wifi_connect();
    }
}

static esp_err_t network_init(const pocket_config_t *config) {
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t settings = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&settings);
    if (error != ESP_OK) return error;
    error = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    if (error != ESP_OK) return error;
    error = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL);
    if (error != ESP_OK) return error;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) return error;
    atomic_store(&s_wifi_configured, config->wifi_ssid[0] != '\0');
    wifi_config_t credentials = {0};
    memcpy(credentials.sta.ssid, config->wifi_ssid, strlen(config->wifi_ssid));
    memcpy(credentials.sta.password, config->wifi_password, strlen(config->wifi_password));
    error = config->wifi_ssid[0] ? esp_wifi_set_config(WIFI_IF_STA, &credentials) : ESP_OK;
    memset(&credentials, 0, sizeof(credentials));
    if (error != ESP_OK) return error;
    error = esp_wifi_start();
    if (error != ESP_OK) return error;
    if (!config->wifi_ssid[0]) { (void)pocket_setup_open(false); return ESP_OK; }
    esp_sntp_config_t clock = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    error = esp_netif_sntp_init(&clock);
    if (error != ESP_OK) ESP_LOGW(TAG, "Network clock initialization failed");
    return ESP_OK;
}

static bool login_progress(const char *qr, const char *message, void *context) {
    const command_t *command = context;
    unsigned generation = command->generation;
    if (generation != atomic_load(&s_login_generation)) return false;
    result_t *result = calloc(1, sizeof(*result));
    if (!result) return false;
    result->operation = command->operation; result->page = POCKET_GENSHIN;
    result->progress = true; result->generation = generation;
    snprintf(result->qr, sizeof(result->qr), "%s", qr);
    snprintf(result->snapshot.message, sizeof(result->snapshot.message), "%s", message);
    bool sent = xQueueSend(s_results, result, pdMS_TO_TICKS(200)) == pdTRUE;
    free(result);
    return sent;
}

static void failure_message(result_t *result, int api_code) {
    result->snapshot.status = POCKET_FAILED;
    const char *message = "连接失败，旧数据保留";
    if (result->error == ESP_ERR_INVALID_STATE) {
        result->snapshot.status = POCKET_UNCONFIGURED;
        message = result->page == POCKET_GENSHIN ? "长按确定扫码登录" : "长按下键打开手机设置";
    } else if (result->error == ESP_ERR_NOT_SUPPORTED && result->page == POCKET_NAS) message = "NAS 未提供监控指标";
    else if (result->error == ESP_ERR_NOT_SUPPORTED && result->page == POCKET_PC) message = "请重启新版电脑助手";
    else if (result->page == POCKET_PC && result->error == ESP_ERR_INVALID_RESPONSE) message = "电脑尚未读取到资源";
    else if (result->page == POCKET_CODEX && result->error == ESP_ERR_INVALID_RESPONSE) message = "电脑尚未读取到额度";
    if (api_code == -100 || api_code == 10001 || api_code == 1034 || api_code == 10035 || api_code == 10041 || api_code == -3101) {
        result->snapshot.status = POCKET_AUTH_REQUIRED;
        message = api_code == 1034 || api_code == 10035 || api_code == 10041 || api_code == -3101 ? "长按确定完成安全验证" : "登录失效，长按确定";
    }
    snprintf(result->snapshot.message, sizeof(result->snapshot.message), "%s", message);
    if (result->page == POCKET_GENSHIN && result->snapshot.status == POCKET_FAILED)
        pocket_genshin_last_error(result->snapshot.message, sizeof(result->snapshot.message), result->error);
}

static void network_worker(void *context) {
    (void)context;
    pocket_config_t *config = malloc(sizeof(*config));
    result_t *result = malloc(sizeof(*result));
    command_t *command = malloc(sizeof(*command));
    if (!config || !result || !command) { free(config); free(result); free(command); vTaskDelete(NULL); return; }
    pocket_config_get(config);
    esp_err_t initialized = network_init(config);
    int64_t last_cached[3] = {0};
    int64_t started = esp_timer_get_time();
    bool fallback_opened = !config->wifi_ssid[0];
    for (;;) {
        pocket_setup_tick();
        if (initialized == ESP_OK && !fallback_opened && !(xEventGroupGetBits(s_network) & 1) &&
            esp_timer_get_time() - started >= 30000000) {
            fallback_opened = true; (void)pocket_setup_open(false);
        }
        if (xQueueReceive(s_commands, command, pdMS_TO_TICKS(250)) != pdTRUE) continue;
        if (command->operation == DISPLAY_SAVE) {
            memset(result, 0, sizeof(*result)); result->operation = DISPLAY_SAVE;
            result->error = command->display_save == POCKET_DISPLAY_SAVE_BRIGHTNESS ?
                pocket_config_brightness_save(command->display_value) : pocket_config_screen_timeout_save(command->display_value);
            xQueueSend(s_results, result, portMAX_DELAY);
            continue;
        }
        if (command->operation == SETTINGS_OPEN || command->operation == SETTINGS_CLOSE) {
            esp_err_t error = command->operation == SETTINGS_OPEN ?
                (initialized == ESP_OK ? pocket_setup_open((xEventGroupGetBits(s_network) & 1) != 0) : initialized) : pocket_setup_close();
            if (error != ESP_OK) {
                memset(result, 0, sizeof(*result)); result->page = command->page; result->error = error;
                result->snapshot.status = POCKET_FAILED;
                snprintf(result->snapshot.message, sizeof(result->snapshot.message), "设置暂不可用，请重试");
                xQueueSend(s_results, result, portMAX_DELAY);
            }
            continue;
        }
        if (pocket_setup_active()) {
            memset(result, 0, sizeof(*result)); result->page = command->page; result->operation = command->operation;
            result->error = ESP_ERR_INVALID_STATE; result->snapshot.status = POCKET_UNCONFIGURED;
            snprintf(result->snapshot.message, sizeof(result->snapshot.message), "手机设置期间暂停更新");
            xQueueSend(s_results, result, portMAX_DELAY); continue;
        }
        if ((command->operation == LOGIN || command->operation == VERIFY) && command->generation != atomic_load(&s_login_generation)) continue;
        memset(result, 0, sizeof(*result));
        result->page = command->page; result->operation = command->operation; result->generation = command->generation;
        result->snapshot.status = POCKET_READY;
        pocket_config_get(config);
        int api_code = 0;
        if (initialized != ESP_OK) result->error = initialized;
        else if (!config->wifi_ssid[0]) {
            result->error = ESP_ERR_INVALID_STATE;
            result->snapshot.status = POCKET_UNCONFIGURED;
            snprintf(result->snapshot.message, sizeof(result->snapshot.message), "长按下键用手机配网");
        }
        else if (!(xEventGroupGetBits(s_network) & 1)) {
            result->error = ESP_ERR_TIMEOUT;
            esp_wifi_connect();
            result->snapshot.status = POCKET_WAITING_NETWORK;
            snprintf(result->snapshot.message, sizeof(result->snapshot.message), "等待 Wi-Fi 连接");
        } else if (command->operation == LOGIN || command->operation == VERIFY) {
            if (time(NULL) < 1700000000) {
                result->error = ESP_ERR_TIMEOUT;
                result->snapshot.status = POCKET_WAITING_NETWORK;
                snprintf(result->snapshot.message, sizeof(result->snapshot.message), "等待网络校时");
            }
            else result->error = command->operation == VERIFY ?
                pocket_genshin_verify(login_progress, command, &api_code) : pocket_genshin_login(login_progress, command, &api_code);
            if (result->error == ESP_OK) {
                pocket_config_get(config);
                result->error = pocket_genshin_refresh(config, &result->snapshot, &api_code);
            }
        } else if (command->operation == HOME_EXECUTE) {
            result->error = pocket_home_execute(config, &command->entity, command->action);
            result->command_accepted = result->error == ESP_OK;
            if (result->command_accepted) result->error = pocket_home_list(config, command->offset, &result->home);
        } else if (command->page == POCKET_GENSHIN) {
            result->error = time(NULL) < 1700000000 ? ESP_ERR_TIMEOUT : pocket_genshin_refresh(config, &result->snapshot, &api_code);
            if (time(NULL) < 1700000000) {
                result->snapshot.status = POCKET_WAITING_NETWORK;
                snprintf(result->snapshot.message, sizeof(result->snapshot.message), "等待网络校时");
            }
        } else if (command->page == POCKET_CODEX) result->error = pocket_codex_refresh(config, &result->snapshot);
        else if (command->page == POCKET_PC) result->error = pocket_pc_refresh(config, &result->snapshot, &result->pc_data);
        else if (command->page == POCKET_NAS) result->error = pocket_nas_refresh(config, &result->snapshot);
        else result->error = pocket_home_list(config, command->offset, &result->home);
        if (result->error != ESP_OK && !result->snapshot.message[0]) failure_message(result, api_code);
        result->api_code = api_code;
        if (result->page != POCKET_CODEX && result->page != POCKET_PC && time(NULL) > 1700000000) result->snapshot.updated_at = time(NULL);
        int64_t now = esp_timer_get_time() / 1000;
        if (result->error == ESP_OK && result->page < POCKET_MIHOME && result->snapshot.status == POCKET_READY &&
            (!last_cached[result->page] || now - last_cached[result->page] >= 300000)) {
            result->snapshot.has_data = true;
            if (pocket_cache_save(result->page, &result->snapshot) == ESP_OK) last_cached[result->page] = now;
        }
        xQueueSend(s_results, result, portMAX_DELAY);
    }
}

static void on_pocket_key(bsp_btn_t button, bsp_btn_ev_t event, void *context) {
    (void)context;
    if (atomic_load(&s_suppress_initial_key)) {
        if (event == BSP_BTN_RELEASE || esp_timer_get_time() >= s_initial_key_deadline)
            atomic_store(&s_suppress_initial_key, false);
        return;
    }
    if ((unsigned)event > BSP_BTN_RELEASE) return;
    pocket_key_t key = {.button = button, .event = event};
    xQueueSend(s_keys, &key, 0);
}

static pocket_home_entity_t *selected_entity(void) {
    if (s_entity_index < s_home.offset || s_entity_index - s_home.offset >= s_home.count) return NULL;
    return &s_home.entity[s_entity_index - s_home.offset];
}

static void text_value(char *output, const char *value) {
    size_t length = strlen(value);
    if (length >= POCKET_VALUE_BYTES) {
        length = POCKET_VALUE_BYTES - 4;
        while (length && (((unsigned char)value[length] & 0xc0) == 0x80)) --length;
        memcpy(output, value, length); memcpy(output + length, "...", 4);
    } else memcpy(output, value, length + 1);
}

static const char *home_state_label(const char *state) {
    static const struct { const char *value, *label; } names[] = {
        {"on", "已开启"}, {"off", "已关闭"}, {"unavailable", "离线"}, {"unknown", "未知"},
        {"heat", "制热"}, {"cool", "制冷"}, {"auto", "自动"}, {"idle", "待机"},
        {"cleaning", "清扫中"}, {"docked", "已回充"}, {"paused", "已暂停"},
        {"open", "已打开"}, {"closed", "已关闭"}, {"opening", "打开中"}, {"closing", "关闭中"},
        {"playing", "播放中"}, {"locked", "已上锁"}, {"unlocked", "未上锁"},
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (!strcmp(state, names[i].value)) return names[i].label;
    return state;
}

static void home_snapshot(void) {
    pocket_snapshot_t *snapshot = &s_model.snapshot[POCKET_MIHOME];
    pocket_home_entity_t *entity = s_home_mode == CONFIRM ? &s_confirm_entity : selected_entity();
    if (!entity) {
        for (unsigned i = 0; i < POCKET_ROW_COUNT; ++i) snprintf(snapshot->value[i], POCKET_VALUE_BYTES, "--");
        snprintf(snapshot->value[0], POCKET_VALUE_BYTES, "%u 个项目", s_home.total);
        if (snapshot->has_data && !s_home.total) snprintf(snapshot->message, sizeof(snapshot->message), "请在 HA 接入米家设备");
        return;
    }
    text_value(snapshot->value[0], entity->name);
    text_value(snapshot->value[1], home_state_label(entity->state));
    pocket_home_action_t actions[POCKET_HOME_ACTIONS];
    unsigned count = pocket_home_actions(entity, actions);
    unsigned action_index = s_home_mode == CONFIRM ? s_confirm_action : s_action_index;
    if (action_index >= count) action_index = s_action_index = 0;
    text_value(snapshot->value[2], count ? actions[action_index].label : "只读或暂不支持");
    if (count && actions[action_index].has_number)
        snprintf(snapshot->value[3], POCKET_VALUE_BYTES, "%.1f", actions[action_index].number);
    else snprintf(snapshot->value[3], POCKET_VALUE_BYTES, "%u/%u", s_entity_index + 1, s_home.total);
    text_value(snapshot->value[4], s_home_feedback[0] ? s_home_feedback : "尚未执行");
    snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "%s", s_home_mode == CONFIRM ? "确定后执行" : "长按确定返回");
}

static bool enqueue(command_t *command) {
    if (command->page == POCKET_MIHOME && command->operation != HOME_EXECUTE)
        command->generation = ++s_home_generation;
    if (xQueueSend(s_commands, command, 0) == pdTRUE) return true;
    pocket_model_failure(&s_model, command->page, POCKET_FAILED, "网络任务繁忙");
    return false;
}

static void request_page(void) {
    if (pocket_setup_active()) return;
    if (!pocket_model_request(&s_model, esp_timer_get_time() / 1000)) return;
    command_t command = {.operation = FETCH, .page = s_model.page, .offset = s_entity_index / POCKET_HOME_BATCH * POCKET_HOME_BATCH};
    enqueue(&command);
}

static void process_key(const pocket_key_t *key) {
    if(pocket_screen_key(&s_screen,(unsigned)key->event,esp_timer_get_time()/1000)) return;
    if (s_display_menu.open) {
        unsigned save = pocket_display_menu_key(&s_display_menu, key->button, key->event);
        if (save) {
            command_t command = {.operation=DISPLAY_SAVE, .display_save=save,
                .display_value=save == POCKET_DISPLAY_SAVE_BRIGHTNESS ? s_display_menu.brightness : s_display_menu.timeout};
            /* Local settings must not mark a business page as loading. */
            if (xQueueSend(s_commands, &command, 0) != pdTRUE) pocket_display_menu_saved(&s_display_menu, false);
        }
        return;
    }
    if (key->button == BSP_BTN_OK && key->event == BSP_BTN_DOUBLE && !pocket_setup_active() &&
        !s_qr[0] && s_home_mode == OVERVIEW && !s_confirmation.busy &&
        !(s_model.page == POCKET_GENSHIN && s_model.snapshot[POCKET_GENSHIN].status == POCKET_LOADING)) {
        pocket_display_menu_open(&s_display_menu, pocket_config_brightness_get(), pocket_config_screen_timeout_get());
        return;
    }
    bool ok = key->button == BSP_BTN_OK;
    bool long_press = key->event == BSP_BTN_LONG;
    if (pocket_setup_active()) {
        if (ok && long_press) {
            command_t command = {.operation = SETTINGS_CLOSE, .page = s_model.page}; enqueue(&command);
        } else if (ok && key->event == BSP_BTN_CLICK) pocket_setup_toggle();
        return;
    }
    if (long_press && key->button == BSP_BTN_DOWN) {
        if (s_confirmation.busy) return;
        pocket_confirm_cancel(&s_confirmation); atomic_fetch_add(&s_login_generation, 1); s_qr[0] = '\0';
        command_t command = {.operation = SETTINGS_OPEN, .page = s_model.page}; enqueue(&command); return;
    }
    if(long_press && key->button==BSP_BTN_UP && s_model.page!=POCKET_MIHOME && !s_qr[0] && !s_confirmation.busy) {
        pocket_screen_off(&s_screen,esp_timer_get_time()/1000);return;
    }
    if (ok && long_press) {
        if (s_model.page == POCKET_MIHOME) { pocket_confirm_cancel(&s_confirmation); s_home_mode = OVERVIEW; home_snapshot(); return; }
        if (s_model.page == POCKET_GENSHIN) {
            unsigned generation = atomic_fetch_add(&s_login_generation, 1) + 1;
            command_t command = {.operation = s_genshin_verification_needed ? VERIFY : LOGIN, .page = POCKET_GENSHIN, .generation = generation};
            s_qr[0] = '\0';
            if (enqueue(&command)) pocket_model_failure(&s_model, POCKET_GENSHIN, POCKET_LOADING, "正在准备扫码登录");
        }
        return;
    }
    if (long_press) {
        if (s_model.page == POCKET_MIHOME && key->button == BSP_BTN_UP && !s_confirmation.busy) {
            pocket_confirm_cancel(&s_confirmation); s_home_mode = OVERVIEW; request_page(); home_snapshot();
        }
        return;
    }
    if (s_model.page == POCKET_MIHOME && s_home_mode != OVERVIEW) {
        if (s_confirmation.busy) return;
        pocket_home_entity_t *entity = selected_entity();
        if (!ok) {
            if (s_home_mode == CONFIRM) { pocket_confirm_cancel(&s_confirmation); s_home_mode = ACTIONS; home_snapshot(); return; }
            if (s_home_mode == DEVICES) {
                if (key->button == BSP_BTN_UP && s_entity_index) --s_entity_index;
                if (key->button == BSP_BTN_DOWN && s_entity_index + 1 < s_home.total) ++s_entity_index;
                s_action_index = 0;
                if (!selected_entity()) {
                    command_t command = {.operation = HOME_FETCH, .page = POCKET_MIHOME, .offset = s_entity_index / POCKET_HOME_BATCH * POCKET_HOME_BATCH};
                    enqueue(&command);
                }
            } else if (entity) {
                pocket_home_action_t actions[POCKET_HOME_ACTIONS];
                unsigned count = pocket_home_actions(entity, actions);
                if (key->button == BSP_BTN_UP && s_action_index) --s_action_index;
                if (key->button == BSP_BTN_DOWN && s_action_index + 1 < count) ++s_action_index;
            }
        } else if (entity) {
            if (s_confirmation.busy) return;
            pocket_home_action_t actions[POCKET_HOME_ACTIONS];
            unsigned count = pocket_home_actions(entity, actions);
            if (!count) return;
            if (s_home_mode == DEVICES) s_home_mode = ACTIONS;
            else if (s_home_mode == ACTIONS) {
                if (s_model.snapshot[POCKET_MIHOME].status != POCKET_READY || esp_timer_get_time() / 1000 - s_home_updated_ms > 60000) {
                    snprintf(s_home_feedback, sizeof(s_home_feedback), "请先刷新设备状态");
                } else if (pocket_confirm_arm(&s_confirmation, entity->id, s_action_index, esp_timer_get_time() / 1000)) {
                    s_confirm_entity = *entity; s_confirm_action = s_action_index; s_home_mode = CONFIRM;
                }
            }
            else {
                if (strcmp(entity->id, s_confirm_entity.id) || esp_timer_get_time() / 1000 - s_home_updated_ms > 60000 || s_model.snapshot[POCKET_MIHOME].status != POCKET_READY) {
                    snprintf(s_home_feedback, sizeof(s_home_feedback), "设备状态已变化");
                    pocket_confirm_cancel(&s_confirmation); s_home_mode = ACTIONS; home_snapshot(); return;
                }
                if (!pocket_confirm_consume(&s_confirmation, s_confirm_entity.id, s_confirm_action, esp_timer_get_time() / 1000)) {
                    snprintf(s_home_feedback, sizeof(s_home_feedback), "确认已过期，请重试");
                    s_home_mode = ACTIONS; home_snapshot(); return;
                }
                command_t command = {.operation = HOME_EXECUTE, .page = POCKET_MIHOME, .offset = s_home.offset, .action = s_confirm_action, .entity = s_confirm_entity};
                if (enqueue(&command)) snprintf(s_home_feedback, sizeof(s_home_feedback), "正在提交");
                else pocket_confirm_complete(&s_confirmation);
                s_home_mode = ACTIONS;
            }
        }
        home_snapshot();
        return;
    }
    if (!ok) {
        atomic_fetch_add(&s_login_generation, 1); s_qr[0] = '\0';
        pocket_confirm_cancel(&s_confirmation);
        int direction = key->button == BSP_BTN_UP ? -1 : 1;
        pocket_pc_navigate(&s_model,&s_pc_subpage,pocket_pc_pages(&s_pc_display),direction);
        if (key->event == BSP_BTN_DOUBLE) pocket_pc_navigate(&s_model,&s_pc_subpage,pocket_pc_pages(&s_pc_display),direction);
        s_home_mode = OVERVIEW;
        request_page();
    } else if (s_model.page == POCKET_MIHOME) {
        s_home_mode = DEVICES;
        if (!s_home.total) request_page();
        home_snapshot();
    } else request_page();
}

static void ui_worker(void *context) {
    (void)context;
    result_t *result = malloc(sizeof(*result));
    if (!result) { vTaskDelete(NULL); return; }
    int battery = -1;
    int64_t next_battery = 0;
    bool was_setup = false;
    unsigned setup_revision = 0;
    bool backlight_off=false;
    unsigned brightness = pocket_config_brightness_get();
    unsigned applied_brightness = brightness;
    const int64_t intervals[POCKET_PAGE_COUNT] = {300000, 60000, 30000, 30000, 30000};
    request_page();
    for (;;) {
        bool dirty = false;
        pocket_key_t key;
        while (xQueueReceive(s_keys, &key, 0) == pdTRUE) { process_key(&key); dirty = true; }
        while (xQueueReceive(s_results, result, 0) == pdTRUE) {
            if (result->operation == DISPLAY_SAVE) {
                pocket_display_menu_saved(&s_display_menu, result->error == ESP_OK);
                brightness = pocket_config_brightness_get();
                s_screen.timeout_seconds = pocket_config_screen_timeout_get();
                dirty = true;
                continue;
            }
            if (result->page == POCKET_MIHOME && result->operation != HOME_EXECUTE && result->generation != s_home_generation) continue;
            if ((result->operation == LOGIN || result->operation == VERIFY) && result->generation != atomic_load(&s_login_generation)) continue;
            if (result->progress) {
                snprintf(s_qr, sizeof(s_qr), "%s", result->qr);
                pocket_model_failure(&s_model, POCKET_GENSHIN, POCKET_LOADING, result->snapshot.message);
            } else {
                if (result->operation == HOME_EXECUTE) pocket_confirm_complete(&s_confirmation);
                if (result->operation == LOGIN || result->operation == VERIFY) s_qr[0] = '\0';
                if (result->page == POCKET_GENSHIN) {
                    if (result->error == ESP_OK) s_genshin_verification_needed = false;
                    else if (result->api_code == 1034 || result->api_code == 10035 || result->api_code == 10041)
                        s_genshin_verification_needed = true;
                }
                if (result->error == ESP_OK) {
                    pocket_model_success(&s_model, result->page, &result->snapshot, result->snapshot.updated_at);
                    if(result->page==POCKET_PC) s_pc_data=result->pc_data;
                    if (result->page == POCKET_MIHOME) {
                        s_home = result->home;
                        s_home_updated_ms = esp_timer_get_time() / 1000;
                        if (s_home.total && s_entity_index >= s_home.total) s_entity_index = s_home.total - 1;
                        if (result->operation == HOME_EXECUTE) snprintf(s_home_feedback, sizeof(s_home_feedback), "指令已提交");
                        home_snapshot();
                        if (s_home.total && !selected_entity()) {
                            command_t command = {.operation = HOME_FETCH, .page = POCKET_MIHOME, .offset = s_entity_index / POCKET_HOME_BATCH * POCKET_HOME_BATCH};
                            enqueue(&command);
                        }
                    }
                    if (result->snapshot.status == POCKET_STALE) pocket_model_failure(&s_model, result->page, POCKET_STALE, "电脑数据已过期");
                } else {
                    pocket_model_failure(&s_model, result->page, result->snapshot.status, result->snapshot.message);
                    if (result->operation == HOME_EXECUTE) { snprintf(s_home_feedback, sizeof(s_home_feedback), "%s", result->command_accepted ? "已提交，状态未确认" : "操作未完成"); home_snapshot(); }
                }
            }
            dirty = true;
        }
        int64_t now = esp_timer_get_time() / 1000;
        pocket_setup_view_t setup_view;
        bool in_setup = pocket_setup_view(&setup_view);
        if (in_setup && s_display_menu.open && !s_display_menu.saving) { s_display_menu.open = false; dirty = true; }
        if (in_setup != was_setup || (in_setup && setup_view.revision != setup_revision)) dirty = true;
        if (was_setup && !in_setup) {
            for (unsigned page = 0; page < POCKET_PAGE_COUNT; ++page) s_model.requested[page] = false;
        }
        was_setup = in_setup;
        if (in_setup) setup_revision = setup_view.revision;
        if (now >= next_battery) { battery = bsp_battery_soc(); next_battery = now + 30000; dirty = true; }
        bool paused_for_auth = s_model.page == POCKET_GENSHIN && (s_model.snapshot[POCKET_GENSHIN].status == POCKET_AUTH_REQUIRED || s_genshin_verification_needed);
        bool needs_setup = s_model.requested[s_model.page] && s_model.snapshot[s_model.page].status == POCKET_UNCONFIGURED;
        bool waiting_network = s_model.snapshot[s_model.page].status == POCKET_WAITING_NETWORK;
        if (!in_setup && !paused_for_auth && waiting_network && now - s_model.last_request_ms[s_model.page] >= 5000) {
            s_model.requested[s_model.page] = false;
            request_page(); dirty = true;
        } else if (!in_setup && !s_display_menu.open && !paused_for_auth && !needs_setup && (!s_model.requested[s_model.page] || now - s_model.last_request_ms[s_model.page] >= intervals[s_model.page])) { request_page(); dirty = true; }
        bool keep_awake=in_setup || s_display_menu.open || s_qr[0] || s_home_mode!=OVERVIEW || s_confirmation.busy;
        (void)pocket_screen_tick(&s_screen,keep_awake,now);
        if(pocket_setup_take_screen_off()) pocket_screen_off(&s_screen,now);
        unsigned requested_brightness = pocket_setup_take_brightness();
        if (requested_brightness) brightness = requested_brightness;
        unsigned target_brightness = s_screen.off ? 0 : s_display_menu.open ? s_display_menu.brightness : brightness;
        if (backlight_off != s_screen.off || applied_brightness != target_brightness) {
            backlight_off = s_screen.off;
            applied_brightness = target_brightness;
            bsp_display_backlight(target_brightness);
            dirty = true;
        }
        if(s_model.page==POCKET_PC && s_model.snapshot[POCKET_PC].has_data) {
            for(unsigned i=0;i<POCKET_ROW_COUNT;++i) {
                unsigned index=s_pc_subpage*POCKET_PC_ROWS+i;
                if(index<s_pc_display.count) pocket_pc_data_format(&s_pc_data,s_pc_display.metric[index],s_model.snapshot[POCKET_PC].value[i],POCKET_VALUE_BYTES);
                else s_model.snapshot[POCKET_PC].value[i][0]=0;
            }
        }
        if (dirty && !s_screen.off && bsp_lvgl_lock(200)) {
            if (s_display_menu.open && !in_setup) {
                pocket_ui_display_settings(&s_display_menu, battery);
                bsp_lvgl_unlock(); vTaskDelay(pdMS_TO_TICKS(50)); continue;
            }
            if (in_setup) {
                pocket_ui_setup(&setup_view, battery);
                bsp_lvgl_unlock(); vTaskDelay(pdMS_TO_TICKS(50)); continue;
            }
            pocket_ui_pc_page(s_pc_subpage);
            pocket_ui_render(&s_model, battery);
            const char *hint = "双击确定  屏幕设置";
            if (s_model.page == POCKET_MIHOME) {
                if (s_home_mode == OVERVIEW) hint = "上下翻页  确定选设备";
                else if (s_home_mode == DEVICES) hint = "上下选设备  确定操作";
                else if (s_home_mode == ACTIONS) hint = "上下选操作  确定预览";
                else hint = "确定执行  上下取消";
            }
            pocket_ui_hint(hint);
            pocket_ui_qr(s_model.page == POCKET_GENSHIN ? s_qr : "");
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void setup_worker(void *context) {
    (void)context;
    char *line = malloc(4097);
    if (!line) { vTaskDelete(NULL); return; }
    size_t used = 0; bool overflow = false;
    for (;;) {
        int character = getchar();
        if (character == EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (character == '\r') continue;
        if (character == '\n') {
            line[used] = '\0';
            if (used && !overflow && s_storage_ready && pocket_config_update(line) == ESP_OK) {
                memset(line, 0, 4097); printf("POCKET_CONFIG_OK\n"); fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(250)); esp_restart();
            } else if (used || overflow) { printf("POCKET_CONFIG_REJECTED\n"); fflush(stdout); }
            memset(line, 0, 4097); used = 0; overflow = false;
        } else if (used < 4096) line[used++] = (char)character;
        else overflow = true;
    }
}

void pocket_app_main(void) {
    setenv("TZ", "CST-8", 1); tzset();
    pocket_model_init(&s_model);
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) { ESP_LOGE(TAG, "Display initialization failed"); return; }
    bsp_display_backlight(75);
    if (!bsp_lvgl_lock(1000)) return;
    pocket_ui_create();
    bool fonts = pocket_ui_fonts_valid();
    pocket_ui_render(&s_model, -1);
    bsp_lvgl_unlock();
    if (!fonts) { ESP_LOGE(TAG, "Chinese glyph coverage check failed"); return; }
    esp_err_t storage = pocket_config_init();
    s_storage_ready = storage == ESP_OK;
    if (!s_storage_ready) { ESP_LOGE(TAG, "Configuration storage unavailable: %s", esp_err_to_name(storage)); return; }
    bsp_display_backlight(pocket_config_brightness_get());
    pocket_pc_display_t pc_display;
    pocket_config_pc_display_get(&pc_display);
    s_pc_display=pc_display;
    pocket_screen_init(&s_screen,pocket_config_screen_timeout_get(),esp_timer_get_time()/1000);
    pocket_page_layout_t page_layout;
    pocket_config_page_layout_get(&page_layout);
    (void)pocket_model_layout(&s_model, &page_layout);
    if (!bsp_lvgl_lock(1000)) return;
    pocket_ui_pc_display(&pc_display);
    bsp_lvgl_unlock();
    if (pocket_setup_init() != ESP_OK) { ESP_LOGE(TAG, "Setup initialization failed"); return; }
    for (unsigned page = 0; page < POCKET_MIHOME; ++page) (void)pocket_cache_load((pocket_page_t)page, &s_model.snapshot[page]);
    s_keys = xQueueCreate(8, sizeof(pocket_key_t));
    s_commands = xQueueCreate(4, sizeof(command_t));
    s_results = xQueueCreate(2, sizeof(result_t));
    s_network = xEventGroupCreate();
    if (!s_keys || !s_commands || !s_results || !s_network) { ESP_LOGE(TAG, "Queue allocation failed"); return; }
    (void)bsp_battery_init();
    if (xTaskCreate(network_worker, "pocket_network", 16384, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(ui_worker, "pocket_ui", 6144, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(setup_worker, "pocket_setup", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Application task allocation failed"); return;
    }
    atomic_store(&s_suppress_initial_key, true);
    s_initial_key_deadline = esp_timer_get_time() + 10000000;
    if (bsp_button_init(on_pocket_key, NULL) != ESP_OK) ESP_LOGE(TAG, "Button initialization failed");
    else if (bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN) atomic_store(&s_suppress_initial_key, false);
    ESP_LOGI(TAG, "Pocket Notes ready; settings are accepted over USB without echo");
}
