#include "pocket_services.h"
#include "pocket_http.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"

static void allowance(cJSON *window, char *amount, char *reset) {
    cJSON *remaining = cJSON_GetObjectItemCaseSensitive(window, "remaining_percent");
    cJSON *resets = cJSON_GetObjectItemCaseSensitive(window, "resets_at");
    snprintf(amount, POCKET_VALUE_BYTES, "--");
    snprintf(reset, POCKET_VALUE_BYTES, "--");
    if (cJSON_IsNumber(remaining) && isfinite(remaining->valuedouble) && remaining->valuedouble >= 0 && remaining->valuedouble <= 100)
        snprintf(amount, POCKET_VALUE_BYTES, "%.0f%%", remaining->valuedouble);
    if (cJSON_IsNumber(resets) && resets->valuedouble > 0 && resets->valuedouble <= 2147483647) {
        time_t seconds = (time_t)resets->valuedouble;
        struct tm local;
        localtime_r(&seconds, &local);
        strftime(reset, POCKET_VALUE_BYTES, "%m/%d %H:%M", &local);
    }
}

esp_err_t pocket_codex_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot) {
    if (!config->companion_url[0] || !config->companion_token[0]) return ESP_ERR_INVALID_STATE;
    char url[256], authorization[144];
    /* Strip the optional trailing slash before adding the fixed API path. */
    size_t base_length = strlen(config->companion_url);
    if (base_length && config->companion_url[base_length - 1] == '/') --base_length;
    snprintf(url, sizeof(url), "%.*s/v1/codex", (int)base_length, config->companion_url);
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->companion_token);
    const pocket_header_t headers[] = {{"Authorization", authorization}};
    char *body = NULL;
    int http_status;
    esp_err_t error = pocket_http(url, HTTP_METHOD_GET, headers, 1, NULL, &body, &http_status);
    memset(authorization, 0, sizeof(authorization));
    if (http_status == 401 || http_status == 403) return ESP_ERR_INVALID_STATE;
    if (error != ESP_OK) return error;
    cJSON *root = cJSON_Parse(body); free(body);
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsObject(data) || !cJSON_IsString(status)) {
        cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE;
    }
    allowance(cJSON_GetObjectItemCaseSensitive(data, "primary"), snapshot->value[0], snapshot->value[1]);
    allowance(cJSON_GetObjectItemCaseSensitive(data, "secondary"), snapshot->value[2], snapshot->value[3]);
    cJSON *credits = cJSON_GetObjectItemCaseSensitive(data, "credits");
    cJSON *balance = cJSON_GetObjectItemCaseSensitive(credits, "balance");
    cJSON *unlimited = cJSON_GetObjectItemCaseSensitive(credits, "unlimited");
    snprintf(snapshot->value[4], POCKET_VALUE_BYTES, "%s", cJSON_IsTrue(unlimited) ? "无限额" : cJSON_IsString(balance) ? balance->valuestring : "--");
    cJSON *reset_data = cJSON_GetObjectItemCaseSensitive(data, "resets");
    cJSON *resets = cJSON_GetObjectItemCaseSensitive(reset_data, "available_count");
    bool resets_valid = cJSON_IsNumber(resets) && isfinite(resets->valuedouble) && resets->valuedouble >= 0 &&
        resets->valuedouble <= INT32_MAX && resets->valuedouble == (double)resets->valueint;
    if (resets_valid)
        snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "%d", resets->valueint);
    else snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "--");
    if (resets_valid && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reset_data, "details_complete"))) {
        double earliest = 0;
        cJSON *expiry;
        cJSON_ArrayForEach(expiry, cJSON_GetObjectItemCaseSensitive(reset_data, "expires_at")) {
            if (cJSON_IsNumber(expiry) && isfinite(expiry->valuedouble) && expiry->valuedouble > 0 &&
                expiry->valuedouble < 4102444800.0 && (!earliest || expiry->valuedouble < earliest)) earliest = expiry->valuedouble;
        }
        if (earliest) {
            time_t seconds = (time_t)earliest;
            struct tm local; localtime_r(&seconds, &local);
            char date[16]; strftime(date, sizeof(date), "%m/%d", &local);
            snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "%d / %s", resets->valueint, date);
        }
    }
    cJSON *updated = cJSON_GetObjectItemCaseSensitive(root, "updated_at");
    snapshot->updated_at = cJSON_IsNumber(updated) && isfinite(updated->valuedouble) && updated->valuedouble > 0 && updated->valuedouble < 4102444800.0 ? (int64_t)updated->valuedouble : 0;
    snapshot->status = strcmp(status->valuestring, "ok") == 0 ? POCKET_READY : POCKET_STALE;
    cJSON_Delete(root);
    return ESP_OK;
}
