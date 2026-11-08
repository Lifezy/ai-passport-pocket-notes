#include "pocket_verification.h"
#include "pocket_json.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct pocket_verification {
    httpd_handle_t server;
    SemaphoreHandle_t lock;
    char key[33], host[64], origin[80];
    char *configuration;
    char proof[4097];
    int64_t expires;
    bool consumed;
};

static const char PAGE[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta http-equiv=Content-Security-Policy content=\"default-src 'none';script-src 'unsafe-inline' https://*.geetest.com https://*.geevisit.com https://*.gsensebot.com;style-src 'unsafe-inline' https://*.geetest.com;img-src data: blob: https://*.geetest.com https://*.geevisit.com https://*.gsensebot.com;connect-src 'self' https://*.geetest.com https://*.geevisit.com https://*.gsensebot.com;frame-src https://*.geetest.com https://*.geevisit.com https://*.gsensebot.com;font-src data: https://*.geetest.com;worker-src blob:\">"
    "<style>body{background:#101b2b;color:#f5f4ef;font:17px sans-serif;padding:24px;max-width:500px;margin:auto}button{padding:14px;margin:16px 0}</style>"
    "<h2>原神便笺 · 安全验证</h2><p id=s>正在加载。此页面不会接收米游社登录凭据。</p><div id=captcha></div><button id=open hidden>开始验证</button>"
    "<script>const s=document.getElementById('s');fetch('/config'+location.search).then(r=>{if(!r.ok)throw Error();return r.json()}).then(d=>{"
    "const v3=!!d.challenge;const options=v3?{gt:d.gt,challenge:d.challenge,https:true,product:'popup',offline:false,new_captcha:true}:{captchaId:d.gt,https:true,product:'popup',lang:'zho'};"
    "if(!v3&&d.risk_type)options.riskType=d.risk_type;const sdk=document.createElement('script');sdk.src=v3?'https://static.geetest.com/static/js/gt.0.4.9.js':'https://static.geetest.com/v4/gt4.js';"
    "sdk.onload=()=>{(v3?initGeetest:initGeetest4)(options,c=>{c.appendTo('#captcha');c.onReady(()=>{s.textContent='请本人手动完成验证。';const b=document.getElementById('open');if(c.showCaptcha){b.hidden=false;b.onclick=()=>c.showCaptcha()}});"
    "c.onSuccess(()=>{const proof=c.getValidate();if(!proof)return;fetch('/proof'+location.search,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(proof)}).then(r=>{s.textContent=r.ok?'结果已提交，请查看 AI Passport 是否恢复同步。':'会话已结束，请重新生成。'}).catch(()=>s.textContent='连接失败，请保持手机与设备在同一 WLAN。')})})};"
    "sdk.onerror=()=>s.textContent='验证组件加载失败，请稍后重试。';document.head.append(sdk)}).catch(()=>s.textContent='会话已结束，请重新生成。')</script>";

static bool allowed(httpd_req_t *request, pocket_verification_t *session) {
    char query[128], key[64], host[80];
    if (esp_timer_get_time() >= session->expires || httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "k", key, sizeof(key)) != ESP_OK || strlen(key) != 32 ||
        httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK || strcmp(host, session->host)) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; ++i) difference |= (unsigned char)key[i] ^ (unsigned char)session->key[i];
    return difference == 0;
}

static esp_err_t serve(httpd_req_t *request) {
    pocket_verification_t *session = request->user_ctx;
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    if (!allowed(request, session)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session unavailable");
    if (request->method == HTTP_GET) {
        if (!strncmp(request->uri, "/config", 7)) {
            httpd_resp_set_type(request, "application/json");
            return httpd_resp_sendstr(request, session->configuration);
        }
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        return httpd_resp_sendstr(request, PAGE);
    }
    char origin[96], content_type[64];
    if (httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK || strcmp(origin, session->origin) ||
        httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strncmp(content_type, "application/json", 16) || request->content_len < 2 || request->content_len > 4096)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid proof");
    char *body = malloc(request->content_len + 1);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) { free(body); return ESP_FAIL; }
        received += (size_t)count;
    }
    body[received] = '\0';
    if (!pocket_json_flat_object(body, received)) { free(body); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid proof"); }
    cJSON *proof = cJSON_Parse(body);
    bool v3 = cJSON_HasObjectItem(proof, "geetest_validate");
    const char *v3_names[] = {"geetest_challenge", "geetest_validate", "geetest_seccode"};
    const char *v4_names[] = {"captcha_id", "lot_number", "captcha_output", "pass_token", "gen_time"};
    const char **names = v3 ? v3_names : v4_names;
    unsigned expected = v3 ? 3 : 5;
    bool valid = cJSON_IsObject(proof) && cJSON_GetArraySize(proof) == (int)expected;
    for (unsigned i = 0; i < expected; ++i) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(proof, names[i]);
        valid &= cJSON_IsString(item) && item->valuestring[0] && strlen(item->valuestring) <= 3000;
    }
    cJSON_Delete(proof);
    if (!valid) { free(body); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid proof"); }
    xSemaphoreTake(session->lock, portMAX_DELAY);
    bool consumed = session->consumed;
    if (!consumed) {
        memcpy(session->proof, body, received + 1);
        session->consumed = true;
    }
    xSemaphoreGive(session->lock);
    memset(body, 0, received); free(body);
    if (consumed) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Proof already submitted");
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "Submitted; check the device for the actual sync result.");
}

