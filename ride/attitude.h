#pragma once
/* Bike-specific attitude estimator for a frame-mounted 6-axis IMU.
 *
 * Portable C11, no hardware I/O. Frame convention after mounting rotation:
 *   x forward, y left, z up (FLU), gyro right-handed about those axes.
 * Reported angles: pitch > 0 nose up, roll > 0 leaning right, yaw rate > 0 turning right.
 *
 * Why not a generic AHRS: on a bike the accelerometer almost never measures
 * gravity alone. Forward acceleration is removed using the rear-wheel speed
 * derivative from CAN 0x201, and roll is corrected towards the coordinated-turn
 * lean angle atan(v * yaw_rate / g) instead of the (useless) lateral accel. */
#include <stdbool.h>
#include <stdint.h>

#define ATT_G 9.80665f

typedef struct {
    float ax, ay, az; /* m/s^2, sensor frame */
    float gx, gy, gz; /* rad/s, sensor frame */
} att_raw_t;

typedef struct {
    /* Rows are bike x/y/z expressed in sensor coordinates: bike = R * sensor. */
    float r[3][3];
} att_mount_t;

typedef struct {
    att_mount_t mount;
    float pitch, roll;          /* rad */
    float yaw_rate;             /* rad/s, world vertical, + = right turn */
    float pitch_rate;           /* rad/s, Euler pitch rate, + = nose rising */
    float grade;                /* rad, slow terrain estimate (frozen while held) */
    float a_long;               /* m/s^2 from wheel speed derivative */
    float specific_force;       /* |f| in g, low-pass filtered */
    float specific_force_raw;   /* |f| in g, unfiltered */
    float fx_bike, fz_bike;     /* m/s^2, bike frame, low-pass */
    float bias[3];              /* rad/s gyro bias, sensor frame */
    /* internal */
    float v_prev, v_filt, fx_matched;
    uint64_t v_at, last_us;
    bool v_valid, initialised, grade_hold;
    float still_sum[3], still_acc[3];
    uint32_t still_n;
    uint64_t still_since, still_from;
    uint32_t samples;
} attitude_t;

/* Mount from a configured forward hint (+X=0,-X=1,+Y=2,-Y=3 of the sensor) and up axis
 * (+Z=0, -Z=1). Used until a level calibration is available. */
void att_mount_default(att_mount_t *m, unsigned forward_hint, unsigned up_axis);
/* Builds a mount from a measured gravity vector (sensor frame, bike standing level)
 * and a forward hint. Removes any mounting tilt. Returns false if hint ~ parallel to g. */
bool att_mount_from_gravity(att_mount_t *m, const float gravity[3], unsigned forward_hint);

void att_init(attitude_t *a, const att_mount_t *m);
/* Wheel speed from CAN (m/s). Call whenever 0x201 is decoded. */
void att_wheel_speed(attitude_t *a, uint64_t now_us, float speed_ms);
/* One IMU sample. speed_fresh: a 0x201 frame was seen within the last 300 ms. */
void att_update(attitude_t *a, uint64_t now_us, const att_raw_t *raw, bool speed_fresh);
/* Freeze the terrain grade (during tricks, air time, hard braking). */
void att_hold_grade(attitude_t *a, bool hold);
/* Gyro bias learned while the bike stands still (speed 0, steady accel). */
bool att_is_still(const attitude_t *a, uint64_t now_us);
/* Mean accelerometer vector (sensor frame) over the current still window. */
bool att_still_gravity(const attitude_t *a, uint64_t now_us, float out[3]);
