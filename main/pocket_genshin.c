#include "pocket_genshin.h"
#include "pocket_http.h"
#include "pocket_verification.h"
#include "pocket_genshin_diagnostic.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md5.h"

#define RECORD "https://api-takumi-record.mihoyo.com"
#define PASSPORT "https://passport-api.mihoyo.com"
#define WIDGET RECORD "/game_record/app/genshin/aapi/widget/v2"
#define MISC "https://bbs-api.miyoushe.com/misc/api/"

static char *s_verification;
static pocket_verification_t *s_verification_session;
static pocket_genshin_stage_t s_stage;
static int s_http_status, s_api_status;

void pocket_genshin_last_error(char *output, size_t capacity, esp_err_t error) {
    pocket_genshin_diagnostic_text(output, capacity, s_stage, error, s_http_status, s_api_status);
    ESP_LOGW("pocket_genshin", "stage=%u error=%s http=%d api=%d free=%u largest=%u", (unsigned)s_stage,
        esp_err_to_name(error), s_http_status, s_api_status,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static const char *string(cJSON *object, const char *key) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}

static bool identifier(cJSON *object, const char *key, char *output, size_t capacity) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring[0] && strlen(value->valuestring) < capacity) {
        for (const char *p = value->valuestring; *p; ++p) if (*p < '0' || *p > '9') return false;
        snprintf(output, capacity, "%s", value->valuestring);
        return true;
    }
    if (cJSON_IsNumber(value) && isfinite(value->valuedouble) && value->valuedouble > 0 && value->valuedouble <= 9007199254740991.0 && floor(value->valuedouble) == value->valuedouble) {
        int length = snprintf(output, capacity, "%.0f", value->valuedouble);
        return length > 0 && (size_t)length < capacity;
    }
    return false;
}

static bool integer(cJSON *object, const char *key, int64_t *output) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(value) && value->valuedouble >= 0 && value->valuedouble <= 2147483647.0) {
        *output = (int64_t)value->valuedouble;
        return value->valuedouble == (double)*output;
    }
    if (cJSON_IsString(value) && value->valuestring[0]) {
        char *end;
        int64_t result = strtoll(value->valuestring, &end, 10);
        if (!*end && result >= 0 && result <= INT32_MAX) { *output = result; return true; }
    }
    return false;
}

static void random_hex(char *output, unsigned bytes) {
    static const char HEX[] = "0123456789abcdef";
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned value = esp_random();
        output[i * 2] = HEX[(value >> 4) & 15];
        output[i * 2 + 1] = HEX[value & 15];
    }
    output[bytes * 2] = '\0';
}