esp_err_t pocket_verification_open(const char *public_json, pocket_verification_t **output, char *url, unsigned url_size) {
    *output = NULL;
    esp_netif_t *interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t address;
    if (!interface || esp_netif_get_ip_info(interface, &address) != ESP_OK || !address.ip.addr) return ESP_ERR_INVALID_STATE;
    pocket_verification_t *session = calloc(1, sizeof(*session));
    if (!session) return ESP_ERR_NO_MEM;
    session->configuration = strdup(public_json);
    session->lock = xSemaphoreCreateMutex();
    if (!session->configuration || !session->lock) {
        free(session->configuration); if (session->lock) vSemaphoreDelete(session->lock); free(session); return ESP_ERR_NO_MEM;
    }
    static const char HEX[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) session->key[i] = HEX[esp_random() & 15];
    session->expires = esp_timer_get_time() + 300000000;
    httpd_config_t settings = HTTPD_DEFAULT_CONFIG();
    settings.server_port = 20000 + esp_random() % 20000;
    settings.ctrl_port = 40000 + esp_random() % 20000;
    settings.max_open_sockets = 2; settings.stack_size = 4096; settings.lru_purge_enable = true;
    settings.recv_wait_timeout = 3; settings.send_wait_timeout = 3;
    esp_err_t error = httpd_start(&session->server, &settings);
    if (error != ESP_OK) { vSemaphoreDelete(session->lock); free(session->configuration); free(session); return error; }
    snprintf(session->host, sizeof(session->host), IPSTR ":%u", IP2STR(&address.ip), settings.server_port);
    snprintf(session->origin, sizeof(session->origin), "http://%s", session->host);
    httpd_uri_t handlers[] = {
        {.uri = "/verify", .method = HTTP_GET, .handler = serve, .user_ctx = session},
        {.uri = "/config", .method = HTTP_GET, .handler = serve, .user_ctx = session},
        {.uri = "/proof", .method = HTTP_POST, .handler = serve, .user_ctx = session},
    };
    for (unsigned i = 0; i < 3; ++i) {
        error = httpd_register_uri_handler(session->server, &handlers[i]);
        if (error != ESP_OK) {
            if (pocket_verification_close(session) != ESP_OK) *output = session;
            return error;
        }
    }
    int length = snprintf(url, url_size, "%s/verify?k=%s", session->origin, session->key);
    if (length < 0 || (unsigned)length >= url_size) {
        if (pocket_verification_close(session) != ESP_OK) *output = session;
        return ESP_ERR_INVALID_SIZE;
    }
    *output = session;
    return ESP_OK;
}

bool pocket_verification_take(pocket_verification_t *session, char *proof, unsigned capacity) {
    xSemaphoreTake(session->lock, portMAX_DELAY);
    bool ready = session->proof[0] && strlen(session->proof) < capacity;
    if (ready) { strcpy(proof, session->proof); memset(session->proof, 0, sizeof(session->proof)); }
    xSemaphoreGive(session->lock);
    return ready;
}

esp_err_t pocket_verification_close(pocket_verification_t *session) {
    if (!session) return ESP_OK;
    esp_err_t error = httpd_stop(session->server);
    if (error != ESP_OK) return error; /* Preserve resources if handlers may still be alive. */
    vSemaphoreDelete(session->lock);
    free(session->configuration);
    memset(session, 0, sizeof(*session)); free(session);
    return ESP_OK;
}
