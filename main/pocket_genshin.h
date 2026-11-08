#pragma once

#include "pocket_config.h"
#include "pocket_model.h"

/* Progress callback runs on the network worker; false cancels login. */
typedef bool (*pocket_login_progress_t)(const char *qr, const char *message, void *context);

esp_err_t pocket_genshin_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot, int *api_code);
esp_err_t pocket_genshin_login(pocket_login_progress_t progress, void *context, int *api_code);
esp_err_t pocket_genshin_verify(pocket_login_progress_t progress, void *context, int *api_code);
/* Network-worker only; contains a fixed stage label and numeric codes, never credentials. */
void pocket_genshin_last_error(char *output, size_t capacity, esp_err_t error);
