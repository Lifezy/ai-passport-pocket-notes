#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "pocket_setup_view.h"

esp_err_t pocket_setup_init(void);
/* Network worker owns open/close/tick; Wi-Fi has already been initialized. */
esp_err_t pocket_setup_open(bool connected);
esp_err_t pocket_setup_close(void);
void pocket_setup_tick(void);
bool pocket_setup_view(pocket_setup_view_t *view);
bool pocket_setup_active(void);
bool pocket_setup_take_screen_off(void);
/* UI task consumes a saved brightness request; zero means no update. */
unsigned pocket_setup_take_brightness(void);
void pocket_setup_toggle(void);
