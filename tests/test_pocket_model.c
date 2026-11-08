#include <assert.h>
#include <string.h>

#include "pocket_model.h"
#include "pocket_json.h"

int main(void) {
    const char *valid = " {\"value\":\"quoted { [ } ]\"} \n";
    assert(pocket_json_flat_object(valid, strlen(valid)));
    assert(!pocket_json_flat_object("{\"x\":{}}", 8));
    assert(!pocket_json_flat_object("{\"x\":[]}", 8));
    assert(!pocket_json_flat_object("{}{}", 4));
    assert(!pocket_json_flat_object("{\"x\":\"", 6));
    pocket_model_t model;
    pocket_model_init(&model);
    assert(model.page == POCKET_GENSHIN);
    assert(!model.snapshot[0].has_data);
    assert(strcmp(model.snapshot[0].value[0], "--") == 0);
    pocket_model_turn(&model, -1);
    assert(model.page == POCKET_PC);
    pocket_model_turn(&model, 1);
    assert(model.page == POCKET_GENSHIN);
    assert(pocket_model_request(&model, 0));
    assert(!pocket_model_request(&model, 29999));
    assert(pocket_model_request(&model, 30000));
    pocket_model_turn(&model, 1);
    assert(pocket_model_request(&model, 30000));
    assert(!pocket_model_request(&model, -1));
    pocket_snapshot_t fresh = {0};
    strcpy(fresh.value[0], "75%");
    pocket_model_success(&model, POCKET_CODEX, &fresh, 1234);
    pocket_model_failure(&model, POCKET_CODEX, POCKET_FAILED, "offline");
    assert(model.snapshot[POCKET_CODEX].status == POCKET_STALE);
    assert(model.snapshot[POCKET_CODEX].updated_at == 1234);
    assert(strcmp(model.snapshot[POCKET_CODEX].value[0], "75%") == 0);
    pocket_model_failure(&model, POCKET_GENSHIN, POCKET_AUTH_REQUIRED, "login");
    assert(model.snapshot[POCKET_GENSHIN].status == POCKET_AUTH_REQUIRED);
    assert(!model.snapshot[POCKET_GENSHIN].has_data);
    pocket_model_success(&model, POCKET_CODEX, &fresh, 2000);
    assert(model.snapshot[POCKET_CODEX].status == POCKET_READY);
    assert(model.snapshot[POCKET_CODEX].updated_at == 2000);
    pocket_page_layout_t layout;
    char encoded[POCKET_PAGE_LAYOUT_BYTES];
    assert(pocket_page_layout_parse("pc,-genshin,codex,-nas,home", &layout));
    assert(pocket_page_layout_format(&layout, encoded, sizeof(encoded)));
    assert(!strcmp(encoded, "pc,-genshin,codex,-nas,home"));
    assert(pocket_model_layout(&model, &layout) && model.page == POCKET_PC);
    pocket_model_turn(&model, 1); assert(model.page == POCKET_CODEX);
    pocket_model_turn(&model, 1); assert(model.page == POCKET_MIHOME);
    pocket_model_turn(&model, 1); assert(model.page == POCKET_PC);
    pocket_model_turn(&model, -1); assert(model.page == POCKET_MIHOME);
    const char *bad[] = {"", "pc", "pc,pc,codex,nas,home", "-pc,-genshin,-codex,-nas,-home",
        "pc,genshin,codex,nas,wrong", "pc,genshin,codex,nas,home,", "pc, genshin,codex,nas,home"};
    pocket_page_layout_t saved = layout;
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        assert(!pocket_page_layout_parse(bad[i], &layout));
        assert(!memcmp(&layout, &saved, sizeof(layout)));
    }
    assert(pocket_page_layout_parse("-genshin,-codex,-nas,-home,pc", &layout));
    assert(pocket_model_layout(&model, &layout));
    for (unsigned i = 0; i < 12; ++i) { pocket_model_turn(&model, i%2 ? 1 : -1); assert(model.page == POCKET_PC); }
    return 0;
}
