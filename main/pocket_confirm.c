#include "pocket_confirm.h"
#include <string.h>

bool pocket_confirm_arm(pocket_confirm_t *state, const char *entity_id, unsigned action, int64_t now_ms) {
    if (state->busy || !entity_id || !*entity_id || strlen(entity_id) >= sizeof(state->entity_id) || now_ms < 0) return false;
    strcpy(state->entity_id, entity_id);
    state->action = action; state->armed_ms = now_ms; state->armed = true;
    return true;
}

bool pocket_confirm_consume(pocket_confirm_t *state, const char *entity_id, unsigned action, int64_t now_ms) {
    bool valid = state->armed && !state->busy && entity_id &&
        !strcmp(state->entity_id, entity_id) && state->action == action &&
        now_ms >= state->armed_ms && now_ms - state->armed_ms <= 30000;
    state->armed = false;
    if (valid) state->busy = true;
    return valid;
}

void pocket_confirm_cancel(pocket_confirm_t *state) { state->armed = false; }
void pocket_confirm_complete(pocket_confirm_t *state) { state->busy = false; }
