#pragma once
/* Terrain-following assist selection ("AUTO").
 *
 * Picks only among the three captured, validated profiles (1 Eco, 2 Trail, 3 Turbo).
 * It never selects Off, never invents support/peak values, and never changes level
 * while a hold condition is active (trick, air time, hard braking, IMU not trusted).
 * The firmware still routes the result through the same profile allowlist as manual
 * selection. Pure C, no hardware access. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float eco_max_pct;     /* below: Eco (incl. descents) */
    float turbo_min_pct;   /* above: Turbo */
    float hysteresis_pct;  /* band around each threshold */
    float descent_pct;     /* below (negative): Eco even with + bias */
    uint32_t up_dwell_ms;  /* grade must stay above a threshold this long */
    uint32_t down_dwell_ms;/* and below this long; longer to avoid hunting */
    uint32_t min_hold_ms;  /* minimum time between any two changes */
} terrain_cfg_t;

typedef struct {
    bool enabled;
    int bias;          /* -1, 0, +1 levels from the +/- buttons while in AUTO */
    unsigned base;     /* grade-only level 1..3, before bias */
    unsigned level;    /* last chosen level 1..3 */
    unsigned pending;  /* candidate level waiting for dwell */
    uint64_t pending_since, changed_at;
} terrain_assist_t;

void terrain_cfg_default(terrain_cfg_t *c);
/* Enter AUTO, starting from the currently selected manual level (1..3). */
void terrain_assist_enable(terrain_assist_t *t, uint64_t now_us, unsigned current_level);
void terrain_assist_disable(terrain_assist_t *t);
/* +1 / -1 from the remote. Clamped to -1..+1 and applied immediately. */
void terrain_assist_bias(terrain_assist_t *t, uint64_t now_us, int step);
/* Grade-only level with hysteresis around from_base, no dwell and no bias. */
unsigned terrain_base_for_grade(const terrain_cfg_t *c, float grade_pct, unsigned from_base);
/* Applies bias and the descent rule to a base level. */
unsigned terrain_apply_bias(const terrain_cfg_t *c, unsigned base, int bias, float grade_pct);
/* Returns the level to request (1..3). hold: keep the current level. */
unsigned terrain_assist_update(terrain_assist_t *t, const terrain_cfg_t *c, uint64_t now_us,
                               float grade_pct, bool grade_valid, bool hold);
