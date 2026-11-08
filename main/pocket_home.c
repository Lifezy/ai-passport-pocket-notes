#include "pocket_home.h"
#include "pocket_http.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static bool entity_id(const char *value) {
    unsigned dots = 0;
    if (!value || !*value || strlen(value) >= 128) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '.') ++dots;
        else if (!(*p >= 'a' && *p <= 'z') && !isdigit(*p) && *p != '_') return false;
    }
    const char *dot = strchr(value, '.');
    return dots == 1 && dot != value && dot[1] != '\0' && dot - value < 24;
}

static void copy_text(char *output, size_t capacity, const char *text) {
    size_t length = strlen(text);
    if (length >= capacity) {
        length = capacity - 1;
        while (length && (((unsigned char)text[length] & 0xc0) == 0x80)) --length;
    }
    memcpy(output, text, length); output[length] = '\0';
}

static const char *text(cJSON *object, const char *name) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(value) ? value->valuestring : "";
}

static bool scalar(cJSON *object, const char *name, double *number) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble)) return false;
    *number = value->valuedouble;
    return true;
}

static esp_err_t call(const pocket_config_t *config, const char *path, const char *body, char **response) {
    if (!config->home_url[0] || !config->home_token[0]) return ESP_ERR_INVALID_STATE;
    char url[320], authorization[528];
    size_t length = strlen(config->home_url);
    if (length && config->home_url[length - 1] == '/') --length;
    snprintf(url, sizeof(url), "%.*s%s", (int)length, config->home_url, path);
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->home_token);
    const pocket_header_t headers[] = {{"Authorization", authorization}};
    int status;
    esp_err_t error = pocket_http(url, HTTP_METHOD_POST, headers, 1, body, response, &status);
    memset(authorization, 0, sizeof(authorization));
    if (status == 401 || status == 403) return ESP_ERR_INVALID_STATE;
    return error;
}

esp_err_t pocket_home_list(const pocket_config_t *config, unsigned offset, pocket_home_batch_t *batch) {
    if (offset > 100000) return ESP_ERR_INVALID_ARG;
    /* Render four records on HA instead of downloading an unbounded /api/states. */
    static const char TEMPLATE[] =
        "{%% set ids = (integration_entities('xiaomi_home') + integration_entities('xiaomi_miio')) | unique | sort | list %%}"
        "{%% set ns = namespace(items=[]) %%}"
        "{%% for id in ids[%u:%u] %%}"
        "{%% set ns.items = ns.items + [{'id':id,'name':state_attr(id,'friendly_name') or id,'state':states(id),"
        "'temperature':state_attr(id,'temperature'),'brightness':state_attr(id,'brightness'),"
        "'min':state_attr(id,'min') if state_attr(id,'min') is not none else state_attr(id,'min_temp'),"
        "'max':state_attr(id,'max') if state_attr(id,'max') is not none else state_attr(id,'max_temp'),"
        "'step':state_attr(id,'step') if state_attr(id,'step') is not none else state_attr(id,'target_temp_step')}] %%}"
        "{%% endfor %%}{{ {'total':ids|length,'items':ns.items} | tojson }}";
    char rendered[1536];
    snprintf(rendered, sizeof(rendered), TEMPLATE, offset, offset + POCKET_HOME_BATCH);
    cJSON *request = cJSON_CreateObject();
    if (!request || !cJSON_AddStringToObject(request, "template", rendered)) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
    char *body = cJSON_PrintUnformatted(request); cJSON_Delete(request);
    if (!body) return ESP_ERR_NO_MEM;
    char *raw = NULL;
    esp_err_t error = call(config, "/api/template", body, &raw);
    free(body);
    if (error != ESP_OK) { free(raw); return error; }
    cJSON *root = cJSON_Parse(raw); free(raw);
    cJSON *total = cJSON_GetObjectItemCaseSensitive(root, "total");
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsNumber(total) || total->valuedouble < 0 || total->valuedouble > 100000 || !cJSON_IsArray(items) || cJSON_GetArraySize(items) > POCKET_HOME_BATCH) {
        cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE;
    }
    memset(batch, 0, sizeof(*batch));
    batch->offset = offset; batch->total = (unsigned)total->valueint;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (!entity_id(text(item, "id"))) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
        pocket_home_entity_t *entity = &batch->entity[batch->count++];
        copy_text(entity->id, sizeof(entity->id), text(item, "id"));
        copy_text(entity->name, sizeof(entity->name), text(item, "name"));
        copy_text(entity->state, sizeof(entity->state), text(item, "state"));
        entity->has_temperature = scalar(item, "temperature", &entity->temperature);
        double brightness;
        entity->has_brightness = scalar(item, "brightness", &brightness);
        entity->minimum = 0; entity->maximum = 100; entity->step = 1;
        scalar(item, "min", &entity->minimum); scalar(item, "max", &entity->maximum); scalar(item, "step", &entity->step);
        char *end;
        entity->number = strtod(entity->state, &end);
        entity->has_number = end != entity->state && !*end && isfinite(entity->number);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

