#pragma once
/* Ride pipeline: attitude -> terrain grade -> trick detection -> challenges.
 * Shared by the firmware IMU task and the host replay tool. Pure C. */
#include <stdbool.h>
#include <stdint.h>
#include "attitude.h"
#include "challenges.h"
#include "tricks.h"

typedef struct {
    attitude_t att;
    trick_detector_t det;
    trick_cfg_t tcfg;
    challenge_book_t book;
    float speed_kmh, cadence_rpm;
    uint64_t speed_at, cadence_at, imu_at;
    bool have_speed, have_cadence;
} ride_t;

typedef struct {
    float grade_pct;   /* terrain grade, + uphill */
    bool grade_valid;  /* IMU fresh, filter settled */
    bool hold;         /* trick, air time, hard braking: do not change AUTO level */
    float pitch_deg, roll_deg, pitch_rel_deg;
    trick_state_t trick_state;
} ride_output_t;

void ride_init(ride_t *r, const att_mount_t *mount);
void ride_speed(ride_t *r, uint64_t now_us, float speed_kmh);
void ride_cadence(ride_t *r, uint64_t now_us, float cadence_rpm);
/* Returns true when a trick completed; *ev and *res are then filled. */
bool ride_imu_sample(ride_t *r, uint64_t now_us, const att_raw_t *raw, trick_event_t *ev,
                     challenge_result_t *res);
ride_output_t ride_output(const ride_t *r, uint64_t now_us);
