#pragma once
/* Trick detection from frame attitude + CAN telemetry. Pure C, no hardware access.
 *
 * Detected: wheelie (nose up, pedalling), manual (nose up, not pedalling),
 * endo/stoppie (nose down, rear wheel up), bunny hop (short air time after a
 * front lift), jump (longer or faster air time), with air rotation (180/360).
 *
 * All angles are relative to the terrain baseline that is frozen while a trick
 * candidate is open, so a wheelie on a 10 % climb is measured from the climb.
 * Thresholds are starting points: tune them with tools/ride_replay on your own logs. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TRICK_NONE,
    TRICK_WHEELIE,
    TRICK_MANUAL,
    TRICK_ENDO,
    TRICK_HOP,
    TRICK_JUMP,
    TRICK_COUNT
} trick_type_t;

typedef struct {
    float lift_on_deg, lift_off_deg; /* nose-up entry / exit */
    uint32_t lift_confirm_ms, lift_min_ms;
    float lift_min_speed_kmh;
    float steady_range_deg;          /* ramp rejection: pitch this steady ... */
    uint32_t steady_window_ms;       /* ... for this long is terrain, not a trick */
    float endo_on_deg, endo_off_deg; /* negative, nose down */
    uint32_t endo_confirm_ms, endo_min_ms;
    float endo_max_speed_kmh;
    float air_g, land_g;             /* specific force thresholds */
    uint32_t air_confirm_ms, air_min_ms, air_max_ms, land_settle_ms;
    uint32_t hop_max_ms, hop_lift_window_ms;
    float hop_lift_deg, hop_max_speed_kmh;
    float pedal_rpm;
    uint32_t hold_after_ms;          /* keep terrain AUTO frozen after a trick */
} trick_cfg_t;

typedef struct {
    float pitch_rel_deg;   /* frame pitch minus frozen terrain baseline */
    float yaw_rate_dps;
    float force_g;         /* low-pass |f| */
    float force_raw_g;     /* unfiltered |f| */
    float speed_kmh;
    bool speed_fresh;
    float cadence_rpm;
} trick_input_t;

typedef struct {
    trick_type_t type;
    uint32_t duration_ms;  /* hold time (wheelie/manual/endo) or air time */
    float distance_m;      /* wheelie/manual, from rear-wheel speed */
    float max_angle_deg;   /* peak |pitch| relative to terrain */
    float height_cm;       /* hop/jump: g*t^2/8 estimate for a symmetric arc */
    float rotation_deg;    /* signed yaw rotation while airborne */
    float landing_g;
    uint64_t at_us;
} trick_event_t;

typedef enum { TS_RIDE, TS_LIFT, TS_ENDO, TS_AIR, TS_LAND } trick_state_t;

typedef struct {
    trick_state_t state;
    bool confirmed;
    uint64_t start, last, below_since, air_seen_since, land_at, last_event_at, last_lift_at;
    float max_angle, distance, rotation, landing_g, last_lift_angle;
    uint64_t pedal_us, total_us;
    float win_min, win_max;
    uint64_t win_start;
    bool lift_before_air;
    bool rebaseline;          /* set when a candidate was terrain: snap baseline, then clear */
    uint64_t suppress_until;  /* no new lift/endo candidate before this */
} trick_detector_t;

void trick_cfg_default(trick_cfg_t *c);
void trick_reset(trick_detector_t *d);
/* Feed one sample. Returns true and fills *out when a trick has completed. */
bool trick_update(trick_detector_t *d, const trick_cfg_t *c, uint64_t now_us,
                  const trick_input_t *in, trick_event_t *out);
/* True while a candidate/trick is open or shortly after one: freeze terrain AUTO. */
bool trick_hold(const trick_detector_t *d, const trick_cfg_t *c, uint64_t now_us);
/* True while the terrain baseline must stay frozen (candidate open). */
bool trick_baseline_frozen(const trick_detector_t *d);
const char *trick_name(trick_type_t t);