static esp_err_t request(const pocket_config_t *config, const char *url, const char *body,
                          bool passport, const char *cookie, cJSON **response, int *api_code) {
    *response = NULL;
    *api_code = 0;
    s_http_status = 0; s_api_status = 0;
    s_stage = strstr(url, "/getFp") ? GENSHIN_FP : strstr(url, "/createQRLogin") ? GENSHIN_QR :
        strstr(url, "/queryQRLoginStatus") ? GENSHIN_POLL : strstr(url, "getCookieAccountInfoBySToken") ? GENSHIN_COOKIE :
        strstr(url, "getUserGameRolesByCookieToken") ? GENSHIN_ROLE : strstr(url, "Verification") ? GENSHIN_VERIFY : GENSHIN_NOTE;
    char ds[128], hash_hex[33], nonce[16];
    time_t timestamp = time(NULL);
    bool exchange = strstr(url, "getCookieAccountInfoBySToken") != NULL;
    bool misc = strncmp(url, MISC, strlen(MISC)) == 0;
    bool ds1 = misc && !body;
    const char *query = strchr(url, '?');
    if (ds1) {
        const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        for (unsigned i = 0; i < 6; ++i) nonce[i] = alphabet[esp_random() % 62];
        nonce[6] = '\0';
    } else snprintf(nonce, sizeof(nonce), "%u", 100001 + (unsigned)(esp_random() % 100000));
    size_t signature_size = 128 + (body ? strlen(body) : 0) + (query ? strlen(query) : 0);
    char *signature = malloc(signature_size);
    if (!signature) return ESP_ERR_NO_MEM;
    if (ds1) snprintf(signature, signature_size, "salt=xV8v4Qu54lUKrEYFZkJhB8cuOh9Asafs&t=%lld&r=%s", (long long)timestamp, nonce);
    else snprintf(signature, signature_size, "salt=xV8v4Qu54lUKrEYFZkJhB8cuOh9Asafs&t=%lld&r=%s&b=%s&q=%s",
                  (long long)timestamp, nonce, body ? body : "", query ? query + 1 : "");
    unsigned char hash[16];
    int hashed = mbedtls_md5((const unsigned char *)signature, strlen(signature), hash);
    memset(signature, 0, signature_size); free(signature);
    if (hashed != 0) return ESP_FAIL;
    for (unsigned i = 0; i < 16; ++i) snprintf(hash_hex + i * 2, 3, "%02x", hash[i]);
    snprintf(ds, sizeof(ds), "%lld,%s,%s", (long long)timestamp, nonce, hash_hex);
    const pocket_header_t headers[] = {
        {"DS", ds}, {"Cookie", cookie},
        {"x-rpc-app_version", passport || misc ? "2.115.0" : "2.40.1"},
        {"x-rpc-client_type", exchange ? "2" : passport ? "3" : "5"},
        {"x-rpc-device_id", config->genshin_device_id}, {"x-rpc-device_fp", config->genshin_device_fp},
        {"X-Requested-With", "com.mihoyo.hyperion"},
        {"Referer", "https://webstatic.mihoyo.com"}, {"Origin", "https://webstatic.mihoyo.com"},
        {"x-rpc-page", "3.1.3_#/rpg"}, {"x-rpc-language", "zh-cn"},
        {"x-rpc-sdk_version", "2.115.0"}, {"x-rpc-account_version", "2.115.0"},
        {"x-rpc-app_id", "bll8iq97cem8"}, {"x-rpc-game_biz", "bbs_cn"},
        {"x-rpc-challenge_path", WIDGET}, {"x-rpc-challenge_game", "2"},
        {"x-rpc-challenge", s_verification ? s_verification : ""},
        {"User-Agent", exchange || misc ? "Mozilla/5.0 (Windows NT 10.0; Win64; x64) miHoYoBBS/2.115.0" :
                       passport ? "Mozilla/5.0 miHoYoBBS/2.115.0 Capture/2.2.0" :
                       "Mozilla/5.0 (iPhone; CPU iPhone OS 17_6 like Mac OS X) AppleWebKit/605.1.15 miHoYoBBS/2.40.1"},
    };
    char *raw = NULL;
    int http_status;
    esp_err_t error = pocket_http(url, body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
                                  headers, sizeof(headers) / sizeof(headers[0]), body, &raw, &http_status);
    s_http_status = http_status;
    if (error != ESP_OK) { *api_code = -http_status; return error; }
    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "retcode");
    if (!cJSON_IsNumber(code) || code->valueint != 0) {
        *api_code = cJSON_IsNumber(code) ? code->valueint : -900;
        s_api_status = *api_code;
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    *response = root;
    return ESP_OK;
}

esp_err_t pocket_genshin_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot, int *api_code) {
    if (!config->genshin_cookie[0] || !config->genshin_uid[0]) return ESP_ERR_INVALID_STATE;
    char url[256];
    bool widget = config->genshin_widget_cookie[0];
    if (widget) snprintf(url, sizeof(url), "%s", WIDGET);
    else snprintf(url, sizeof(url), RECORD "/game_record/app/genshin/api/dailyNote?role_id=%s&server=%s",
                  config->genshin_uid, config->genshin_region);
    cJSON *root;
    esp_err_t error = request(config, url, NULL, false,
                              widget ? config->genshin_widget_cookie : config->genshin_cookie, &root, api_code);
    if (error != ESP_OK) return error;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *expeditions = cJSON_GetObjectItemCaseSensitive(data, "expeditions");
    cJSON *reward = cJSON_GetObjectItemCaseSensitive(data, "is_extra_task_reward_received");
    int64_t resin, maximum, recovery, done, total, coin, coin_max;
    if (!integer(data, "current_resin", &resin) || !integer(data, "max_resin", &maximum) ||
        !integer(data, "resin_recovery_time", &recovery) || !integer(data, "finished_task_num", &done) ||
        !integer(data, "total_task_num", &total) || resin > maximum || maximum == 0 ||
        done > total || total > 100 || !cJSON_IsArray(expeditions) || !cJSON_IsBool(reward)) {
        cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE;
    }
    snprintf(snapshot->value[0], POCKET_VALUE_BYTES, "%lld/%lld", (long long)resin, (long long)maximum);
    snprintf(snapshot->value[1], POCKET_VALUE_BYTES, "%lld时%lld分", (long long)(recovery / 3600), (long long)(recovery % 3600 / 60));
    snprintf(snapshot->value[2], POCKET_VALUE_BYTES, "%lld/%lld", (long long)done, (long long)total);
    snprintf(snapshot->value[3], POCKET_VALUE_BYTES, "%s", cJSON_IsTrue(reward) ? "已领取" : "未领取");
    unsigned finished = 0;
    cJSON *expedition;
    cJSON_ArrayForEach(expedition, expeditions) if (strcmp(string(expedition, "status"), "Finished") == 0) ++finished;
    snprintf(snapshot->value[4], POCKET_VALUE_BYTES, "%u/%d", finished, cJSON_GetArraySize(expeditions));
    if (integer(data, "current_home_coin", &coin) && integer(data, "max_home_coin", &coin_max))
        snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "%lld/%lld", (long long)coin, (long long)coin_max);
    else snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "--");
    cJSON_Delete(root);
    return ESP_OK;
}

