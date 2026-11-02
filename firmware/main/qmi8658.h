#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "attitude.h"

/* QMI8658 6-axis IMU on the Waveshare ESP32-C6-Touch-LCD-1.47.
 * Configured for +-16 g, +-1024 dps, 224.2 Hz ODR with on-chip low-pass filters. */
typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t address;
} qmi8658_t;

/* Probes 0x6B then 0x6A, checks WHO_AM_I and configures. */
bool qmi8658_init(qmi8658_t *q, i2c_master_bus_handle_t bus);
/* Reads one accel+gyro sample in SI units (sensor frame). False on bus error or no new data. */
bool qmi8658_read(qmi8658_t *q, att_raw_t *out, bool *fresh);
