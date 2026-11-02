#include "terrain_assist.h"

void terrain_cfg_default(terrain_cfg_t *c) {
    *c = (terrain_cfg_t){.eco_max_pct = 4.0f,
                         .turbo_min_pct = 11.0f,
                         .hysteresis_pct = 1.5f,
                         .descent_pct = -4.0f,
                         .up_dwell_ms = 800,
                         .down_dwell_ms = 3000,
                         .min_hold_ms = 1500};
}

static unsigned clamp_level(int l) {
    return l < 1 ? 1 : l > 3 ? 3 : (unsigned)l;
}

void terrain_assist_enable(terrain_assist_t *t, uint64_t now, unsigned current) {
    unsigned l = clamp_level((int)current);
    *t = (terrain_assist_t){.enabled = true, .base = l, .level = l, .pending = l,
                            .pending_since = now, .changed_at = now};
}

void terrain_assist_disable(terrain_assist_t *t) {
    t->enabled = false;
    t->bias = 0;
}

void terrain_assist_bias(terrain_assist_t *t, uint64_t now, int step) {
    int b = t->bias + (step > 0 ? 1 : step < 0 ? -1 : 0);
    t->bias = b < -1 ? -1 : b > 1 ? 1 : b;
    /* A button press is a deliberate rider request: apply it at once. */
    t->level = t->pending = clamp_level((int)t->base + t->bias);
    t->changed_at = t->pending_since = now;
}

unsigned terrain_base_for_grade(const terrain_cfg_t *c, float g, unsigned from) {
    float h = c->hysteresis_pct * 0.5f;
    from = clamp_level((int)from);
    /* Thresholds move away from the current level: stay unless clearly past. */
    float lo = c->eco_max_pct + (from >= 2 ? -h : h);
    float hi = c->turbo_min_pct + (from >= 3 ? -h : h);
    if (!(g == g)) /* NaN */
        return from;
    return g >= hi ? 3 : g >= lo ? 2 : 1;
}

unsigned terrain_apply_bias(const terrain_cfg_t *c, unsigned base, int bias, float g) {
    if (g < c->descent_pct)
        return 1;
    return clamp_level((int)base + bias);
}

unsigned terrain_assist_update(terrain_assist_t *t, const terrain_cfg_t *c, uint64_t now,
                               float g, bool valid, bool hold) {
    if (!t->enabled)
        return t->level;
    if (now < t->pending_since || now < t->changed_at) { /* clock went backwards */
        t->pending_since = t->changed_at = now;
        return t->level;
    }
    if (!valid || hold) {
        t->pending = t->level;
        t->pending_since = now;
        return t->level;
    }
    unsigned base = terrain_base_for_grade(c, g, t->base);
    unsigned want = terrain_apply_bias(c, base, t->bias, g);
    if (want == t->level) {
        t->base = base;
        t->pending = want;
        t->pending_since = now;
        return t->level;
    }
    if (want != t->pending) {
        t->pending = want;
        t->pending_since = now;
        return t->level;
    }
    uint64_t dwell = (uint64_t)(want > t->level ? c->up_dwell_ms : c->down_dwell_ms) * 1000u;
    if (now - t->pending_since >= dwell && now - t->changed_at >= (uint64_t)c->min_hold_ms * 1000u) {
        t->base = base;
        t->level = want;
        t->changed_at = now;
    }
    return t->level;
}
