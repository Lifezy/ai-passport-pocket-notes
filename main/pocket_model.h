#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define POCKET_PAGE_COUNT 5
#define POCKET_ROW_COUNT 6
#define POCKET_VALUE_BYTES 64

typedef enum {
    POCKET_GENSHIN, POCKET_CODEX, POCKET_NAS, POCKET_MIHOME, POCKET_PC,
} pocket_page_t;

#define POCKET_PAGE_LAYOUT_BYTES 96
typedef struct {
    pocket_page_t order[POCKET_PAGE_COUNT];
    bool enabled[POCKET_PAGE_COUNT];
} pocket_page_layout_t;
void pocket_page_layout_default(pocket_page_layout_t *layout);
bool pocket_page_layout_parse(const char *text, pocket_page_layout_t *layout);
bool pocket_page_layout_format(const pocket_page_layout_t *layout, char *text, size_t size);

typedef enum {
    POCKET_UNCONFIGURED, POCKET_WAITING_NETWORK, POCKET_LOADING,
    POCKET_READY, POCKET_STALE, POCKET_AUTH_REQUIRED, POCKET_FAILED,
} pocket_status_t;

typedef struct {
    pocket_status_t status;
    bool has_data;
    int64_t updated_at;
    char value[POCKET_ROW_COUNT][POCKET_VALUE_BYTES];
    char message[96];
} pocket_snapshot_t;

typedef struct {
    pocket_page_t page;
    pocket_page_layout_t layout;
    pocket_snapshot_t snapshot[POCKET_PAGE_COUNT];
    int64_t last_request_ms[POCKET_PAGE_COUNT];
    bool requested[POCKET_PAGE_COUNT];
} pocket_model_t;

void pocket_model_init(pocket_model_t *model);
bool pocket_model_layout(pocket_model_t *model, const pocket_page_layout_t *layout);
void pocket_model_turn(pocket_model_t *model, int direction);
bool pocket_model_request(pocket_model_t *model, int64_t monotonic_ms);
void pocket_model_success(pocket_model_t *model, pocket_page_t page,
                          const pocket_snapshot_t *snapshot, int64_t wall_seconds);
void pocket_model_failure(pocket_model_t *model, pocket_page_t page,
                          pocket_status_t status, const char *message);
