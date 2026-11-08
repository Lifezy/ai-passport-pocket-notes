#include <assert.h>
#include "pocket_confirm.h"

int main(void) {
    pocket_confirm_t state = {0};
    assert(!pocket_confirm_consume(&state, "light.desk", 0, 0));
    assert(pocket_confirm_arm(&state, "light.desk", 0, 100));
    assert(!pocket_confirm_consume(&state, "light.other", 0, 101));
    assert(!state.armed && !state.busy);
    assert(pocket_confirm_arm(&state, "light.desk", 0, 100));
    assert(!pocket_confirm_consume(&state, "light.desk", 1, 101));
    assert(pocket_confirm_arm(&state, "light.desk", 0, 100));
    assert(!pocket_confirm_consume(&state, "light.desk", 0, 30101));
    assert(pocket_confirm_arm(&state, "light.desk", 0, 100));
    assert(!pocket_confirm_consume(&state, "light.desk", 0, 99));
    assert(pocket_confirm_arm(&state, "light.desk", 0, 100));
    pocket_confirm_cancel(&state);
    assert(!pocket_confirm_consume(&state, "light.desk", 0, 101));
    assert(pocket_confirm_arm(&state, "light.desk", 0, 100));
    assert(pocket_confirm_consume(&state, "light.desk", 0, 30100));
    assert(state.busy && !state.armed);
    assert(!pocket_confirm_consume(&state, "light.desk", 0, 30100));
    pocket_confirm_cancel(&state);
    assert(state.busy);
    assert(!pocket_confirm_arm(&state, "light.desk", 0, 30101));
    pocket_confirm_complete(&state);
    assert(pocket_confirm_arm(&state, "light.desk", 1, 30101));
    return 0;
}
