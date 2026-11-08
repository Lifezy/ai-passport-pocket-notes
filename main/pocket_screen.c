#include "pocket_screen.h"
#include <limits.h>

bool pocket_screen_brightness_parse(const char *text, unsigned *percent) {
    if (!text || !percent || !text[0]) return false;
    unsigned value = 0, count = 0;
    for (; *text; ++text) {
        if (++count > 3 || *text < '0' || *text > '9') return false;
        value = value * 10 + (unsigned)(*text - '0');
    }
    if (value < 10 || value > 100) return false;
    *percent = value;
    return true;
}

bool pocket_screen_timeout_valid(unsigned seconds) {
    return seconds == 0 || seconds == 30 || seconds == 60 || seconds == 120 || seconds == 300;
}
void pocket_screen_init(pocket_screen_t *s, unsigned seconds, int64_t now) {
    *s = (pocket_screen_t){.last_activity=now, .timeout_seconds=pocket_screen_timeout_valid(seconds) ? seconds : 60};
}
bool pocket_screen_key(pocket_screen_t *s, unsigned event, int64_t now) {
    if (event > 4 || now < 0) return true;
    s->last_activity = now;
    if (event == 0) s->held = true;
    if (event == 4) s->held = false;
    if (s->off && s->suppress_until > now) {
        if (event == 4) s->suppress_until = now+500;
        return true;
    }
    if (s->off) { s->off = false; s->suppress_until = s->held ? INT64_MAX : now+500; return true; }
    if (s->suppress_until > now) {
        if (event == 0) s->suppress_until = INT64_MAX;
        if (event == 4) s->suppress_until = now+500;
        return true;
    }
    return event == 0 || event == 4;
}
bool pocket_screen_tick(pocket_screen_t *s, bool keep_awake, int64_t now) {
    if (keep_awake || s->held) s->last_activity = now;
    if (!s->off && !keep_awake && !s->held && s->timeout_seconds && now >= s->last_activity &&
        now-s->last_activity >= (int64_t)s->timeout_seconds*1000) s->off = true;
    return s->off;
}
void pocket_screen_off(pocket_screen_t *s, int64_t now) {
    s->off=true; s->last_activity=now; s->suppress_until=s->held ? INT64_MAX : now;
}

void pocket_display_menu_open(pocket_display_menu_t *m, unsigned brightness, unsigned timeout) {
    *m = (pocket_display_menu_t){.open=true, .brightness=brightness, .timeout=timeout,
        .saved_brightness=brightness, .saved_timeout=timeout};
}

unsigned pocket_display_menu_key(pocket_display_menu_t *m, unsigned button, unsigned event) {
    if (!m->open || m->saving || button > 2) return POCKET_DISPLAY_NO_SAVE;
    if (button != 2 && event == 2) {
        (void)pocket_display_menu_key(m, button, 1);
        (void)pocket_display_menu_key(m, button, 1);
        return POCKET_DISPLAY_NO_SAVE;
    }
    if (button == 2 && event == 3) {
        if (m->editing) {
            m->brightness = m->saved_brightness; m->timeout = m->saved_timeout;
            m->editing = false; m->error = false;
        } else m->open = false;
        return POCKET_DISPLAY_NO_SAVE;
    }
    if (event != 1) return POCKET_DISPLAY_NO_SAVE;
    m->error = false;
    if (button == 2) {
        if (m->row == 2) { m->open = false; return POCKET_DISPLAY_NO_SAVE; }
        if (!m->editing) { m->editing = true; return POCKET_DISPLAY_NO_SAVE; }
        if ((m->row == 0 && m->brightness == m->saved_brightness) ||
            (m->row == 1 && m->timeout == m->saved_timeout)) {
            m->editing = false; return POCKET_DISPLAY_NO_SAVE;
        }
        m->saving = true;
        return m->row == 0 ? POCKET_DISPLAY_SAVE_BRIGHTNESS : POCKET_DISPLAY_SAVE_TIMEOUT;
    }
    if (!m->editing) m->row = button == 0 ? (m->row + 2) % 3 : (m->row + 1) % 3;
    else if (m->row == 0) {
        if (button == 0) m->brightness = m->brightness > 95 ? 100 : m->brightness + 5;
        else m->brightness = m->brightness < 15 ? 10 : m->brightness - 5;
    } else {
        static const unsigned times[] = {0, 30, 60, 120, 300};
        unsigned index = 0;
        for (unsigned i = 0; i < 5; ++i) if (times[i] == m->timeout) index = i;
        if (button == 0 && index < 4) ++index;
        if (button == 1 && index > 0) --index;
        m->timeout = times[index];
    }
    return POCKET_DISPLAY_NO_SAVE;
}

void pocket_display_menu_saved(pocket_display_menu_t *m, bool success) {
    if (!m->saving) return;
    m->saving = false; m->error = !success;
    if (success) {
        m->saved_brightness = m->brightness; m->saved_timeout = m->timeout;
        m->editing = false;
    } else {
        m->brightness = m->saved_brightness; m->timeout = m->saved_timeout;
    }
}
