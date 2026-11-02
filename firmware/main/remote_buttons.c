#include "remote_buttons.h"
void remote_buttons_reset(remote_buttons_t *s, uint64_t now) {
    *s = (remote_buttons_t){.since = now};
}
bool remote_buttons_poll(remote_buttons_t *s, uint64_t now, unsigned pressed, bool enabled,
                         unsigned current, unsigned *requested) {
    pressed &= 15;
    if (!enabled) {
        s->released = false;
        s->minus_hold = false;
    }
    if (now < s->since) {
        remote_buttons_reset(s, now);
        return false;
    }
    if (pressed != s->candidate) {
        s->minus_hold = false;
        s->candidate = pressed;
        s->since = now;
        return false;
    }
    if (now - s->since < 30000)
        return false;
    if (!pressed) {
        s->stable = 0;
        s->released = enabled;
        return false;
    }
    if (pressed == s->stable) {
        if (enabled && s->minus_hold && pressed == 2 && now - s->since >= 2000000) {
            s->shutdown = true;
            s->minus_hold = false;
        }
        return false;
    }
    s->stable = pressed;
    bool allow = s->released && enabled;
    s->released = false;
    if (!allow || current > 3)
        return false;
    s->event = pressed;
    if (pressed == 1)
        *requested = current < 3 ? current + 1 : 3;
    else if (pressed == 2) {
        s->minus_hold = true;
        *requested = current ? current - 1 : 0;
    } else if (pressed == 8)
        *requested = current >= 3 ? 1 : current + 1;
    else
        return false; /* Walk is reserved for a separate hold/release validation. */
    return *requested != current;
}
