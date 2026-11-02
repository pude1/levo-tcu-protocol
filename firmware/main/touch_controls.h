#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { UI_PREVIEW, UI_WAIT, UI_LIVE, UI_STOPPING, UI_WALK_ARMED, UI_WALK } ui_state_t;
typedef enum {
    UI_OFF,
    UI_ECO,
    UI_TRAIL,
    UI_TURBO,
    UI_MINUS,
    UI_PLUS,
    UI_SHUTDOWN,
    UI_CALIBRATE, /* hold + for 3 s: level IMU calibration */
    UI_NONE = -1
} ui_action_t;
void touch_controls_start(void);
void ui_set(ui_state_t state, unsigned mode);
void ui_get(ui_state_t *state, unsigned *mode, bool *touch_ok);
bool ui_next(ui_action_t *action);
int ui_hit(int x, int y);
unsigned ui_requested(ui_action_t action, unsigned current);

ui_action_t ui_hold_action(ui_action_t pressed, int64_t held_us);
