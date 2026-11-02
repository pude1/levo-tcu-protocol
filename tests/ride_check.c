/* Synthetic rides through the full ride pipeline: grade with acceleration correction,
 * ramp rejection, wheelie, manual, endo, bunny hop and a 180 jump. No hardware. */
#include "ride.h"
#include "terrain_assist.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DT_US 5000u /* 200 Hz */
#define D2R 0.017453293f

typedef struct {
    uint64_t t;
    float pitch;     /* rad */
    float v;         /* m/s */
    float a_long;    /* m/s^2 */
    float yaw_rate;  /* rad/s, + right */
    float cadence;   /* rpm */
    bool air;        /* free fall */
    float impact_g;  /* extra vertical specific force (landing) */
    uint32_t rng;
    ride_t ride;
    trick_event_t events[16];
    challenge_result_t results[16];
    unsigned n_events;
    uint64_t next_can;
} sim_t;

static float noise(sim_t *s, float amp) {
    s->rng = s->rng * 1664525u + 1013904223u;
    return amp * ((float)(s->rng >> 8) / 8388608.0f - 1.0f);
}

static void sim_init(sim_t *s) {
    memset(s, 0, sizeof(*s));
    s->rng = 12345;
    att_mount_t m;
    att_mount_default(&m, 0, 0);
    ride_init(&s->ride, &m);
    s->t = 1000000;
}

/* Advance dt with a target pitch; derive gyro from the pitch change. */
static void step(sim_t *s, float target_pitch) {
    float prev = s->pitch;
    s->pitch = target_pitch;
    float dt = DT_US * 1e-6f;
    float pitch_rate = (s->pitch - prev) / dt;
    s->v += s->a_long * dt;
    if (s->v < 0)
        s->v = 0;
    s->t += DT_US;
    if (s->t >= s->next_can) { /* 0x201 / 0x203 at 20 Hz */
        ride_speed(&s->ride, s->t, s->v * 3.6f);
        ride_cadence(&s->ride, s->t, s->cadence);
        s->next_can = s->t + 50000;
    }
    att_raw_t r;
    if (s->air) {
        r.ax = noise(s, 0.2f);
        r.ay = noise(s, 0.2f);
        r.az = noise(s, 0.2f);
    } else {
        float vib = noise(s, 1.5f); /* trail vibration, mostly vertical */
        r.ax = s->a_long + ATT_G * sinf(s->pitch) + noise(s, 0.4f);
        r.ay = noise(s, 0.4f); /* coordinated turn: no net lateral force */
        r.az = ATT_G * cosf(s->pitch) + vib + s->impact_g * ATT_G;
    }
    /* FLU: nose-up rate is a negative rotation about +y. Add a constant gyro bias. */
    r.gx = noise(s, 0.01f) + 0.004f;
    r.gy = -pitch_rate + noise(s, 0.01f) - 0.006f;
    r.gz = -s->yaw_rate + noise(s, 0.01f) + 0.003f; /* FLU: right turn = negative about +z */
    trick_event_t e;
    challenge_result_t cr;
    if (ride_imu_sample(&s->ride, s->t, &r, &e, &cr) && s->n_events < 16) {
        s->events[s->n_events] = e;
        s->results[s->n_events++] = cr;
    }
}

static void hold_for(sim_t *s, float pitch, float seconds) {
    for (float t = 0; t < seconds; t += DT_US * 1e-6f)
        step(s, pitch);
}
static void ramp_to(sim_t *s, float pitch, float seconds) {
    float from = s->pitch;
    unsigned n = (unsigned)(seconds / (DT_US * 1e-6f));
    for (unsigned i = 1; i <= n; i++)
        step(s, from + (pitch - from) * (float)i / (float)n);
}
static float grade_pct(sim_t *s) {
    return ride_output(&s->ride, s->t).grade_pct;
}
static float deg_pct(float pct) {
    return atanf(pct / 100.0f);
}

