#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pocket_screen.h"
int main(void) {
    unsigned brightness = 75;
    assert(pocket_screen_brightness_parse("10", &brightness) && brightness == 10);
    assert(pocket_screen_brightness_parse("100", &brightness) && brightness == 100);
    const char *bad_brightness[] = {"", "0", "9", "101", "1000", "-10", "+50", "50%", " 50", "50 ", "1.5"};
    for (unsigned i = 0; i < sizeof(bad_brightness) / sizeof(bad_brightness[0]); ++i) {
        assert(!pocket_screen_brightness_parse(bad_brightness[i], &brightness) && brightness == 100);
    }
    assert(!pocket_screen_brightness_parse(NULL, &brightness));
    assert(!pocket_screen_brightness_parse("75", NULL));
    pocket_screen_t s;
    pocket_screen_init(&s,60,0);
    assert(!pocket_screen_tick(&s,false,59999));assert(pocket_screen_tick(&s,false,60000));
    assert(pocket_screen_key(&s,0,61000) && !s.off);
    assert(pocket_screen_key(&s,3,61500));assert(pocket_screen_key(&s,4,62000));
    assert(pocket_screen_key(&s,1,62100));assert(pocket_screen_key(&s,2,62180));
    assert(!pocket_screen_key(&s,1,63000));
    assert(!pocket_screen_tick(&s,true,200000));
    assert(!pocket_screen_tick(&s,false,259999));assert(pocket_screen_tick(&s,false,260000));
    pocket_screen_init(&s,0,0);assert(!pocket_screen_tick(&s,false,1000000));
    assert(pocket_screen_key(&s,0,1000001));assert(!pocket_screen_key(&s,3,1000600));
    pocket_screen_off(&s,1000600);assert(s.off);
    assert(pocket_screen_key(&s,4,1000700) && s.off);
    assert(pocket_screen_key(&s,1,1000800) && s.off);
    assert(pocket_screen_key(&s,0,1001300) && !s.off);
    assert(pocket_screen_key(&s,4,1001400));assert(pocket_screen_key(&s,2,1001500));
    pocket_screen_init(&s,30,0);pocket_screen_key(&s,0,1);assert(!pocket_screen_tick(&s,false,90000));
    pocket_screen_key(&s,4,90001);assert(!pocket_screen_tick(&s,false,120000));assert(pocket_screen_tick(&s,false,120001));
    assert(!pocket_screen_timeout_valid(7));pocket_screen_init(&s,7,0);assert(s.timeout_seconds==60);
    pocket_display_menu_t menu;
    pocket_display_menu_open(&menu, 75, 60);
    assert(menu.open && !menu.editing && menu.row == 0);
    assert(pocket_display_menu_key(&menu, 2, 1) == POCKET_DISPLAY_NO_SAVE && menu.editing);
    assert(pocket_display_menu_key(&menu, 2, 1) == POCKET_DISPLAY_NO_SAVE && !menu.editing); /* unchanged: no Flash write */
    pocket_display_menu_key(&menu, 2, 1);
    pocket_display_menu_key(&menu, 0, 2); assert(menu.brightness == 85); /* double click */
    for (unsigned i = 0; i < 30; ++i) pocket_display_menu_key(&menu, 0, 1);
    assert(menu.brightness == 100);
    pocket_display_menu_key(&menu, 2, 3); assert(!menu.editing && menu.brightness == 75 && menu.open);
    pocket_display_menu_key(&menu, 2, 1);
    for (unsigned i = 0; i < 30; ++i) pocket_display_menu_key(&menu, 1, 1);
    assert(menu.brightness == 10);
    assert(pocket_display_menu_key(&menu, 2, 1) == POCKET_DISPLAY_SAVE_BRIGHTNESS && menu.saving);
    pocket_display_menu_key(&menu, 2, 3); assert(menu.open && menu.saving);
    pocket_display_menu_saved(&menu, false); assert(menu.error && menu.editing && menu.brightness == 75);
    pocket_display_menu_key(&menu, 1, 1);
    assert(pocket_display_menu_key(&menu, 2, 1) == POCKET_DISPLAY_SAVE_BRIGHTNESS);
    pocket_display_menu_saved(&menu, true); assert(!menu.editing && menu.saved_brightness == 70);
    pocket_display_menu_key(&menu, 1, 1); assert(menu.row == 1);
    pocket_display_menu_key(&menu, 2, 1);
    for (unsigned i = 0; i < 8; ++i) pocket_display_menu_key(&menu, 0, 1);
    assert(menu.timeout == 300);
    for (unsigned i = 0; i < 8; ++i) pocket_display_menu_key(&menu, 1, 1);
    assert(menu.timeout == 0);
    assert(pocket_display_menu_key(&menu, 2, 1) == POCKET_DISPLAY_SAVE_TIMEOUT);
    pocket_display_menu_saved(&menu, true); assert(menu.saved_timeout == 0 && !menu.editing);
    pocket_display_menu_key(&menu, 1, 1); assert(menu.row == 2);
    pocket_display_menu_key(&menu, 2, 1); assert(!menu.open);
    pocket_display_menu_open(&menu, 70, 0); pocket_display_menu_key(&menu, 2, 3); assert(!menu.open);
    puts("Screen idle/wake, brightness and local display menu: PASS");return 0;
}
