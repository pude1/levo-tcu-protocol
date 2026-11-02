#include "attitude.h"
#include <math.h>
#include <string.h>

/* Tuning. Time constants in seconds. */
#define TAU_PITCH 1.5f   /* accel correction of pitch */
#define TAU_ROLL 2.0f    /* turn-model correction of roll */
#define TAU_GRADE 2.0f   /* terrain grade low-pass */
#define TAU_FORCE 0.02f  /* specific-force low-pass (vibration) */
#define TAU_ALONG 0.25f  /* wheel-speed derivative low-pass */
#define A_LONG_MAX 8.0f  /* m/s^2 clamp; endos make the rear-wheel derivative meaningless */
#define STILL_SECONDS 1.0f

static void hint_vector(unsigned hint, float v[3]) {
    v[0] = v[1] = v[2] = 0;
    v[hint < 2 ? 0 : 1] = (hint & 1) ? -1.0f : 1.0f;
}
static void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
static float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static float norm(const float a[3]) {
    return sqrtf(dot(a, a));
}
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void att_mount_default(att_mount_t *m, unsigned forward_hint, unsigned up_axis) {
    float f[3], u[3] = {0, 0, up_axis ? -1.0f : 1.0f}, l[3];
    hint_vector(forward_hint & 3, f);
    cross(u, f, l);
    memcpy(m->r[0], f, sizeof(f));
    memcpy(m->r[1], l, sizeof(l));
    memcpy(m->r[2], u, sizeof(u));
}

bool att_mount_from_gravity(att_mount_t *m, const float gravity[3], unsigned forward_hint) {
    float n = norm(gravity);
    if (!(n > 0.5f * ATT_G && n < 1.5f * ATT_G))
        return false;
    float u[3] = {gravity[0] / n, gravity[1] / n, gravity[2] / n}, h[3], f[3], l[3];
    hint_vector(forward_hint & 3, h);
    float d = dot(h, u);
    for (int i = 0; i < 3; i++)
        f[i] = h[i] - d * u[i];
    float fn = norm(f);
    if (fn < 0.5f) /* forward hint within 30 degrees of vertical: wrong hint */
        return false;
    for (int i = 0; i < 3; i++)
        f[i] /= fn;
    cross(u, f, l);
    memcpy(m->r[0], f, sizeof(f));
    memcpy(m->r[1], l, sizeof(l));
    memcpy(m->r[2], u, sizeof(u));
    return true;
}

void att_init(attitude_t *a, const att_mount_t *m) {
    memset(a, 0, sizeof(*a));
    a->mount = *m;
    a->specific_force = 1.0f;
}

void att_wheel_speed(attitude_t *a, uint64_t now, float v) {
    if (!(v >= 0) || v > 40.0f)
        return;
    if (!a->v_valid || now <= a->v_at || now - a->v_at > 500000) {
        a->a_long = 0;
    } else {
        float dt = (float)(now - a->v_at) * 1e-6f;
        float raw = (v - a->v_prev) / dt;
        a->a_long += dt / (TAU_ALONG + dt) * (raw - a->a_long);
        a->a_long = clampf(a->a_long, -A_LONG_MAX, A_LONG_MAX);
    }
    a->v_prev = v;
    a->v_filt = v;
    a->v_at = now;
    a->v_valid = true;
}

void att_hold_grade(attitude_t *a, bool hold) {
    a->grade_hold = hold;
}

bool att_is_still(const attitude_t *a, uint64_t now) {
    return a->still_n > 0 && now >= a->still_from && now - a->still_from >= 500000;
}

bool att_still_gravity(const attitude_t *a, uint64_t now, float out[3]) {
    if (!att_is_still(a, now) || a->still_n < 20)
        return false;
    for (int i = 0; i < 3; i++)
        out[i] = a->still_acc[i] / (float)a->still_n;
    return true;
}

static void rotate(const att_mount_t *m, const float in[3], float out[3]) {
    for (int i = 0; i < 3; i++)
        out[i] = m->r[i][0] * in[0] + m->r[i][1] * in[1] + m->r[i][2] * in[2];
}

