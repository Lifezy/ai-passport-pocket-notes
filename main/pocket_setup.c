#include "pocket_setup.h"
#include "pocket_setup_guard.h"
#include "pocket_config.h"
#include "pocket_json.h"
#include "pocket_http.h"
#include "pocket_screen.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern const char setup_html[] asm("_binary_pocket_setup_html_start");
static httpd_handle_t s_server;
static SemaphoreHandle_t s_lock;
static atomic_bool s_active;
static atomic_bool s_screen_off_requested;
static atomic_uint s_brightness_requested;
unsigned pocket_setup_take_brightness(void) { return atomic_exchange(&s_brightness_requested, 0); }
bool pocket_setup_take_screen_off(void) { return atomic_exchange(&s_screen_off_requested,false); }
static pocket_setup_view_t s_view;
static char s_key[33];
static int64_t s_expires;
static atomic_uint s_saved;
static atomic_bool s_restart_pending;

static void mark_saved(void) {
    atomic_store(&s_saved, (uint32_t)(esp_timer_get_time() / 1000));
    atomic_store(&s_restart_pending, true);
}

esp_err_t pocket_setup_init(void) {
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

bool pocket_setup_active(void) { return atomic_load(&s_active); }

bool pocket_setup_view(pocket_setup_view_t *view) {
    if (!s_lock || !pocket_setup_active()) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *view = s_view;
    xSemaphoreGive(s_lock);
    return true;
}

void pocket_setup_toggle(void) {
    if (!pocket_setup_active()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_view.hotspot) s_view.wifi_qr = !s_view.wifi_qr;
    if (s_view.wifi_qr)
        snprintf(s_view.qr, sizeof(s_view.qr), "WIFI:T:WPA;S:%s;P:%s;;", s_view.ssid, s_view.password);
    else snprintf(s_view.qr, sizeof(s_view.qr), "%s", s_view.url);
    ++s_view.revision;
    xSemaphoreGive(s_lock);
}

static bool authorized(httpd_req_t *request, bool write) {
    if (!pocket_setup_active() || esp_timer_get_time() >= s_expires || atomic_load(&s_restart_pending)) return false;
    char query[96], key[48], host[48], origin[64] = "";
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "k", key, sizeof(key)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK) return false;
    if (write && httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
    const char *interfaces[] = {"WIFI_AP_DEF", "WIFI_STA_DEF"};
    for (unsigned i = 0; i < 2; ++i) {
        esp_netif_t *interface = esp_netif_get_handle_from_ifkey(interfaces[i]);
        esp_netif_ip_info_t ip;
        if (!interface || esp_netif_get_ip_info(interface, &ip) != ESP_OK || !ip.ip.addr) continue;
        char address[24]; snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
        if (pocket_setup_authorized(s_key, key, host, address, origin, write)) return true;
    }
    return false;
}

static void response_headers(httpd_req_t *request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'none'; frame-ancestors 'none'; base-uri 'none'");
}

static esp_err_t page(httpd_req_t *request) {
    response_headers(request);
    if (!authorized(request, false)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open the QR code shown on the device");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, setup_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status(httpd_req_t *request) {
    response_headers(request);
    if (!authorized(request, false)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    pocket_config_t *config = malloc(sizeof(*config));
    cJSON *root = cJSON_CreateObject();
    if (!config || !root) { free(config); cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    pocket_config_get(config);
    pocket_pc_display_t display;
    char selection[POCKET_PC_DISPLAY_BYTES];
    pocket_config_pc_display_get(&display);
    bool display_valid = pocket_pc_display_format(&display, selection, sizeof(selection));
    pocket_page_layout_t layout; char navigation[POCKET_PAGE_LAYOUT_BYTES];
    pocket_config_page_layout_get(&layout);
    display_valid = display_valid && pocket_page_layout_format(&layout, navigation, sizeof(navigation));
    bool ok = cJSON_AddStringToObject(root, "wifi_ssid", config->wifi_ssid) &&
        cJSON_AddStringToObject(root, "nas_host", config->nas_host) &&
        cJSON_AddStringToObject(root, "companion_url", config->companion_url) &&
        cJSON_AddStringToObject(root, "home_url", config->home_url) &&
        cJSON_AddBoolToObject(root, "codex_ready", config->companion_token[0] != 0) &&
        cJSON_AddBoolToObject(root, "nas_ready", config->nas_community[0] != 0) &&
        cJSON_AddBoolToObject(root, "home_ready", config->home_token[0] != 0) &&
        display_valid && cJSON_AddStringToObject(root, "pc_display", selection) &&
        cJSON_AddStringToObject(root, "page_layout", navigation) &&
        cJSON_AddNumberToObject(root,"screen_timeout",pocket_config_screen_timeout_get()) &&
        cJSON_AddNumberToObject(root,"brightness",pocket_config_brightness_get());
    memset(config, 0, sizeof(*config)); free(config);
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL; cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(request, "application/json");
    esp_err_t error = httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
    free(json); return error;
}

static esp_err_t save(httpd_req_t *request) {
    response_headers(request);
    char content_type[48];
    if (!authorized(request, true)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") || request->content_len < 2 || request->content_len > 2304)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid settings");
    char *body = calloc(request->content_len + 1, 1);
    if (!body) return ESP_ERR_NO_MEM;
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) { memset(body, 0, request->content_len + 1); free(body); return ESP_FAIL; }
        received += count;
    }
    bool valid = strlen(body) == received && pocket_json_flat_object(body, received);
    cJSON *root = valid ? cJSON_Parse(body) : NULL;
    valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) > 0;
    const char *allowed[] = {"wifi_ssid", "wifi_password", "companion_url", "companion_token", "nas_host", "nas_community", "home_url", "home_token"};
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        bool known = false;
        for (unsigned i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) if (!strcmp(item->string, allowed[i])) known = true;
        if (!known || !cJSON_IsString(item)) valid = false;
    }
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "wifi_ssid");
    if (ssid && (!cJSON_IsString(ssid) || !ssid->valuestring[0])) valid = false;
    esp_err_t error = valid ? pocket_config_update(body) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root); memset(body, 0, received + 1); free(body);
    if (error != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Settings were not saved");
    mark_saved();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"restart\":true}");
}