static void test_grade_and_auto(void) {
    sim_t s;
    sim_init(&s);
    terrain_cfg_t cfg;
    terrain_cfg_default(&cfg);
    terrain_assist_t ta;
    terrain_assist_enable(&ta, s.t, 1);
    s.v = 4.0f;
    s.cadence = 70;
    hold_for(&s, 0, 5);
    assert(fabsf(grade_pct(&s)) < 1.0f);
    /* Hard acceleration on the flat must not read as a climb. */
    s.a_long = 2.0f;
    hold_for(&s, 0, 3);
    s.a_long = 0;
    assert(fabsf(grade_pct(&s)) < 1.5f);
    /* Without the wheel-speed correction this would read as about 20 %. */
    s.v = 3.0f;
    ramp_to(&s, deg_pct(8), 3);
    unsigned level = 1;
    for (int i = 0; i < 2000; i++) { /* 10 s on 8 % */
        step(&s, deg_pct(8));
        ride_output_t o = ride_output(&s.ride, s.t);
        level = terrain_assist_update(&ta, &cfg, s.t, o.grade_pct, o.grade_valid, o.hold);
    }
    assert(fabsf(grade_pct(&s) - 8.0f) < 1.5f);
    assert(level == 2);
    ramp_to(&s, deg_pct(16), 3);
    for (int i = 0; i < 2000; i++) {
        step(&s, deg_pct(16));
        ride_output_t o = ride_output(&s.ride, s.t);
        level = terrain_assist_update(&ta, &cfg, s.t, o.grade_pct, o.grade_valid, o.hold);
    }
    assert(fabsf(grade_pct(&s) - 16.0f) < 2.0f);
    assert(level == 3);
    s.v = 7.0f;
    s.cadence = 0;
    ramp_to(&s, deg_pct(-12), 3);
    for (int i = 0; i < 2000; i++) {
        step(&s, deg_pct(-12));
        ride_output_t o = ride_output(&s.ride, s.t);
        level = terrain_assist_update(&ta, &cfg, s.t, o.grade_pct, o.grade_valid, o.hold);
    }
    assert(fabsf(grade_pct(&s) + 12.0f) < 2.0f);
    assert(level == 1);
    assert(s.n_events == 0); /* climbs and descents are not tricks */
    printf("grade 8/16/-12 %% and AUTO Eco->Trail->Turbo->Eco ok; ");
}

static void test_ramp_rejection(void) {
    sim_t s;
    sim_init(&s);
    s.v = 3.0f;
    s.cadence = 60;
    hold_for(&s, 0, 4);
    ramp_to(&s, deg_pct(30), 1.0f); /* sudden steep pitch, then steady */
    hold_for(&s, deg_pct(30), 12);
    assert(s.n_events == 0);
    assert(fabsf(grade_pct(&s) - 30.0f) < 4.0f);
    printf("steady steep ramp is terrain; ");
}

static void wheelie(sim_t *s, float peak_deg, float seconds) {
    ramp_to(s, peak_deg * D2R, 0.5f);
    for (float t = 0; t < seconds; t += DT_US * 1e-6f)
        step(s, (peak_deg + 4.0f * sinf(6.283f * 1.2f * t)) * D2R); /* balance */
    ramp_to(s, 0, 0.5f);
    hold_for(s, 0, 2);
}