void att_update(attitude_t *a, uint64_t now, const att_raw_t *raw, bool speed_fresh) {
    float as[3] = {raw->ax, raw->ay, raw->az};
    float gs[3] = {raw->gx - a->bias[0], raw->gy - a->bias[1], raw->gz - a->bias[2]};
    float f[3], w[3];
    rotate(&a->mount, as, f);
    rotate(&a->mount, gs, w);
    float fn = norm(f) / ATT_G;
    a->specific_force_raw = fn;
    a->samples++;
    if (!a->initialised || now <= a->last_us || now - a->last_us > 200000) {
        /* (Re)start from the accelerometer alone. */
        a->pitch = atan2f(f[0], sqrtf(f[1] * f[1] + f[2] * f[2]));
        a->roll = atan2f(f[1], f[2]);
        if (!a->initialised)
            a->grade = a->pitch;
        a->fx_bike = a->fx_matched = f[0];
        a->fz_bike = f[2];
        a->specific_force = fn;
        a->last_us = now;
        a->initialised = true;
        return;
    }
    float dt = (float)(now - a->last_us) * 1e-6f;
    a->last_us = now;

    /* FLU body rates -> FRD (aerospace) for the standard ZYX Euler kinematics. */
    float p = w[0], q = -w[1], r = -w[2];
    float sr = sinf(a->roll), cr = cosf(a->roll);
    float cp = cosf(a->pitch);
    if (fabsf(cp) < 0.17f) /* ~80 deg: keep kinematics finite (looping out) */
        cp = cp < 0 ? -0.17f : 0.17f;
    float tp = sinf(a->pitch) / cp;
    float roll_dot = p + (q * sr + r * cr) * tp;
    float pitch_dot = q * cr - r * sr;
    float yaw_dot = (q * sr + r * cr) / cp;
    a->roll += roll_dot * dt;
    a->pitch += pitch_dot * dt;
    a->pitch_rate = pitch_dot;
    a->yaw_rate = yaw_dot;

    float k = dt / (TAU_FORCE + dt);
    a->fx_bike += k * (f[0] - a->fx_bike);
    a->fz_bike += k * (f[2] - a->fz_bike);
    a->specific_force += k * (fn - a->specific_force);
    /* Delay fx exactly like the wheel derivative so both cancel during accelerations. */
    a->fx_matched += dt / (TAU_ALONG + 0.025f + dt) * (f[0] - a->fx_matched);

    if (!speed_fresh)
        a->a_long *= 1.0f - clampf(dt / 0.3f, 0, 1);

    /* Accelerometer trust: full near 1 g, none in free fall or on impacts. */
    float dev = fabsf(a->specific_force - 1.0f);
    float trust = dev < 0.15f ? 1.0f : dev > 0.4f ? 0.0f : (0.4f - dev) / 0.25f;
    if (!speed_fresh)
        trust *= 0.3f; /* forward acceleration unknown */
    if (a->grade_hold)
        trust *= 0.2f; /* trick in progress: lean on the gyro */
    if (fabsf(a->a_long) > 4.0f)
        trust *= 0.3f;

    float s = clampf((a->fx_matched - a->a_long) / ATT_G, -1.0f, 1.0f);
    float pitch_acc = asinf(s);
    a->pitch += clampf(dt / TAU_PITCH, 0, 1) * trust * (pitch_acc - a->pitch);

    float v = speed_fresh ? a->v_filt : 0;
    float roll_ref = v > 2.0f ? atanf(v * yaw_dot / ATT_G) : atan2f(f[1], f[2]);
    a->roll += clampf(dt / TAU_ROLL, 0, 1) * trust * (roll_ref - a->roll);

    if (!a->grade_hold)
        a->grade += clampf(dt / TAU_GRADE, 0, 1) * (a->pitch - a->grade);

    /* Gyro bias while standing still on the CAN-reported zero speed. */
    float wn = norm(gs);
    bool still = speed_fresh && a->v_filt < 0.05f && fabsf(fn - 1.0f) < 0.03f && wn < 0.08f;
    if (!still) {
        a->still_n = 0;
        return;
    }
    if (a->still_n == 0) {
        a->still_since = a->still_from = now;
        memset(a->still_sum, 0, sizeof(a->still_sum));
        memset(a->still_acc, 0, sizeof(a->still_acc));
    }
    for (int i = 0; i < 3; i++) {
        float g_raw = i == 0 ? raw->gx : i == 1 ? raw->gy : raw->gz;
        a->still_sum[i] += g_raw;
        a->still_acc[i] += as[i];
    }
    a->still_n++;
    if ((float)(now - a->still_since) * 1e-6f >= STILL_SECONDS) {
        for (int i = 0; i < 3; i++)
            a->bias[i] = a->still_sum[i] / (float)a->still_n;
        a->still_since = now;
        a->still_n = 1;
        for (int i = 0; i < 3; i++) {
            a->still_sum[i] = a->bias[i];
            a->still_acc[i] = as[i];
        }
    }
}