static esp_err_t pc_display_save(httpd_req_t *request) {
    response_headers(request);
    char type[48], body[513] = {0};
    if (!authorized(request, true)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    if (request->content_len < 2 || request->content_len > 512 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK || strcmp(type, "application/json"))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid display selection");
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    bool valid = strlen(body) == received && pocket_json_flat_object(body, received);
    cJSON *root = valid ? cJSON_Parse(body) : NULL;
    cJSON *selection = cJSON_GetObjectItemCaseSensitive(root, "pc_display");
    valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsString(selection);
    esp_err_t error = valid ? pocket_config_pc_display_save(selection->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (error != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Display selection was not saved");
    mark_saved();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"restart\":true}");
}

static esp_err_t page_layout_save(httpd_req_t *request) {
    response_headers(request);
    char type[48], body[161] = {0};
    if (!authorized(request, true)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    if (request->content_len < 2 || request->content_len > 160 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK || strcmp(type, "application/json"))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid display selection");
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    bool valid = strlen(body) == received && pocket_json_flat_object(body, received);
    cJSON *root = valid ? cJSON_Parse(body) : NULL;
    cJSON *selection = cJSON_GetObjectItemCaseSensitive(root, "page_layout");
    valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsString(selection);
    esp_err_t error = valid ? pocket_config_page_layout_save(selection->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (error != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Display selection was not saved");
    mark_saved();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"restart\":true}");
}

static esp_err_t screen_settings_save(httpd_req_t *request) {
    response_headers(request);
    char type[48], body[161] = {0};
    if (!authorized(request, true)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    if (request->content_len < 2 || request->content_len > 160 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK || strcmp(type, "application/json"))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid display selection");
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    bool valid = strlen(body) == received && pocket_json_flat_object(body, received);
    cJSON *root = valid ? cJSON_Parse(body) : NULL;
    cJSON *selection = cJSON_GetObjectItemCaseSensitive(root, "screen_timeout");
    valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsString(selection);
    unsigned seconds = 0;
    if (valid) {
        const char *value = selection->valuestring;
        size_t length = strlen(value);
        valid = length > 0 && length <= 3;
        for (size_t i = 0; valid && i < length; ++i) {
            valid = value[i] >= '0' && value[i] <= '9';
            if (valid) seconds = seconds * 10 + (unsigned)(value[i] - '0');
        }
    }
    esp_err_t error = valid ? pocket_config_screen_timeout_save(seconds) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (error != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Display selection was not saved");
    mark_saved();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"restart\":true}");
}

static esp_err_t brightness_save(httpd_req_t *request) {
    response_headers(request);
    char type[48], body[97] = {0};
    if (!authorized(request, true)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    if (request->content_len < 2 || request->content_len > 96 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK || strcmp(type, "application/json"))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid brightness");
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += (size_t)count;
    }
    bool valid = strlen(body) == received && pocket_json_flat_object(body, received);
    cJSON *root = valid ? cJSON_Parse(body) : NULL;
    cJSON *selection = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    unsigned percent = 0;
    valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsString(selection) &&
        pocket_screen_brightness_parse(selection->valuestring, &percent);
    esp_err_t error = valid ? pocket_config_brightness_save(percent) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (error != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Brightness was not saved");
    atomic_store(&s_brightness_requested, percent);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"restart\":false}");
}

static esp_err_t screen_off_request(httpd_req_t *request) {
    response_headers(request); char type[48],body[2];
    if(!authorized(request,true))return httpd_resp_send_err(request,HTTPD_403_FORBIDDEN,"Session ended");
    if(request->content_len!=2 || httpd_req_get_hdr_value_str(request,"Content-Type",type,sizeof(type))!=ESP_OK || strcmp(type,"application/json")) return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"Invalid request");
    size_t received=0;while(received<2) { int count=httpd_req_recv(request,body+received,2-received);if(count<=0)return ESP_FAIL;received+=(size_t)count; }
    if(memcmp(body,"{}",2))return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"Invalid request");
    atomic_store(&s_screen_off_requested,true);httpd_resp_set_type(request,"application/json");return httpd_resp_sendstr(request,"{\"ok\":true,\"restart\":false}");
}

static esp_err_t codex_pair(httpd_req_t *request) {
    response_headers(request);
    char type[48], body[257] = {0};
    if (!authorized(request, true)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Session ended");
    if (request->content_len < 2 || request->content_len > 256 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK || strcmp(type, "application/json"))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid pairing");
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    if (strlen(body) != received || !pocket_json_flat_object(body, received)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid pairing");
    cJSON *root = cJSON_Parse(body);
    cJSON *address = cJSON_GetObjectItemCaseSensitive(root, "address"), *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    struct in_addr ip;
    bool valid = cJSON_IsString(address) && cJSON_IsString(code) && strlen(code->valuestring) == 6 &&
        strspn(code->valuestring, "0123456789") == 6 && inet_pton(AF_INET, address->valuestring, &ip) == 1;
    uint32_t host = valid ? ntohl(ip.s_addr) : 0;
    valid = valid && ((host >> 24) == 10 || (host >> 20) == 0xac1 || (host >> 16) == 0xc0a8);
    if (!valid) { cJSON_Delete(root); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid pairing"); }
    char url[80], payload[32], base[64];
    snprintf(base, sizeof(base), "http://%s:8765", address->valuestring);
    snprintf(url, sizeof(url), "%s/v1/pair", base);
    snprintf(payload, sizeof(payload), "{\"code\":\"%s\"}", code->valuestring);
    cJSON_Delete(root); memset(body, 0, sizeof(body));
    char *response = NULL; int status_code;
    esp_err_t error = pocket_http(url, HTTP_METHOD_POST, NULL, 0, payload, &response, &status_code);
    memset(payload, 0, sizeof(payload));
    if (error != ESP_OK) { free(response); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Check the computer address, pairing code, assistant and firewall"); }
    root = cJSON_Parse(response);
    memset(response, 0, strlen(response)); free(response);
    cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
    valid = cJSON_IsString(token) && strlen(token->valuestring) >= 32 && strlen(token->valuestring) <= 128 &&
        strspn(token->valuestring, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(token->valuestring);
    cJSON *settings = cJSON_CreateObject();
    valid = valid && settings && cJSON_AddStringToObject(settings, "companion_url", base) &&
        cJSON_AddStringToObject(settings, "companion_token", token->valuestring);
    char *json = valid ? cJSON_PrintUnformatted(settings) : NULL;
    cJSON_Delete(settings); cJSON_Delete(root);
    error = json ? pocket_config_update(json) : ESP_ERR_INVALID_RESPONSE;
    if (json) { memset(json, 0, strlen(json)); free(json); }
    if (error != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Pairing was not saved; generate a new code and retry");
    mark_saved();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"restart\":true}");
}

esp_err_t pocket_setup_open(bool connected) {
    if (pocket_setup_active()) return ESP_OK;
    pocket_setup_view_t view = {.hotspot = !connected, .wifi_qr = !connected};
    uint8_t random[16]; esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < sizeof(random); ++i) snprintf(s_key + i * 2, 3, "%02x", random[i]);
    esp_fill_random(random, sizeof(random)); /* Hotspot credentials are independent of the web access key. */
    esp_netif_t *interface;
    esp_err_t error;
    if (!connected) {
        interface = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!interface) interface = esp_netif_create_default_wifi_ap();
        if (!interface) return ESP_ERR_NO_MEM;
        snprintf(view.ssid, sizeof(view.ssid), "Passport-%02X%02X", random[0], random[1]);
        static const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
        for (unsigned i = 0; i < 10; ++i) view.password[i] = alphabet[random[i] % (sizeof(alphabet) - 1)];
        wifi_config_t ap = {0};
        memcpy(ap.ap.ssid, view.ssid, strlen(view.ssid));
        memcpy(ap.ap.password, view.password, strlen(view.password));
        ap.ap.ssid_len = strlen(view.ssid); ap.ap.channel = 1; ap.ap.max_connection = 2;
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        error = esp_wifi_stop();
        if (error != ESP_OK) return error;
        error = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_AP, &ap);
        memset(&ap, 0, sizeof(ap));
        if (error == ESP_OK) error = esp_wifi_start();
        if (error != ESP_OK) { esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_start(); return error; }
    } else interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!interface || esp_netif_get_ip_info(interface, &ip) != ESP_OK) {
        if (view.hotspot) esp_wifi_set_mode(WIFI_MODE_STA);
        return ESP_FAIL;
    }
    snprintf(view.address, sizeof(view.address), IPSTR, IP2STR(&ip.ip));
    snprintf(view.url, sizeof(view.url), "http://%s/?k=%s", view.address, s_key);
    if (view.wifi_qr) snprintf(view.qr, sizeof(view.qr), "WIFI:T:WPA;S:%s;P:%s;;", view.ssid, view.password);
    else snprintf(view.qr, sizeof(view.qr), "%s", view.url);
    xSemaphoreTake(s_lock, portMAX_DELAY); s_view = view; xSemaphoreGive(s_lock);
    atomic_store(&s_restart_pending, false); s_expires = esp_timer_get_time() + 600000000;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80; config.ctrl_port = 32769; config.max_open_sockets = 2;
    config.max_uri_handlers = 10;
    config.stack_size = 8192; config.lru_purge_enable = true; config.recv_wait_timeout = 5; config.send_wait_timeout = 5;
    error = httpd_start(&s_server, &config);
    if (error != ESP_OK) { if (view.hotspot) esp_wifi_set_mode(WIFI_MODE_STA); return error; }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/status", .method = HTTP_GET, .handler = status},
        {.uri = "/save", .method = HTTP_POST, .handler = save},
        {.uri = "/codex-pair", .method = HTTP_POST, .handler = codex_pair},
        {.uri = "/pc-display", .method = HTTP_POST, .handler = pc_display_save},
        {.uri = "/page-layout", .method = HTTP_POST, .handler = page_layout_save},
        {.uri="/display-settings",.method=HTTP_POST,.handler=screen_settings_save},
        {.uri="/screen-off",.method=HTTP_POST,.handler=screen_off_request},
        {.uri="/brightness",.method=HTTP_POST,.handler=brightness_save},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        error = httpd_register_uri_handler(s_server, &routes[i]);
        if (error != ESP_OK) {
            atomic_store(&s_active, true);
            pocket_setup_close(); return error;
        }
    }
    atomic_store(&s_active, true);
    return ESP_OK;
}

esp_err_t pocket_setup_close(void) {
    if (!pocket_setup_active()) return ESP_OK;
    esp_err_t error = httpd_stop(s_server);
    if (error != ESP_OK) return error;
    s_server = NULL;
    atomic_store(&s_active, false);
    atomic_store(&s_restart_pending, false);
    memset(s_key, 0, sizeof(s_key));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool hotspot = s_view.hotspot; memset(&s_view, 0, sizeof(s_view));
    xSemaphoreGive(s_lock);
    if (hotspot) return esp_wifi_set_mode(WIFI_MODE_STA);
    return ESP_OK;
}

void pocket_setup_tick(void) {
    if (!pocket_setup_active()) return;
    int64_t now = esp_timer_get_time();
    uint32_t saved = atomic_load(&s_saved);
    if (atomic_load(&s_restart_pending) && (uint32_t)((uint32_t)(now / 1000) - saved) > 1000) esp_restart();
    if (now >= s_expires) (void)pocket_setup_close();
}
