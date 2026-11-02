# IMU hardware options

The ride features need a frame-fixed inertial sensor. Pick an option by how much you want to
add to the reference build.

## Option A: onboard QMI8658 (default, no extra parts)

The **Waveshare ESP32-C6-Touch-LCD-1.47** in the [parts list](README.md#parts) already carries a
**QST QMI8658** 6-axis IMU (3-axis accelerometer and 3-axis gyroscope).

| Item | Value used by this fork |
|---|---|
| Bus | Internal I2C, **GPIO18 SDA / GPIO19 SCL**, shared with the AXS5106L touch controller (0x63) |
| Address | 0x6B (0x6A probed as a fallback); WHO_AM_I = 0x05 |
| Interrupt pins | Not used. Data is polled at 200 Hz, so no GPIO is consumed. |
| Ranges | ±16 g (hard landings), ±1024 °/s |
| Output rate | 224.2 Hz with on-chip low-pass at about 30 Hz |

No wiring changes are needed: the remote, CAN and wake pins stay as in the
[reference schematic](diagrams/reference-schematic.svg).

**Mounting matters more than the sensor:**

* The board sits in the top-tube bezel, which is close to the bike's centre of mass and on the
  sprung frame. That is a good place for pitch and air-time detection.
* The board must not rattle. Pad it so it cannot move in the shroud (thin closed-cell foam or a
  dab of hot glue on the board edge, away from the antenna). A loose board turns trail chatter
  into fake "air time".
* The exact tilt doesn't matter: the level calibration (hold **+** for 3 s) measures it.
* Find the forward axis once with [`tools/imu_orientation.py`](../tools/imu_orientation.py).
* The IMU only sees the frame. It cannot tell which wheel is on the ground; it infers this
  from pitch and specific force. The limits are in [ride-features.md](../docs/ride-features.md#known-limits).

## Option B: barometer for drift-free grade (optional upgrade)

A pressure sensor plus the CAN odometer (`0x202`, metres) gives a second, independent grade:
grade = Δaltitude / Δdistance over 20 to 50 m. It cannot react quickly, but it does not drift,
and it is unaffected by sag, fork dive and mounting. Fused with the IMU grade, it corrects any
calibration error over a ride.

| Part | Why |
|---|---|
| **Bosch BMP581** or **BMP390** breakout, 3.3 V, I2C | About 0.1 m relative noise at a moderate rate; I2C addresses 0x46/0x47 (BMP581) or 0x76/0x77 (BMP390) do not clash with 0x63/0x6B |
| 4-wire lead, 100 nF decoupling at the sensor | |
| Vent: a 1 mm hole or a PTFE membrane in the shroud | Sealed boxes read the inside pressure and are disturbed by temperature |

Wiring: the board's I2C is internal (GPIO18/19 run to the touch FPC and IMU). Connect to it at
the I2C pull-ups or the touch connector pads. **Check your board revision's schematic** from
the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47) before soldering.
Fine-pitch work: use 30 AWG wire, add strain relief, and keep it under 10 cm.

Firmware: not implemented in this fork yet. The plan is a `bmp5xx.c` driver on `board_i2c_bus()`
and a complementary fusion in `ride.c` (IMU grade high-pass + baro grade low-pass at about
20 s). The interfaces are designed so this drops in without touching AUTO.

## Option C: front-wheel unload sensor (robust trick classification)

The only ambiguity the frame IMU cannot resolve is whether the **front wheel is actually off the
ground** (wheelie vs. steep ramp, endo vs. roll-in). A sensor on the fork answers this directly:

* **Fork-extension switch**: a small reed switch and magnet, or a mechanical microswitch,
  between crown and lower that closes in the last 5 mm of travel (topped out = front
  unloaded). It needs one GPIO with a pull-up. GPIO5 or GPIO6, the unused IMU interrupt nets,
  may be routed to a header pad on your revision; verify first.
* **Second IMU on the fork lower** (unsprung), for example an LSM6DSO on a 4-wire lead to the same
  I2C bus. The difference between fork and frame vertical acceleration shows contact and
  impacts per wheel.

Any wire from the fork to the frame must survive steering and full travel. Route it like a
brake hose, and use a connector so the front can be removed.

## What not to do

* Do not connect anything to the wake line or 12 V domain. Only 3.3 V logic belongs on the
  ESP32 pins (see [hardware README](README.md#connections-by-signal)).
* Do not fit the microSD card: its pins are the remote buttons in this build.
* Do not power add-on sensors from the 5 V buck rail without a regulator. Use the board's
  3.3 V.
