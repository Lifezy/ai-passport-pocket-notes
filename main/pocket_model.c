#include "pocket_model.h"

#include <stdio.h>
#include <string.h>

static const char *const PAGE_IDS[] = {"genshin", "codex", "nas", "home", "pc"};
_Static_assert(sizeof(PAGE_IDS) / sizeof(PAGE_IDS[0]) == POCKET_PAGE_COUNT, "Extend page IDs");

void pocket_page_layout_default(pocket_page_layout_t *layout) {
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) { layout->order[i] = (pocket_page_t)i; layout->enabled[i] = true; }
}

bool pocket_page_layout_parse(const char *text, pocket_page_layout_t *layout) {
    if (!text || !layout || !*text) return false;
    size_t length = 0;
    while (length < POCKET_PAGE_LAYOUT_BYTES && text[length]) ++length;
    if (length == POCKET_PAGE_LAYOUT_BYTES) return false;
    pocket_page_layout_t next = {0}; unsigned seen = 0, count = 0, active = 0;
    const char *start = text;
    while (*start) {
        bool enabled = *start != '-'; if (!enabled) ++start;
        const char *end = strchr(start, ','); size_t size = end ? (size_t)(end-start) : strlen(start);
        unsigned id = POCKET_PAGE_COUNT;
        for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i)
            if (strlen(PAGE_IDS[i]) == size && !memcmp(start, PAGE_IDS[i], size)) id = i;
        if (id == POCKET_PAGE_COUNT || count == POCKET_PAGE_COUNT || (seen & (1u << id))) return false;
        seen |= 1u << id; next.order[count++] = (pocket_page_t)id; next.enabled[id] = enabled; active += enabled;
        if (!end) break;
        start = end+1; if (!*start) return false;
    }
    if (count != POCKET_PAGE_COUNT || !active) return false;
    *layout = next; return true;
}

bool pocket_page_layout_format(const pocket_page_layout_t *layout, char *text, size_t size) {
    if (!layout || !text) return false;
    char result[POCKET_PAGE_LAYOUT_BYTES] = {0}; size_t used = 0; unsigned seen = 0, active = 0;
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) {
        unsigned id = (unsigned)layout->order[i];
        if (id >= POCKET_PAGE_COUNT || (seen & (1u << id))) return false;
        seen |= 1u << id; active += layout->enabled[id];
        int count = snprintf(result+used, sizeof(result)-used, "%s%s%s", i ? "," : "", layout->enabled[id] ? "" : "-", PAGE_IDS[id]);
        if (count < 0 || (size_t)count >= sizeof(result)-used) return false;
        used += (size_t)count;
    }
    if (!active || used >= size) return false;
    memcpy(text, result, used+1); return true;
}

bool pocket_model_layout(pocket_model_t *model, const pocket_page_layout_t *layout) {
    char validation[POCKET_PAGE_LAYOUT_BYTES];
    if (!pocket_page_layout_format(layout, validation, sizeof(validation))) return false;
    model->layout = *layout;
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i)
        if (layout->enabled[layout->order[i]]) { model->page = layout->order[i]; break; }
    return true;
}

void pocket_model_init(pocket_model_t *model) {
    memset(model, 0, sizeof(*model));
    pocket_page_layout_default(&model->layout);
    for (unsigned page = 0; page < POCKET_PAGE_COUNT; ++page) {
        for (unsigned row = 0; row < POCKET_ROW_COUNT; ++row) {
            snprintf(model->snapshot[page].value[row], POCKET_VALUE_BYTES, "--");
        }
    }
}

void pocket_model_turn(pocket_model_t *model, int direction) {
    if (!direction) return;
    unsigned position = 0;
    while (position < POCKET_PAGE_COUNT && model->layout.order[position] != model->page) ++position;
    if (position == POCKET_PAGE_COUNT) position = 0;
    for (unsigned step = 0; step < POCKET_PAGE_COUNT; ++step) {
        position = (position + (direction > 0 ? 1 : POCKET_PAGE_COUNT-1)) % POCKET_PAGE_COUNT;
        pocket_page_t page = model->layout.order[position];
        if (model->layout.enabled[page]) { model->page = page; return; }
    }
}

bool pocket_model_request(pocket_model_t *model, int64_t monotonic_ms) {
    unsigned page = (unsigned)model->page;
    if (monotonic_ms < 0 || (model->requested[page] &&
        monotonic_ms - model->last_request_ms[page] < 30000)) return false;
    model->requested[page] = true;
    model->last_request_ms[page] = monotonic_ms;
    model->snapshot[page].status = POCKET_LOADING;
    model->snapshot[page].message[0] = '\0';
    return true;
}

void pocket_model_success(pocket_model_t *model, pocket_page_t page,
                          const pocket_snapshot_t *snapshot, int64_t wall_seconds) {
    if ((unsigned)page >= POCKET_PAGE_COUNT) return;
    model->snapshot[page] = *snapshot;
    model->snapshot[page].status = POCKET_READY;
    model->snapshot[page].has_data = true;
    model->snapshot[page].updated_at = wall_seconds;
    model->snapshot[page].message[0] = '\0';
}

void pocket_model_failure(pocket_model_t *model, pocket_page_t page,
                          pocket_status_t status, const char *message) {
    if ((unsigned)page >= POCKET_PAGE_COUNT) return;
    pocket_snapshot_t *snapshot = &model->snapshot[page];
    snapshot->status = snapshot->has_data && status == POCKET_FAILED ? POCKET_STALE : status;
    snprintf(snapshot->message, sizeof(snapshot->message), "%s", message ? message : "");
}