static bool qr_allowed(const char *url) {
    if (strncmp(url, "https://", 8) != 0 || strlen(url) > 1000) return false;
    const char *start = url + 8, *end = strchr(start, '/');
    if (!end) return false;
    size_t length = (size_t)(end - start);
    const char *suffixes[] = {"mihoyo.com", "miyoushe.com"};
    for (unsigned i = 0; i < 2; ++i) {
        size_t suffix = strlen(suffixes[i]);
        if (length >= suffix && memcmp(end - suffix, suffixes[i], suffix) == 0 &&
            (length == suffix || end[-(int)suffix - 1] == '.')) {
            for (const char *p = start; p < end; ++p) if (*p == '@' || *p == ':' || *p == '\\') return false;
            return true;
        }
    }
    return false;
}

esp_err_t pocket_genshin_login(pocket_login_progress_t progress, void *context, int *api_code) {
    s_stage = GENSHIN_FP; s_http_status = 0; s_api_status = 0;
    pocket_config_t *config = calloc(1, sizeof(*config));
    if (!config) return ESP_ERR_NO_MEM;
    pocket_config_get(config);
    free(s_verification); s_verification = NULL;
    random_hex(config->genshin_device_id, 16);
    config->genshin_device_fp[0] = '\0';
    char seed[33], fp_seed[15], payload[512];
    random_hex(seed, 16); random_hex(fp_seed, 7);
    fp_seed[13] = '\0';
    snprintf(payload, sizeof(payload), "{\"device_id\":\"%s\",\"seed_id\":\"%s\",\"seed_time\":\"%lld000\",\"platform\":\"4\",\"device_fp\":\"%s\",\"app_name\":\"bbs_cn\",\"ext_fields\":\"{\\\"userAgent\\\":\\\"Mozilla/5.0 miHoYoBBS/2.115.0\\\"}\"}",
             config->genshin_device_id, seed, (long long)time(NULL), fp_seed);
    cJSON *root = NULL;
    esp_err_t error = request(config, "https://public-data-api.mihoyo.com/device-fp/api/getFp", payload, true, "", &root, api_code);
    if (error != ESP_OK) goto finish;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const char *fingerprint = string(data, "device_fp");
    if (!fingerprint[0] || strlen(fingerprint) >= sizeof(config->genshin_device_fp)) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
    snprintf(config->genshin_device_fp, sizeof(config->genshin_device_fp), "%s", fingerprint);
    cJSON_Delete(root); root = NULL;
    error = request(config, PASSPORT "/account/ma-cn-passport/app/createQRLogin", "{}", true, "", &root, api_code);
    if (error != ESP_OK) goto finish;
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    char ticket[256], qr[1024];
    if (strlen(string(data, "ticket")) >= sizeof(ticket) || !qr_allowed(string(data, "url"))) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
    snprintf(ticket, sizeof(ticket), "%s", string(data, "ticket"));
    snprintf(qr, sizeof(qr), "%s", string(data, "url"));
    cJSON_Delete(root); root = NULL;
    int64_t deadline = esp_timer_get_time() + 170000000;
    while (esp_timer_get_time() < deadline) {
        if (!progress(qr, "请用米游社扫码确认", context)) { error = ESP_ERR_INVALID_STATE; goto finish; }
        vTaskDelay(pdMS_TO_TICKS(2000));
        cJSON *query = cJSON_CreateObject();
        if (!query || !cJSON_AddStringToObject(query, "ticket", ticket)) { cJSON_Delete(query); error = ESP_ERR_NO_MEM; goto finish; }
        char *body = cJSON_PrintUnformatted(query); cJSON_Delete(query);
        if (!body) { error = ESP_ERR_NO_MEM; goto finish; }
        error = request(config, PASSPORT "/account/ma-cn-passport/app/queryQRLoginStatus", body, true, "", &root, api_code);
        free(body);
        if (error != ESP_OK) goto finish;
        data = cJSON_GetObjectItemCaseSensitive(root, "data");
        const char *status = string(data, "status");
        if (strcmp(status, "Expired") == 0) { error = ESP_ERR_TIMEOUT; goto finish; }
        if (strcmp(status, "Confirmed") != 0) { cJSON_Delete(root); root = NULL; continue; }
        cJSON *user = cJSON_GetObjectItemCaseSensitive(data, "user_info");
        cJSON *tokens = cJSON_GetObjectItemCaseSensitive(data, "tokens");
        char mid[128], account[32], token[256] = "";
        if (strlen(string(user, "mid")) >= sizeof(mid) || !identifier(user, "aid", account, sizeof(account))) { error = ESP_ERR_INVALID_SIZE; goto finish; }
        snprintf(mid, sizeof(mid), "%s", string(user, "mid"));
        cJSON *entry;
        cJSON_ArrayForEach(entry, tokens) {
            const char *value = string(entry, "token");
            if (strncmp(value, "v2_", 3) == 0 && strlen(value) < sizeof(token)) { snprintf(token, sizeof(token), "%s", value); break; }
        }
        if (!token[0] || !mid[0] || !account[0]) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
        snprintf(config->genshin_widget_cookie, sizeof(config->genshin_widget_cookie), "mid=%s;stuid=%s;stoken=%s;", mid, account, token);
        cJSON_Delete(root); root = NULL;
        /* The reference endpoint accepts the SToken query; never log this URL. */
        char encoded_token[768], cookie_url[1024];
        size_t encoded_length = 0;
        for (const unsigned char *p = (const unsigned char *)token; *p; ++p) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.') encoded_token[encoded_length++] = (char)*p;
            else { snprintf(encoded_token + encoded_length, 4, "%%%02X", *p); encoded_length += 3; }
        }
        encoded_token[encoded_length] = '\0';
        snprintf(cookie_url, sizeof(cookie_url), PASSPORT "/account/auth/api/getCookieAccountInfoBySToken?stoken=%s", encoded_token);
        memset(encoded_token, 0, sizeof(encoded_token));
        error = request(config, cookie_url, NULL, true, config->genshin_widget_cookie, &root, api_code);
        memset(token, 0, sizeof(token));
        memset(cookie_url, 0, sizeof(cookie_url));
        if (error != ESP_OK) goto finish;
        data = cJSON_GetObjectItemCaseSensitive(root, "data");
        const char *cookie_token = string(data, "cookie_token");
        char cookie_uid[32];
        if (!identifier(data, "uid", cookie_uid, sizeof(cookie_uid)) || strcmp(cookie_uid, account) || !cookie_token[0] || strlen(cookie_token) > 300) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
        snprintf(config->genshin_cookie, sizeof(config->genshin_cookie), "account_id=%s;cookie_token=%s;", account, cookie_token);
        cJSON_Delete(root); root = NULL;
        error = request(config, PASSPORT "/binding/api/getUserGameRolesByCookieToken?game_biz=hk4e_cn", NULL, false, config->genshin_cookie, &root, api_code);
        if (error != ESP_OK) goto finish;
        cJSON *roles = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "data"), "list");
        cJSON *picked = NULL;
        cJSON_ArrayForEach(entry, roles) {
            if (strcmp(string(entry, "game_biz"), "hk4e_cn") != 0) continue;
            if (!picked || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "is_chosen"))) picked = entry;
        }
        if (!picked || !identifier(picked, "game_uid", config->genshin_uid, sizeof(config->genshin_uid)) || strlen(string(picked, "region")) >= sizeof(config->genshin_region)) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
        snprintf(config->genshin_region, sizeof(config->genshin_region), "%s", string(picked, "region"));
        if (!progress("", "正在保存登录", context)) { error = ESP_ERR_INVALID_STATE; goto finish; }
        cJSON *update = cJSON_CreateObject();
        if (!update) { error = ESP_ERR_NO_MEM; goto finish; }
        cJSON_AddStringToObject(update, "genshin_uid", config->genshin_uid);
        cJSON_AddStringToObject(update, "genshin_region", config->genshin_region);
        cJSON_AddStringToObject(update, "genshin_cookie", config->genshin_cookie);
        cJSON_AddStringToObject(update, "genshin_widget_cookie", config->genshin_widget_cookie);
        cJSON_AddStringToObject(update, "genshin_device_id", config->genshin_device_id);
        cJSON_AddStringToObject(update, "genshin_device_fp", config->genshin_device_fp);
        char *saved = cJSON_PrintUnformatted(update); cJSON_Delete(update);
        error = saved ? pocket_config_update(saved) : ESP_ERR_NO_MEM;
        if (saved) { memset(saved, 0, strlen(saved)); free(saved); }
        goto finish;
    }
    error = ESP_ERR_TIMEOUT;
