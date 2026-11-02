#pragma once
#include "driver/i2c_master.h"
/* The board's internal I2C bus (GPIO18 SDA / GPIO19 SCL) carries the AXS5106L touch
 * controller (0x63) and the QMI8658 IMU (0x6B). The IDF master driver serialises
 * transactions from several tasks on one bus. Created once, never deleted. */
i2c_master_bus_handle_t board_i2c_bus(void);
