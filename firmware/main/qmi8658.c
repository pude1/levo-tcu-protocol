/* Register map from the QST QMI8658A datasheet (rev. A). Only the registers needed
 * for polled 6-axis data are used; no FIFO, no interrupts (INT pins left unused). */
#include "qmi8658.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REG_WHO_AM_I 0x00 /* reads 0x05 */
#define REG_CTRL1 0x02    /* serial interface: bit6 address auto-increment */
#define REG_CTRL2 0x03    /* accel full scale [6:4], ODR [3:0] */
#define REG_CTRL3 0x04    /* gyro full scale [6:4], ODR [3:0] */
#define REG_CTRL5 0x06    /* low-pass filters */
#define REG_CTRL7 0x08    /* sensor enable: bit0 accel, bit1 gyro */
#define REG_STATUS0 0x2E  /* bit0 accel data available, bit1 gyro */
#define REG_AX_L 0x35     /* AX..AZ, GX..GZ, little-endian int16 */
#define REG_RESET 0x60    /* write 0xB0: soft reset */

#define ACC_LSB_PER_G 2048.0f  /* +-16 g */
#define GYR_LSB_PER_DPS 32.0f  /* +-1024 dps */
#define TIMEOUT_MS 10

static bool wr(qmi8658_t *q, uint8_t reg, uint8_t v) {
    uint8_t b[2] = {reg, v};
    return i2c_master_transmit(q->dev, b, 2, TIMEOUT_MS) == ESP_OK;
}
static bool rd(qmi8658_t *q, uint8_t reg, uint8_t *out, size_t n) {
    return i2c_master_transmit_receive(q->dev, &reg, 1, out, n, TIMEOUT_MS) == ESP_OK;
}

bool qmi8658_init(qmi8658_t *q, i2c_master_bus_handle_t bus) {
    static const uint8_t addrs[] = {0x6B, 0x6A};
    for (unsigned i = 0; i < sizeof(addrs); i++) {
        if (i2c_master_probe(bus, addrs[i], TIMEOUT_MS) != ESP_OK)
            continue;
        i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = addrs[i],
                                   .scl_speed_hz = 400000};
        if (i2c_master_bus_add_device(bus, &cfg, &q->dev) != ESP_OK)
            continue;
        q->address = addrs[i];
        uint8_t who = 0;
        if (!rd(q, REG_WHO_AM_I, &who, 1) || who != 0x05) {
            i2c_master_bus_rm_device(q->dev);
            q->dev = NULL;
            continue;
        }
        bool ok = wr(q, REG_RESET, 0xB0);
        vTaskDelay(pdMS_TO_TICKS(20));
        ok = ok && wr(q, REG_CTRL1, 0x40);                 /* auto-increment, little-endian */
        ok = ok && wr(q, REG_CTRL2, (3 << 4) | 5);         /* +-16 g, 224.2 Hz */
        ok = ok && wr(q, REG_CTRL3, (6 << 4) | 5);         /* +-1024 dps, 224.2 Hz */
        ok = ok && wr(q, REG_CTRL5, (3 << 5) | (1 << 4) | (3 << 1) | 1); /* LPF 13.37 % ODR */
        ok = ok && wr(q, REG_CTRL7, 0x03);                 /* accel + gyro on */
        vTaskDelay(pdMS_TO_TICKS(30));
        if (ok)
            return true;
        i2c_master_bus_rm_device(q->dev);
        q->dev = NULL;
    }
    return false;
}

bool qmi8658_read(qmi8658_t *q, att_raw_t *out, bool *fresh) {
    uint8_t st = 0, b[12];
    *fresh = false;
    if (!q->dev || !rd(q, REG_STATUS0, &st, 1))
        return false;
    if ((st & 3) == 0)
        return true; /* bus fine, no new sample yet */
    if (!rd(q, REG_AX_L, b, sizeof(b)))
        return false;
    int16_t v[6];
    for (int i = 0; i < 6; i++)
        v[i] = (int16_t)((uint16_t)b[2 * i] | ((uint16_t)b[2 * i + 1] << 8));
    const float a = ATT_G / ACC_LSB_PER_G, g = 0.017453293f / GYR_LSB_PER_DPS;
    *out = (att_raw_t){.ax = v[0] * a, .ay = v[1] * a, .az = v[2] * a,
                       .gx = v[3] * g, .gy = v[4] * g, .gz = v[5] * g};
    *fresh = true;
    return true;
}