finish:
    cJSON_Delete(root);
    memset(config, 0, sizeof(*config));
    free(config);
    return error;
}

esp_err_t pocket_genshin_verify(pocket_login_progress_t progress, void *context, int *api_code) {
    if (s_verification_session) {
        esp_err_t stopped = pocket_verification_close(s_verification_session);
        if (stopped != ESP_OK) return stopped;
        s_verification_session = NULL;
    }
    pocket_config_t *config = malloc(sizeof(*config));
    char *proof = calloc(1, 4097);
    if (!config || !proof) { free(config); free(proof); return ESP_ERR_NO_MEM; }
    pocket_config_get(config);
    cJSON *root = NULL;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!config->genshin_widget_cookie[0]) goto finish_verification;
    error = request(config, MISC "createVerification?is_high=true", NULL, false, config->genshin_widget_cookie, &root, api_code);
    if (error != ESP_OK) goto finish_verification;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const char *gt = string(data, "gt");
    if (!gt[0] || strlen(gt) > 1000) { error = ESP_ERR_INVALID_RESPONSE; goto finish_verification; }
    cJSON *public_config = cJSON_CreateObject();
    if (!public_config || !cJSON_AddStringToObject(public_config, "gt", gt)) { cJSON_Delete(public_config); error = ESP_ERR_NO_MEM; goto finish_verification; }
    const char *challenge = string(data, "challenge"), *risk_type = string(data, "risk_type");
    if (strlen(challenge) > 1000 || strlen(risk_type) > 1000) { cJSON_Delete(public_config); error = ESP_ERR_INVALID_SIZE; goto finish_verification; }
    if (challenge[0]) cJSON_AddStringToObject(public_config, "challenge", challenge);
    if (risk_type[0]) cJSON_AddStringToObject(public_config, "risk_type", risk_type);
    char *public_json = cJSON_PrintUnformatted(public_config); cJSON_Delete(public_config);
    cJSON_Delete(root); root = NULL;
    if (!public_json) { error = ESP_ERR_NO_MEM; goto finish_verification; }
    char url[160];
    error = pocket_verification_open(public_json, &s_verification_session, url, sizeof(url));
    free(public_json);
    if (error != ESP_OK) goto finish_verification;
    int64_t deadline = esp_timer_get_time() + 300000000;
    bool received = false;
    while (esp_timer_get_time() < deadline) {
        if (!progress(url, "手机扫码完成安全验证", context)) { error = ESP_ERR_INVALID_STATE; goto finish_verification; }
        if (pocket_verification_take(s_verification_session, proof, 4097)) { received = true; break; }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!received) { error = ESP_ERR_TIMEOUT; goto finish_verification; }
    error = pocket_verification_close(s_verification_session);
    if (error != ESP_OK) goto finish_verification;
    s_verification_session = NULL;
    if (!progress("", "正在确认安全验证", context)) { error = ESP_ERR_INVALID_STATE; goto finish_verification; }
    error = request(config, MISC "verifyVerification", proof, false, config->genshin_widget_cookie, &root, api_code);
    if (error != ESP_OK) goto finish_verification;
    challenge = string(cJSON_GetObjectItemCaseSensitive(root, "data"), "challenge");
    if (!challenge[0] || strlen(challenge) > 4096 || strpbrk(challenge, "\r\n")) { error = ESP_ERR_INVALID_RESPONSE; goto finish_verification; }
    char *next = strdup(challenge);
    if (!next) { error = ESP_ERR_NO_MEM; goto finish_verification; }
    free(s_verification); s_verification = next;
finish_verification:
    if (s_verification_session && pocket_verification_close(s_verification_session) == ESP_OK) s_verification_session = NULL;
    cJSON_Delete(root);
    memset(config, 0, sizeof(*config)); free(config);
    memset(proof, 0, 4097); free(proof);
    return error;
}