unsigned pocket_home_actions(const pocket_home_entity_t *entity, pocket_home_action_t actions[POCKET_HOME_ACTIONS]) {
    if (!entity_id(entity->id) || strcmp(entity->state, "unavailable") == 0 || strcmp(entity->state, "unknown") == 0) return 0;
    char domain[24]; size_t length = (size_t)(strchr(entity->id, '.') - entity->id);
    memcpy(domain, entity->id, length); domain[length] = '\0';
    unsigned count = 0;
#define ACTION(label_, service_) do { actions[count++] = (pocket_home_action_t){.label = label_, .service = service_}; } while (0)
#define NUMBER(label_, service_, parameter_, value_) do { actions[count++] = (pocket_home_action_t){.label = label_, .service = service_, .parameter = parameter_, .number = value_, .has_number = true}; } while (0)
    if (!strcmp(domain, "light") || !strcmp(domain, "switch") || !strcmp(domain, "fan") || !strcmp(domain, "humidifier")) {
        ACTION("打开", "turn_on"); ACTION("关闭", "turn_off");
        if (!strcmp(domain, "light") && entity->has_brightness) {
            NUMBER("亮度增加", "turn_on", "brightness_step_pct", 20);
            NUMBER("亮度降低", "turn_on", "brightness_step_pct", -20);
        } else if (!strcmp(domain, "fan")) {
            ACTION("风速增加", "increase_speed"); ACTION("风速降低", "decrease_speed");
        }
    } else if (!strcmp(domain, "climate")) {
        ACTION("打开", "turn_on"); ACTION("关闭", "turn_off");
        if (entity->has_temperature && entity->maximum >= entity->minimum) {
            double step = entity->step > 0 ? entity->step : 1;
            NUMBER("温度增加", "set_temperature", "temperature", fmin(entity->maximum, entity->temperature + step));
            NUMBER("温度降低", "set_temperature", "temperature", fmax(entity->minimum, entity->temperature - step));
        }
    } else if (!strcmp(domain, "cover")) {
        ACTION("打开", "open_cover"); ACTION("关闭", "close_cover"); ACTION("停止", "stop_cover");
    } else if (!strcmp(domain, "vacuum")) {
        ACTION("开始清扫", "start"); ACTION("暂停", "pause"); ACTION("返回充电", "return_to_base");
    } else if (!strcmp(domain, "button")) {
        ACTION("执行", "press");
    } else if (!strcmp(domain, "scene") || !strcmp(domain, "script")) {
        ACTION("执行", "turn_on");
    } else if (!strcmp(domain, "select")) {
        ACTION("下个选项", "select_next"); ACTION("上个选项", "select_previous");
    } else if (!strcmp(domain, "number") && entity->has_number && entity->step > 0 && entity->maximum >= entity->minimum) {
        NUMBER("数值增加", "set_value", "value", fmin(entity->maximum, entity->number + entity->step));
        NUMBER("数值降低", "set_value", "value", fmax(entity->minimum, entity->number - entity->step));
    } else if (!strcmp(domain, "media_player")) {
        ACTION("播放或暂停", "media_play_pause"); ACTION("音量增加", "volume_up"); ACTION("音量降低", "volume_down");
    }
#undef ACTION
#undef NUMBER
    return count;
}

esp_err_t pocket_home_execute(const pocket_config_t *config, const pocket_home_entity_t *entity, unsigned action_index) {
    pocket_home_action_t actions[POCKET_HOME_ACTIONS];
    unsigned count = pocket_home_actions(entity, actions);
    if (action_index >= count) return ESP_ERR_INVALID_ARG;
    const pocket_home_action_t *action = &actions[action_index];
    char path[128];
    snprintf(path, sizeof(path), "/api/services/%.*s/%s", (int)(strchr(entity->id, '.') - entity->id), entity->id, action->service);
    cJSON *object = cJSON_CreateObject();
    if (!object || !cJSON_AddStringToObject(object, "entity_id", entity->id)) { cJSON_Delete(object); return ESP_ERR_NO_MEM; }
    if (action->has_number && !cJSON_AddNumberToObject(object, action->parameter, action->number)) { cJSON_Delete(object); return ESP_ERR_NO_MEM; }
    char *body = cJSON_PrintUnformatted(object); cJSON_Delete(object);
    if (!body) return ESP_ERR_NO_MEM;
    char *response = NULL;
    esp_err_t error = call(config, path, body, &response);
    free(body); free(response);
    return error;
}
