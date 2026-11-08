#pragma once
#include "pocket_pc_display.h"

#include "pocket_model.h"
#include "pocket_setup_view.h"
#include "pocket_screen.h"
void pocket_ui_display_settings(const pocket_display_menu_t *menu, int battery_percent);

/* All functions require the caller to hold the BSP LVGL lock. */
void pocket_ui_pc_display(const pocket_pc_display_t *display);
void pocket_ui_pc_page(unsigned page);
void pocket_ui_create(void);
void pocket_ui_render(const pocket_model_t *model, int battery_percent);
bool pocket_ui_fonts_valid(void);
void pocket_ui_hint(const char *text);
void pocket_ui_qr(const char *url);
void pocket_ui_setup(const pocket_setup_view_t *view, int battery_percent);
