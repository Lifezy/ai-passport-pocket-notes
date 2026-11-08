#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define ESP_ERR_INVALID_SIZE 0x104
#include "esp_http_client.h"
static size_t largest_allocation;
static bool fail_growth;
static bool fail_initial;
static void *tracked_malloc(size_t size) {
    if (size > largest_allocation) largest_allocation = size;
    return fail_initial ? NULL : malloc(size);
}
static void *tracked_realloc(void *pointer, size_t size) {
    if (size > largest_allocation) largest_allocation = size;
    return fail_growth ? NULL : realloc(pointer, size);
}
#define malloc tracked_malloc
#define realloc tracked_realloc
#include "../main/pocket_http.c"
#undef malloc
#undef realloc
static size_t body_size, position;
static int64_t announced;
static bool chunked, finished, truncate_body;
static int tx_size, cleanup_count;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    tx_size=config->buffer_size_tx; assert(config->disable_auto_redirect);
    assert(config->crt_bundle_attach); return (void *)1;
}
esp_err_t esp_http_client_set_header(void *client,const char *key,const char *value) {(void)client;(void)key;(void)value;return ESP_OK;}
esp_err_t esp_http_client_open(void *client,int length) {(void)client;(void)length;return ESP_OK;}
int esp_http_client_write(void *client,const char *data,int size) {(void)client;(void)data;return size;}
int64_t esp_http_client_fetch_headers(void *client) {(void)client;return announced;}
int esp_http_client_get_status_code(void *client) {(void)client;return 200;}
bool esp_http_client_is_complete_data_received(void *client) {(void)client;return finished;}
int esp_http_client_read(void *client,char *buffer,int capacity) {
    (void)client;
    if (position==body_size) { if (!truncate_body) finished=true; return 0; }
    size_t count=body_size-position;
    if (count>(size_t)capacity) count=(size_t)capacity;
    if (count>311) count=311;
    memset(buffer,'x',count); position+=count;
    if (!chunked && !truncate_body && position==body_size) finished=true;
    return (int)count;
}
esp_err_t esp_http_client_cleanup(void *client) {(void)client;++cleanup_count;return ESP_OK;}
static void reset(size_t size,bool chunk) {
    body_size=size; announced=chunk?0:(int64_t)size;position=0;chunked=chunk;
    finished=false;truncate_body=false;fail_growth=false;fail_initial=false;largest_allocation=0;cleanup_count=0;
}
static esp_err_t call(char **response) {int status;return pocket_http("https://example.invalid",HTTP_METHOD_GET,NULL,0,NULL,response,&status);}
int main(void) {
    char *response;
    reset(173,false);assert(call(&response)==ESP_OK);assert(strlen(response)==173);
    assert(largest_allocation==174 && tx_size==1024 && cleanup_count==1);free(response);
    reset(173,false);finished=true;assert(call(&response)==ESP_OK);assert(strlen(response)==173);free(response);
    reset(173,false);fail_initial=true;assert(call(&response)==ESP_ERR_NO_MEM);assert(response==NULL && cleanup_count==1);
    reset(4096,true);assert(call(&response)==ESP_OK);assert(strlen(response)==4096);free(response);
    reset(12*1024,true);assert(call(&response)==ESP_OK);assert(strlen(response)==12*1024);free(response);
    reset(12*1024+1,true);assert(call(&response)==ESP_ERR_INVALID_SIZE);assert(response==NULL && cleanup_count==1);
    reset(12*1024+1,false);assert(call(&response)==ESP_ERR_INVALID_SIZE);assert(largest_allocation==0);
    reset(4096,true);fail_growth=true;assert(call(&response)==ESP_ERR_NO_MEM);assert(response==NULL && cleanup_count==1);
    reset(100,false);truncate_body=true;assert(call(&response)==ESP_ERR_INVALID_SIZE);assert(response==NULL);
    reset(0,false);assert(call(&response)==ESP_OK);assert(!response[0]);free(response);
    char challenge[4097];memset(challenge,'a',4096);challenge[4096]=0;
    pocket_header_t header={"x-rpc-challenge",challenge};int status;
    reset(50,false);assert(pocket_http("https://example.invalid",HTTP_METHOD_POST,&header,1,"{}",&response,&status)==ESP_OK);
    assert(tx_size>=4608 && tx_size<=6144);free(response);
    return 0;
}
