#include "challenges.h"
#include <math.h>
#include <string.h>

/* Starting ladders for an e-MTB (heavy bike, long wheelbase). Edit freely. */
static const challenge_def_t defs[TRICK_COUNT] = {
    [TRICK_WHEELIE] = {TRICK_WHEELIE, "s", {2.0f, 5.0f, 10.0f}},
    [TRICK_MANUAL] = {TRICK_MANUAL, "s", {1.5f, 3.0f, 6.0f}},
    [TRICK_ENDO] = {TRICK_ENDO, "s", {0.5f, 1.0f, 2.0f}},
    [TRICK_HOP] = {TRICK_HOP, "cm", {10.0f, 20.0f, 30.0f}},
    [TRICK_JUMP] = {TRICK_JUMP, "s", {0.5f, 0.8f, 1.2f}},
};

const challenge_def_t *challenge_def(trick_type_t t) {
    return t > TRICK_NONE && t < TRICK_COUNT ? &defs[t] : NULL;
}

float challenge_metric(const trick_event_t *e) {
    return e->type == TRICK_HOP ? e->height_cm : (float)e->duration_ms / 1000.0f;
}

void challenge_book_reset(challenge_book_t *b) {
    memset(b, 0, sizeof(*b));
}

const char *medal_name(medal_t m) {
    static const char *const n[] = {"", "BRONZE", "SILVER", "GOLD"};
    return (unsigned)m < 4 ? n[m] : "";
}

challenge_result_t challenge_score(challenge_book_t *b, const trick_event_t *e) {
    challenge_result_t r = {.type = e->type};
    const challenge_def_t *d = challenge_def(e->type);
    if (!d)
        return r;
    r.value = challenge_metric(e);
    for (int i = 0; i < 3; i++)
        if (r.value >= d->tiers[i])
            r.medal = (medal_t)(i + 1);
    r.next_target = r.medal < MEDAL_GOLD ? d->tiers[r.medal] : 0;
    if (e->type == TRICK_HOP || e->type == TRICK_JUMP) {
        float rot = fabsf(e->rotation_deg);
        r.spin360 = rot >= 330.0f;
        r.spin180 = !r.spin360 && rot >= 150.0f;
    }
    r.personal_best = r.value > b->best[e->type];
    if (r.personal_best)
        b->best[e->type] = r.value;
    static const uint32_t medal_points[] = {0, 1, 3, 5};
    r.points = medal_points[r.medal] + (r.personal_best && r.medal ? 2 : 0) +
               (r.spin360 ? 10 : r.spin180 ? 5 : 0);
    if (b->count[e->type] < UINT16_MAX)
        b->count[e->type]++;
    if (b->medals[e->type][r.medal] < UINT16_MAX)
        b->medals[e->type][r.medal]++;
    b->points += r.points;
    return r;
}
