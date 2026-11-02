#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ride.h"

typedef enum { RIDE_CAL_NONE, RIDE_CAL_RUNNING, RIDE_CAL_OK, RIDE_CAL_FAILED } ride_cal_t;

typedef struct {
    bool imu_ok;          /* sensor present and delivering samples */
    bool calibrated;      /* level mount stored (NVS) or measured this ride */
    ride_output_t out;
    bool has_event;
    trick_event_t event;
    challenge_result_t result;
    uint64_t event_at;
    uint32_t points;
    ride_cal_t cal;
    uint64_t cal_at;
} ride_status_t;

/* Loads stored calibration/bests (read-only flash access) and starts the IMU task.
 * Never fatal: without an IMU the controller behaves exactly like the reference. */
void ride_task_start(void);
/* Feed decoded CAN telemetry from the control loop. Cheap, non-blocking. */
void ride_task_can(uint32_t id, const uint8_t *data, uint8_t len, uint64_t now_us);
ride_status_t ride_task_status(void);
/* Level-ground calibration: bike standing still on flat ground, rider off. */
void ride_task_request_calibration(void);
/* Writes calibration and bests to NVS. Call only after CAN traffic has stopped:
 * flash writes stall the single core and would break the 50 ms command schedule. */
void ride_task_persist(void);
