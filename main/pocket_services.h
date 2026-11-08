#pragma once
/* PC metrics reuse the same authenticated local companion as Codex. */
#include "pocket_config.h"
#include "pocket_model.h"

esp_err_t pocket_nas_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot);
esp_err_t pocket_codex_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot);
esp_err_t pocket_pc_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot, pocket_pc_data_t *metrics);
