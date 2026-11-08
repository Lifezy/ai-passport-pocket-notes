#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool armed, busy;
    char entity_id[128];
    unsigned action;
    int64_t armed_ms;
} pocket_confirm_t;

bool pocket_confirm_arm(pocket_confirm_t *state, const char *entity_id, unsigned action, int64_t now_ms);
bool pocket_confirm_consume(pocket_confirm_t *state, const char *entity_id, unsigned action, int64_t now_ms);
void pocket_confirm_cancel(pocket_confirm_t *state);
void pocket_confirm_complete(pocket_confirm_t *state);
