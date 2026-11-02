#include "ride.h"
#include <math.h>
#include <string.h>

#define RAD2DEG 57.29578f
#define SPEED_FRESH_US 300000u
#define CADENCE_FRESH_US 500000u
#define IMU_FRESH_US 100000u
#define SETTLE_SAMPLES 400u   /* ~2 s at 200 Hz */
#define BRAKE_HOLD 2.5f       /* m/s^2 deceleration: fork dive distorts pitch */

void ride_init(ride_t *r, const att_mount_t *mount) {
    memset(r, 0, sizeof(*r));
    att_init(&r->att, mount);
    trick_cfg_default(&r->tcfg);
    trick_reset(&r->det);
    challenge_book_reset(&r->book);
}

void ride_speed(ride_t *r, uint64_t now, float kmh) {
    if (!(kmh >= 0 && kmh < 120))
        return;
    r->speed_kmh = kmh;
    r->speed_at = now;
    r->have_speed = true;
    att_wheel_speed(&r->att, now, kmh / 3.6f);
}

void ride_cadence(ride_t *r, uint64_t now, float rpm) {
    if (!(rpm >= 0 && rpm < 250))
        return;
    r->cadence_rpm = rpm;
    r->cadence_at = now;
    r->have_cadence = true;
}

static bool fresh(bool have, uint64_t at, uint64_t now, uint64_t limit) {
    return have && now >= at && now - at < limit;
}

bool ride_imu_sample(ride_t *r, uint64_t now, const att_raw_t *raw, trick_event_t *ev,
                     challenge_result_t *res) {
    bool speed_fresh = fresh(r->have_speed, r->speed_at, now, SPEED_FRESH_US);
    att_hold_grade(&r->att, trick_baseline_frozen(&r->det) ||
                                (speed_fresh && r->att.a_long < -BRAKE_HOLD));
    att_update(&r->att, now, raw, speed_fresh);
    r->imu_at = now;
    trick_input_t in = {
        .pitch_rel_deg = (r->att.pitch - r->att.grade) * RAD2DEG,
        .yaw_rate_dps = r->att.yaw_rate * RAD2DEG,
        .force_g = r->att.specific_force,
        .force_raw_g = r->att.specific_force_raw,
        .speed_kmh = speed_fresh ? r->speed_kmh : 0,
        .speed_fresh = speed_fresh,
        .cadence_rpm =
            fresh(r->have_cadence, r->cadence_at, now, CADENCE_FRESH_US) ? r->cadence_rpm : 0,
    };
    trick_event_t e;
    bool done = r->att.samples > 20 && trick_update(&r->det, &r->tcfg, now, &in, &e);
    if (r->det.rebaseline) {
        r->att.grade = r->att.pitch;
        r->det.rebaseline = false;
    }
    if (!done)
        return false;
    if (ev)
        *ev = e;
    challenge_result_t cr = challenge_score(&r->book, &e);
    if (res)
        *res = cr;
    return true;
}

ride_output_t ride_output(const ride_t *r, uint64_t now) {
    bool imu_fresh = r->att.initialised && now >= r->imu_at && now - r->imu_at < IMU_FRESH_US;
    bool speed_fresh = fresh(r->have_speed, r->speed_at, now, SPEED_FRESH_US);
    ride_output_t o = {
        .grade_pct = 100.0f * tanf(r->att.grade),
        .grade_valid = imu_fresh && r->att.samples >= SETTLE_SAMPLES,
        .hold = trick_hold(&r->det, &r->tcfg, now) ||
                (speed_fresh && r->att.a_long < -BRAKE_HOLD) || r->att.specific_force < 0.6f,
        .pitch_deg = r->att.pitch * RAD2DEG,
        .roll_deg = r->att.roll * RAD2DEG,
        .pitch_rel_deg = (r->att.pitch - r->att.grade) * RAD2DEG,
        .trick_state = r->det.state,
    };
    if (!(o.grade_pct == o.grade_pct) || fabsf(o.grade_pct) > 60.0f)
        o.grade_valid = false;
    return o;
}
