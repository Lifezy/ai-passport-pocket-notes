#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool off, held;
    int64_t last_activity, suppress_until;
    unsigned timeout_seconds;
} pocket_screen_t;
void pocket_screen_init(pocket_screen_t *screen, unsigned timeout_seconds, int64_t now);
/* Event values follow BSP PRESS/CLICK/DOUBLE/LONG/RELEASE (0..4). No board access. */
bool pocket_screen_key(pocket_screen_t *screen, unsigned event, int64_t now);
bool pocket_screen_tick(pocket_screen_t *screen, bool keep_awake, int64_t now);
void pocket_screen_off(pocket_screen_t *screen, int64_t now);
bool pocket_screen_timeout_valid(unsigned seconds);
/* Brightness is 10..100%; zero is reserved for screen-off. Failure preserves output. */
bool pocket_screen_brightness_parse(const char *text, unsigned *percent);
typedef struct {
    bool open, editing, saving, error;
    unsigned row, brightness, timeout, saved_brightness, saved_timeout;
} pocket_display_menu_t;
enum { POCKET_DISPLAY_NO_SAVE, POCKET_DISPLAY_SAVE_BRIGHTNESS, POCKET_DISPLAY_SAVE_TIMEOUT };
void pocket_display_menu_open(pocket_display_menu_t *menu, unsigned brightness, unsigned timeout);
/* BSP button indices/events; returns a save operation for a worker task. */
unsigned pocket_display_menu_key(pocket_display_menu_t *menu, unsigned button, unsigned event);
void pocket_display_menu_saved(pocket_display_menu_t *menu, bool success);
