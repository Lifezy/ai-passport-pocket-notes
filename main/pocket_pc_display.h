#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "pocket_model.h"

#define POCKET_PC_DISPLAY_MAX 24
#define POCKET_PC_DISPLAY_BYTES 384
#define POCKET_PC_ROWS 6

typedef enum {
    POCKET_PC_NONE, POCKET_PC_CPU, POCKET_PC_GPU, POCKET_PC_GPU_TEMP,
    POCKET_PC_MEMORY, POCKET_PC_GPU_MEMORY, POCKET_PC_DISK,
    POCKET_PC_DISK_FREE, POCKET_PC_UPTIME, POCKET_PC_CONNECTION,
    POCKET_PC_RAM_USED, POCKET_PC_RAM_FREE, POCKET_PC_RAM_TOTAL,
    POCKET_PC_VRAM_USED, POCKET_PC_VRAM_FREE, POCKET_PC_VRAM_TOTAL,
    POCKET_PC_GPU_POWER, POCKET_PC_GPU_FAN, POCKET_PC_GPU_CLOCK,
    POCKET_PC_CPU_CLOCK, POCKET_PC_DISK_READ, POCKET_PC_DISK_WRITE,
    POCKET_PC_NET_RX, POCKET_PC_NET_TX,
    POCKET_PC_METRIC_COUNT,
} pocket_pc_metric_t;

typedef struct {
    pocket_pc_metric_t metric[POCKET_PC_DISPLAY_MAX];
    unsigned count;
} pocket_pc_display_t;

typedef struct {
    double value[POCKET_PC_METRIC_COUNT];
    unsigned valid;
} pocket_pc_data_t;
const char *pocket_pc_metric_key(pocket_pc_metric_t metric);
void pocket_pc_data_format(const pocket_pc_data_t *data, pocket_pc_metric_t metric, char *text, size_t size);
unsigned pocket_pc_pages(const pocket_pc_display_t *display);
void pocket_pc_navigate(pocket_model_t *model, unsigned *subpage, unsigned pages, int direction);

void pocket_pc_display_default(pocket_pc_display_t *display);
/* One to POCKET_PC_DISPLAY_MAX distinct IDs, comma-separated, no whitespace.
 * Unknown IDs are rejected; failure preserves output. Six rows form a UI subpage. */
bool pocket_pc_display_parse(const char *text, pocket_pc_display_t *display);
bool pocket_pc_display_format(const pocket_pc_display_t *display, char *text, size_t capacity);
const char *pocket_pc_metric_label(pocket_pc_metric_t metric);
