#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct pocket_verification pocket_verification_t;
/* Only the public captcha configuration is exposed; never pass account tokens. */
esp_err_t pocket_verification_open(const char *public_json, pocket_verification_t **session, char *url, unsigned url_size);
bool pocket_verification_take(pocket_verification_t *session, char *proof, unsigned capacity);
esp_err_t pocket_verification_close(pocket_verification_t *session);
