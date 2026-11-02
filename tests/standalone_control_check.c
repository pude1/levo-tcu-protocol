#include "tcu_bench_control.h"
#include "touch_controls.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tcu_bench_control_t c;
    tcu_bench_init(&c, 0);
    assert(c.mode == 0);
    assert(!tcu_bench_select(&c, 0, 0, false, 1));
    for (uint64_t now = 1000; now <= 60000000; now += 1000)
        tcu_bench_tick(&c, now, now);
    assert(!c.stopping && c.mode == 0); // One minute without any USB lease.
    assert(tcu_bench_select(&c, 60000000, 60000000, true, 1));
    tcu_bench_tick(&c, 60500000, 60000000);
    assert(c.stopping && c.mode == 0);
    assert(!tcu_bench_select(&c, 60500000, 60500000, true, 3));
    tcu_bench_init(&c, 0);
    assert(tcu_bench_select(&c, 1, 1, true, 3));
    tcu_bench_stop(&c, 1000);
    assert(c.mode == 0 && !tcu_bench_shutdown_due(&c, 5000999));
    assert(tcu_bench_shutdown_due(&c, 5001000));
    assert(ui_hit(86, 40) == UI_SHUTDOWN);
    assert(ui_hit(86, 140) == UI_PLUS);
    assert(ui_hit(86, 250) == UI_MINUS);
    assert(ui_hold_action(UI_MINUS, 1999999) == UI_NONE);
    assert(ui_hold_action(UI_MINUS, 2000000) == UI_SHUTDOWN);
    assert(ui_hold_action(UI_PLUS, 2999999) == UI_NONE);
    assert(ui_hold_action(UI_PLUS, 3000000) == UI_CALIBRATE);
    assert(ui_requested(UI_CALIBRATE, 2) == 2);
    assert(ui_hold_action(UI_SHUTDOWN, 2000000) == UI_SHUTDOWN);
    assert(ui_hold_action(UI_NONE, 3000000) == UI_NONE);
    puts("PASS: standalone holds without USB; startup OFF; stale motor stops; stop latches; "
         "shutdown dwell and touch zones.");
}