static void test_tricks(void) {
    sim_t s;
    sim_init(&s);
    s.v = 3.5f;
    s.cadence = 70;
    hold_for(&s, 0, 4);

    wheelie(&s, 25, 4.0f);
    assert(s.n_events == 1 && s.events[0].type == TRICK_WHEELIE);
    assert(s.events[0].duration_ms > 4000 && s.events[0].duration_ms < 5500);
    assert(s.events[0].max_angle_deg > 24 && s.events[0].max_angle_deg < 32);
    assert(s.events[0].distance_m > 13 && s.events[0].distance_m < 20);
    assert(s.results[0].medal == MEDAL_BRONZE && s.results[0].personal_best);

    s.cadence = 0;
    wheelie(&s, 15, 2.5f);
    assert(s.n_events == 2 && s.events[1].type == TRICK_MANUAL);
    assert(s.results[1].medal == MEDAL_SILVER || s.results[1].medal == MEDAL_BRONZE);

    s.v = 2.0f;
    ramp_to(&s, -15 * D2R, 0.3f);
    for (float t = 0; t < 1.2f; t += DT_US * 1e-6f)
        step(&s, (-15.0f + 2.0f * sinf(6.283f * 2.0f * t)) * D2R);
    ramp_to(&s, 0, 0.3f);
    hold_for(&s, 0, 2);
    assert(s.n_events == 3 && s.events[2].type == TRICK_ENDO);
    assert(s.results[2].medal == MEDAL_SILVER);

    /* Bunny hop: front lift, 0.42 s air time, landing impact. */
    s.v = 4.0f;
    ramp_to(&s, 12 * D2R, 0.25f);
    ramp_to(&s, 2 * D2R, 0.1f);
    s.air = true;
    hold_for(&s, 0, 0.42f);
    s.air = false;
    s.impact_g = 1.5f;
    hold_for(&s, 0, 0.05f);
    s.impact_g = 0;
    hold_for(&s, 0, 2);
    assert(s.n_events == 4 && s.events[3].type == TRICK_HOP);
    assert(s.events[3].height_cm > 18 && s.events[3].height_cm < 26);
    assert(s.events[3].landing_g > 2.0f);

    /* Jump at speed with a 180 in the air. */
    s.v = 7.0f;
    hold_for(&s, 0, 2);
    s.air = true;
    s.yaw_rate = 3.4907f; /* 200 deg/s for 0.9 s */
    hold_for(&s, 0, 0.9f);
    s.yaw_rate = 0;
    s.air = false;
    s.impact_g = 2.0f;
    hold_for(&s, 0, 0.05f);
    s.impact_g = 0;
    hold_for(&s, 0, 2);
    assert(s.n_events == 5 && s.events[4].type == TRICK_JUMP);
    assert(s.events[4].duration_ms > 850 && s.events[4].duration_ms < 1000);
    assert(fabsf(s.events[4].rotation_deg) > 150);
    assert(s.results[4].spin180 && s.results[4].medal == MEDAL_SILVER);
    printf("wheelie/manual/endo/hop/180 jump ok (%u pts)", s.ride.book.points);
}

static void test_hold_freezes_auto(void) {
    terrain_cfg_t cfg;
    terrain_cfg_default(&cfg);
    terrain_assist_t t;
    terrain_assist_enable(&t, 0, 2);
    uint64_t now = 0;
    for (; now < 10000000; now += 5000)
        assert(terrain_assist_update(&t, &cfg, now, 20.0f, true, true) == 2);
    for (; now < 20000000; now += 5000)
        assert(terrain_assist_update(&t, &cfg, now, 20.0f, false, false) == 2);
    unsigned l = 2;
    for (; now < 21000000; now += 5000)
        l = terrain_assist_update(&t, &cfg, now, 20.0f, true, false);
    assert(l == 3);
    /* Rollers around the Eco/Trail threshold must not hunt. */
    terrain_assist_enable(&t, now, 1);
    unsigned changes = 0, prev = 1;
    for (uint64_t end = now + 60000000; now < end; now += 5000) {
        float g = 4.0f + 1.2f * sinf((float)now * 1e-6f * 6.283f / 4.0f);
        l = terrain_assist_update(&t, &cfg, now, g, true, false);
        changes += l != prev;
        prev = l;
    }
    assert(changes <= 1); /* may step up once, then must stay */
    /* Bias acts immediately and is clamped; descents force Eco. */
    terrain_assist_enable(&t, now, 2);
    terrain_assist_bias(&t, now, +1);
    terrain_assist_bias(&t, now, +1);
    assert(t.level == 3 && t.bias == 1);
    for (uint64_t end = now + 5000000; now < end; now += 5000)
        l = terrain_assist_update(&t, &cfg, now, -8.0f, true, false);
    assert(l == 1);
    printf("; AUTO holds during tricks/invalid IMU, no hunting, bias");
}

int main(void) {
    test_grade_and_auto();
    test_ramp_rejection();
    test_tricks();
    test_hold_freezes_auto();
    puts(". PASS");
    return 0;
}
