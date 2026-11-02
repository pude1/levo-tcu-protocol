#include "tricks.h"
#include <math.h>
#include <string.h>

#define MS(x) ((uint64_t)(x) * 1000u)

void trick_cfg_default(trick_cfg_t *c) {
    *c = (trick_cfg_t){.lift_on_deg = 10.0f,
                       .lift_off_deg = 4.0f,
                       .lift_confirm_ms = 400,
                       .lift_min_ms = 1000,
                       .lift_min_speed_kmh = 3.0f,
                       .steady_range_deg = 1.5f,
                       .steady_window_ms = 2500,
                       .endo_on_deg = -8.0f,
                       .endo_off_deg = -3.0f,
                       .endo_confirm_ms = 200,
                       .endo_min_ms = 300,
                       .endo_max_speed_kmh = 12.0f,
                       .air_g = 0.45f,
                       .land_g = 1.5f,
                       .air_confirm_ms = 40,
                       .air_min_ms = 120,
                       .air_max_ms = 3000,
                       .land_settle_ms = 250,
                       .hop_max_ms = 650,
                       .hop_lift_window_ms = 800,
                       .hop_lift_deg = 6.0f,
                       .hop_max_speed_kmh = 25.0f,
                       .pedal_rpm = 20.0f,
                       .hold_after_ms = 1500};
}

void trick_reset(trick_detector_t *d) {
    memset(d, 0, sizeof(*d));
}

const char *trick_name(trick_type_t t) {
    static const char *const names[TRICK_COUNT] = {"NONE",  "WHEELIE", "MANUAL",
                                                   "ENDO",  "BUNNY HOP", "JUMP"};
    return (unsigned)t < TRICK_COUNT ? names[t] : "?";
}

bool trick_hold(const trick_detector_t *d, const trick_cfg_t *c, uint64_t now) {
    if (d->state != TS_RIDE)
        return true;
    return d->last_event_at && now >= d->last_event_at &&
           now - d->last_event_at < MS(c->hold_after_ms);
}

bool trick_baseline_frozen(const trick_detector_t *d) {
    return d->state != TS_RIDE;
}

static void open_state(trick_detector_t *d, trick_state_t s, uint64_t now, float angle) {
    d->state = s;
    d->start = now;
    d->confirmed = false;
    d->below_since = 0;
    d->max_angle = angle;
    d->distance = 0;
    d->pedal_us = d->total_us = 0;
    d->win_min = d->win_max = angle;
    d->win_start = now;
}

static bool emit(trick_detector_t *d, uint64_t now, trick_event_t *out, trick_event_t e) {
    d->state = TS_RIDE;
    d->last_event_at = now;
    e.at_us = now;
    *out = e;
    return true;
}

/* Ramp rejection: a pitch that stays this steady is terrain, not balance. */
static bool steady_terrain(trick_detector_t *d, const trick_cfg_t *c, uint64_t now, float p) {
    if (p < d->win_min)
        d->win_min = p;
    if (p > d->win_max)
        d->win_max = p;
    if (now - d->win_start < MS(c->steady_window_ms))
        return false;
    bool steady = d->win_max - d->win_min < c->steady_range_deg;
    d->win_min = d->win_max = p;
    d->win_start = now;
    return steady;
}

static void cancel_as_terrain(trick_detector_t *d, uint64_t now) {
    d->state = TS_RIDE;
    d->rebaseline = true;
    d->suppress_until = now + MS(1000);
}

