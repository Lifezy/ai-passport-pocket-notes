#pragma once

#include "esp_err.h"
#include "esp_http_client.h"

typedef struct { const char *name, *value; } pocket_header_t;

/* Worker-task only. Caller owns *response; redirects are never followed. */
esp_err_t pocket_http(const char *url, esp_http_client_method_t method,
                      const pocket_header_t *headers, unsigned header_count,
                      const char *body, char **response, int *status);
