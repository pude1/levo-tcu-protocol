#include "touch_controls.h"
int ui_hit(int x, int y) {
    if (x < 8 || x >= 164)
        return UI_NONE;
    if (y >= 0 && y < 78)
        return UI_SHUTDOWN;
    if (y >= 88 && y < 194)
        return UI_PLUS;
    if (y >= 204 && y < 312)
        return UI_MINUS;
    return UI_NONE;
}
unsigned ui_requested(ui_action_t a, unsigned m) {
    if (m > 3)
        return 0;
    if (a >= UI_OFF && a <= UI_TURBO)
        return (unsigned)a;
    if (a == UI_MINUS)
        return m ? m - 1 : 0;
    if (a == UI_PLUS)
        return m < 3 ? m + 1 : 3;
    return m;
}

ui_action_t ui_hold_action(ui_action_t pressed, int64_t held_us) {
    if (held_us >= 2000000 && (pressed == UI_MINUS || pressed == UI_SHUTDOWN))
        return UI_SHUTDOWN;
    if (held_us >= 3000000 && pressed == UI_PLUS)
        return UI_CALIBRATE;
    return UI_NONE;
}