bool trick_update(trick_detector_t *d, const trick_cfg_t *c, uint64_t now,
                  const trick_input_t *in, trick_event_t *out) {
    uint64_t dt = d->last && now > d->last ? now - d->last : 0;
    if (dt > MS(100))
        dt = MS(100);
    d->last = now;
    float p = in->pitch_rel_deg;
    if (!(p == p))
        return false;

    if (p >= c->hop_lift_deg) {
        d->last_lift_at = now;
        d->last_lift_angle = p;
    }

    /* Leaving the ground has priority over lift/endo tracking. */
    if (d->state != TS_AIR && d->state != TS_LAND) {
        if (in->force_g < c->air_g) {
            if (!d->air_seen_since)
                d->air_seen_since = now;
            if (now - d->air_seen_since >= MS(c->air_confirm_ms)) {
                bool lifted = d->last_lift_at &&
                              (d->last_lift_at >= d->air_seen_since ||
                               d->air_seen_since - d->last_lift_at <= MS(c->hop_lift_window_ms));
                open_state(d, TS_AIR, d->air_seen_since, 0);
                d->lift_before_air = lifted;
                d->rotation = 0;
                d->landing_g = 0;
                d->air_seen_since = 0;
            }
            return false;
        }
        d->air_seen_since = 0;
    }

    switch (d->state) {
    case TS_RIDE:
        if (now < d->suppress_until)
            break;
        if (p >= c->lift_on_deg && in->speed_fresh && in->speed_kmh >= c->lift_min_speed_kmh)
            open_state(d, TS_LIFT, now, p);
        else if (p <= c->endo_on_deg &&
                 (!in->speed_fresh || in->speed_kmh <= c->endo_max_speed_kmh))
            open_state(d, TS_ENDO, now, p);
        break;

    case TS_LIFT:
        d->total_us += dt;
        if (in->cadence_rpm >= c->pedal_rpm)
            d->pedal_us += dt;
        if (in->speed_fresh)
            d->distance += in->speed_kmh / 3.6f * (float)dt * 1e-6f;
        if (p > d->max_angle)
            d->max_angle = p;
        if (!d->confirmed && now - d->start >= MS(c->lift_confirm_ms))
            d->confirmed = true;
        if (steady_terrain(d, c, now, p)) {
            cancel_as_terrain(d, now);
            break;
        }
        if (p < c->lift_off_deg) {
            if (!d->below_since)
                d->below_since = now;
            if (now - d->below_since >= MS(150)) {
                uint64_t held = d->below_since - d->start;
                d->state = TS_RIDE;
                if (d->confirmed && held >= MS(c->lift_min_ms)) {
                    trick_event_t e = {.type = d->pedal_us * 2 >= d->total_us ? TRICK_WHEELIE
                                                                               : TRICK_MANUAL,
                                       .duration_ms = (uint32_t)(held / 1000),
                                       .distance_m = d->distance,
                                       .max_angle_deg = d->max_angle};
                    return emit(d, now, out, e);
                }
            }
        } else {
            d->below_since = 0;
        }
        break;

    case TS_ENDO:
        if (p < d->max_angle)
            d->max_angle = p;
        if (!d->confirmed && now - d->start >= MS(c->endo_confirm_ms))
            d->confirmed = true;
        if (steady_terrain(d, c, now, p)) {
            cancel_as_terrain(d, now);
            break;
        }
        if (p > c->endo_off_deg) {
            if (!d->below_since)
                d->below_since = now;
            if (now - d->below_since >= MS(100)) {
                uint64_t held = d->below_since - d->start;
                d->state = TS_RIDE;
                if (d->confirmed && held >= MS(c->endo_min_ms)) {
                    trick_event_t e = {.type = TRICK_ENDO,
                                       .duration_ms = (uint32_t)(held / 1000),
                                       .max_angle_deg = -d->max_angle};
                    return emit(d, now, out, e);
                }
            }
        } else {
            d->below_since = 0;
        }
        break;

    case TS_AIR:
        d->rotation += in->yaw_rate_dps * (float)dt * 1e-6f;
        if (fabsf(p) > d->max_angle)
            d->max_angle = fabsf(p);
        if (now - d->start > MS(c->air_max_ms)) {
            /* Not plausible air time (sensor fault, bike carried): drop it. */
            cancel_as_terrain(d, now);
            break;
        }
        if (in->force_raw_g >= c->land_g || in->force_g >= 0.85f) {
            d->land_at = now;
            d->landing_g = in->force_raw_g;
            d->state = TS_LAND;
        }
        break;

    case TS_LAND:
        if (in->force_raw_g > d->landing_g)
            d->landing_g = in->force_raw_g;
        if (now - d->land_at >= MS(c->land_settle_ms)) {
            uint64_t air = d->land_at - d->start;
            d->state = TS_RIDE;
            if (air < MS(c->air_min_ms))
                break;
            float t = (float)air * 1e-6f;
            bool hop = air <= MS(c->hop_max_ms) && d->lift_before_air &&
                       (!in->speed_fresh || in->speed_kmh <= c->hop_max_speed_kmh);
            trick_event_t e = {.type = hop ? TRICK_HOP : TRICK_JUMP,
                               .duration_ms = (uint32_t)(air / 1000),
                               .height_cm = 9.80665f * t * t / 8.0f * 100.0f,
                               .rotation_deg = d->rotation,
                               .landing_g = d->landing_g,
                               .max_angle_deg = d->max_angle};
            return emit(d, now, out, e);
        }
        break;
    }
    return false;
}
