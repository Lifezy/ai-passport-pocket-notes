#include "pocket_services.h"
#include "pocket_http.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static bool number(cJSON *object, const char *key, double *output) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < 0) return false;
    *output = value->valuedouble; return true;
}

esp_err_t pocket_pc_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot, pocket_pc_data_t *metrics) {
    if (!config->companion_url[0] || !config->companion_token[0]) return ESP_ERR_INVALID_STATE;
    char url[256], authorization[144];
    size_t length = strlen(config->companion_url);
    if (length && config->companion_url[length - 1] == '/') --length;
    snprintf(url, sizeof(url), "%.*s/v1/pc", (int)length, config->companion_url);
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->companion_token);
    const pocket_header_t headers[] = {{"Authorization", authorization}};
    char *body = NULL; int status;
    esp_err_t error = pocket_http(url, HTTP_METHOD_GET, headers, 1, NULL, &body, &status);
    memset(authorization, 0, sizeof(authorization));
    if (status == 401 || status == 403) return ESP_ERR_INVALID_STATE;
    if (status == 404) return ESP_ERR_NOT_SUPPORTED;
    if (error != ESP_OK) return error;
    cJSON *root = cJSON_Parse(body); free(body);
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsObject(data) || !cJSON_IsString(state)) {
        cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE;
    }
    _Static_assert(POCKET_ROW_COUNT == POCKET_PC_ROWS, "PC row count");
    memset(metrics,0,sizeof(*metrics));
    for(unsigned i=1;i<POCKET_PC_METRIC_COUNT;++i) {
        double value;
        if(number(data,pocket_pc_metric_key((pocket_pc_metric_t)i),&value)) { metrics->value[i]=value;metrics->valid|=1u<<i; }
    }
    double value, total, free_space;
    if(number(data,"system_disk_total_bytes",&total) && total>0 && total<9007199254740992.0 && number(data,"system_disk_free_bytes",&free_space) && free_space<=total) {
        metrics->value[POCKET_PC_DISK]=(total-free_space)*100.0/total; metrics->valid|=1u<<POCKET_PC_DISK;
    }
    if (!number(root, "updated_at", &value) || value <= 0 || value >= 4102444800.0) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    snapshot->updated_at = (int64_t)value;
    snapshot->status = strcmp(state->valuestring, "ok") ? POCKET_STALE : POCKET_READY;
    metrics->value[POCKET_PC_CONNECTION]=snapshot->status==POCKET_READY;
    metrics->valid|=1u<<POCKET_PC_CONNECTION;
    pocket_pc_display_t display; pocket_config_pc_display_get(&display);
    for(unsigned i=0;i<POCKET_ROW_COUNT;++i) {
        if(i<display.count) pocket_pc_data_format(metrics,display.metric[i],snapshot->value[i],POCKET_VALUE_BYTES);
        else snapshot->value[i][0]=0;
    }
    cJSON_Delete(root); return ESP_OK;
}
