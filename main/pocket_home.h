#pragma once
#include "pocket_config.h"

#define POCKET_HOME_BATCH 4
#define POCKET_HOME_ACTIONS 8
typedef struct {
    char id[128], name[128], state[64];
    bool has_number, has_temperature, has_brightness;
    double number, temperature, minimum, maximum, step;
} pocket_home_entity_t;
typedef struct {
    unsigned offset, total, count;
    pocket_home_entity_t entity[POCKET_HOME_BATCH];
} pocket_home_batch_t;
typedef struct {
    const char *label, *service, *parameter;
    double number;
    bool has_number;
} pocket_home_action_t;

esp_err_t pocket_home_list(const pocket_config_t *config, unsigned offset, pocket_home_batch_t *batch);
unsigned pocket_home_actions(const pocket_home_entity_t *entity, pocket_home_action_t actions[POCKET_HOME_ACTIONS]);
esp_err_t pocket_home_execute(const pocket_config_t *config, const pocket_home_entity_t *entity, unsigned action_index);
