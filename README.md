# Levo TCU Protocol: ride fork (IMU terrain assist and trick challenges)

> **This is a fork** of [setha-maker/levo-tcu-protocol](https://github.com/setha-maker/levo-tcu-protocol).
> It keeps the upstream protocol, hardware and safety design unchanged and adds a ride layer on top.
> Everything below "What this is" is the upstream README.

## Fork: ride features at a glance

| Feature | What it does | Extra hardware |
|---|---|---|
| **AUTO assist** | Chooses Eco/Trail/Turbo from the terrain grade; +/− shift the map by one level | None (onboard QMI8658 IMU) |
| **Trick challenges** | Wheelie, manual, endo, bunny hop (height), jump, 180/360, with bronze/silver/gold medals and personal bests | None |
| **Sniff-only build** | Listen-only CAN: try the ride features and check a different bike while the stock TCU stays in charge | A CAN tap and USB power |
| **Replay and tuning tools** | Replay recorded rides on a computer through the same C code; find the IMU axis | None |

**Status:** builds with ESP-IDF 5.4.2 and passes all host tests, including synthetic rides.
It has **not been run on a bike**. Thresholds are starting values.

### Quick start

1. Build the reference hardware from [hardware/README.md](hardware/README.md). The IMU is already on the Waveshare board.
2. **Different bike (e.g. Kenevo)?** Flash the sniff-only build first and run `tools/compare_capture.py` on a log. See [docs/kenevo.md](docs/kenevo.md).
3. Find the IMU forward axis with `tools/imu_orientation.py` (2 minutes, see [ride features → setup](docs/ride-features.md#setup)).
4. On the bike, standing level and still: hold **+** on the screen for 3 s to calibrate.
5. Ride with the Specialized button: Off → Eco → Trail → Turbo → **AUTO**.

### How it fits together

```
QMI8658 IMU (224 Hz, I2C 0x6B)                 CAN 0x201 speed / 0x203 cadence
        │                                                  │
        └──────────► ride/attitude.c ◄─────────────────────┘
                     pitch, roll, yaw rate, |g|, terrain grade
                        │                          │
               ride/tricks.c                ride/terrain_assist.c
          wheelie/manual/endo/hop/jump      grade → Eco/Trail/Turbo
                        │                          │
               ride/challenges.c          firmware CAN loop → bench_send() allowlist
           medals, PBs, points (NVS)        (same path as the buttons)
```

`ride/` is portable C with no ESP-IDF dependency, like `protocol/`. The firmware glue is
`firmware/main/qmi8658.c`, `ride_task.c` and `board_i2c.c`.

### Design notes

These are the choices that matter most for safety and for how it feels to ride.

* **The IMU only advises.** AUTO picks among the three captured profiles through the same
  `tcu_bench_select()` / `bench_send()` path as manual selection. It never selects Off or a
  new support/peak value. If the IMU fails, AUTO exits, the current level stays, and the CAN
  controller carries on normally.
* **A bike accelerometer rarely measures just gravity.** Pitch is corrected with the
  rear-wheel speed derivative, with the accelerometer delayed by the same filter lag so the
  two cancel. Roll uses the coordinated-turn lean, atan(v·yaw rate/g), because lateral
  acceleration is roughly zero in a turn. Without this, 2 m/s² of acceleration would read as
  a 20 % climb.
* **AUTO doesn't change level at the wrong moment.** It holds its level during tricks, in the
  air, under hard braking (fork dive), and while the IMU is untrusted. It shifts up after
  0.8 s and down after 3 s, with ±0.75 % hysteresis. A +/− press applies at once.
* **The terrain baseline freezes during tricks**, so a wheelie on a 10 % climb is measured
  from the climb. A pitch that stays steady for 2.5 s counts as a new slope, not a balance
  point. That rejects steep ramps.
* **No flash writes while CAN runs.** On the single-core ESP32-C6 an NVS write can stall
  for tens of milliseconds and break the 50 ms command schedule. Calibration and personal
  bests are saved after the shutdown sequence.
* **Real-time priorities:** the CAN loop runs at priority 5 and the IMU task at 3, sharing the
  I2C bus with touch through the IDF bus lock.
* **Loop-out guard** (drop to Eco above a set wheelie angle) is available but **off by default**:
  a sudden assist change mid-wheelie can surprise the rider.

### Recommendations

* Mount the board **rigidly** in the bezel. A rattling board shows up as fake air time.
* Calibrate on a floor you know is flat. Every 1 % of slope during calibration becomes a 1 %
  grade offset.
* **Kenevo:** 2020+ models share the Levo's 2.1 motor, M2 battery and top-tube TCU, which is
  promising but unverified. Sniff before you replace anything. All eight startup diagnostic
  reads must succeed or the controller stops safely.
* Tune from your own rides: record with `CONFIG_LEVO_RIDE_RAW_LOG=y`, then run
  `python3 tools/ride_replay.py ride.log --trace`, edit `ride/*.c`, and run
  `python3 tools/test.py` before flashing.
* Do the first powered tests on a workstand with the rear wheel clear, as upstream says.

### Roadmap

| Idea | Why | Notes |
|---|---|---|
| Barometer fusion (BMP581/BMP390 + CAN odometer) | Grade that doesn't drift and isn't affected by sag or calibration error | [IMU options B](hardware/imu-options.md#option-b-barometer-for-drift-free-grade-optional-upgrade) |
| Fork-unload sensor | Tells a real wheelie from a steep ramp, and an endo from a roll-in | [IMU options C](hardware/imu-options.md#option-c-front-wheel-unload-sensor-robust-trick-classification) |
| Threshold tuning from real Kenevo rides | Thresholds are currently based on synthetic data | Share logs or results as issues |
| Challenge modes (timed runs, combos such as manual → hop) | More game, same detector | `ride/challenges.c` |
| Grade-dependent support curve | Smoother than three steps | Needs CAN validation of intermediate support/peak values first |

### Fork documentation

| Doc | Contents |
|---|---|
| [docs/ride-features.md](docs/ride-features.md) | Controls, AUTO map, detection thresholds, setup, tuning, safety, limits |
| [hardware/imu-options.md](hardware/imu-options.md) | Onboard IMU details, mounting, barometer and fork-sensor upgrades |
| [docs/kenevo.md](docs/kenevo.md) | Kenevo differences and the sniff-first procedure |
| [docs/validation.md](docs/validation.md#fork-additions-ride-features) | What is tested and what isn't |

---

## What this is

Reverse-engineered CAN messages and connector pinouts from a [2021 Specialized Turbo Levo](https://www.specialized.com/us/en/turbo-levo/p/184430?color=293256-184430) with an SBC-D03 top-tube controller. The builder confirmed this is the tested model; see the [project bike](docs/images/test-bike.jpg).

The useful part is the protocol: startup, assist profiles, Walk, telemetry, counters, check bytes and shutdown. C and Python implementations are included so you can use the findings in your own controller. A basic ESP32 touchscreen controller and bezel files are included as a reference.

**Start with the [CAN protocol](docs/can-protocol.md) and [connector pinouts](hardware/pinouts.md).**

## What this is not

This is not a complete Specialized protocol specification, a universal replacement TCU, a motor firmware flash, or a speed-limit unlocker. Compatibility has only been investigated on the project bike.

During development, the controller gained games, a web app and wireless updates. Those features are not included here. A later build repeatedly started in Wi-Fi mode and then powered off on the bike; the cause remains unresolved. This repo uses the earlier simple screen and normal controls, retaining the later CAN fault checks. It builds and passes offline tests, but this exact revision has not been run on the bike.

Test a new build on a stand with the driven wheel clear. The design has no independent motor cutoff if the controller or bus fails.

## Find what you need

| Resource | Contents |
|---|---|
| [CAN protocol](docs/can-protocol.md) | Message layouts, examples, timing, telemetry and unknowns |
| [Protocol code](protocol/) | Portable C, Python decoder/encoder, DBC, startup and diagnostic sequencers |
| [Pinouts](hardware/pinouts.md) | Bike, handlebar remote and battery CAN contacts, with mating-face drawings |
| [Hardware](hardware/README.md) | Full wiring schematic, power, wake circuit, termination, ESP pins and [bezel STL files](hardware/3d-print/) |
| [Controller integration](docs/controller.md) | How to use the protocol code with another CAN interface |
| [Reference firmware](docs/build.md) | Normal modes, touchscreen, handlebar controls and USB flashing |
| [Validation](docs/validation.md) | What was measured, what was tested and what remains unverified |
| [Ride features](docs/ride-features.md) | *Fork:* IMU grade, AUTO assist, trick detection, challenges, tuning |
| [IMU options](hardware/imu-options.md) | *Fork:* onboard QMI8658, barometer and fork-sensor upgrades |
| [Kenevo](docs/kenevo.md) | *Fork:* compatibility checklist and sniff-first procedure |

The screen controller starts in Off and supports Eco, Trail, Turbo, hold-to-Walk and shutdown. It has no Wi-Fi, web interface, OTA, game modes or stored ride settings. Keep the USB port reachable; it is the only way to reflash.

## What I built

I replaced the stock TCU with a [Waveshare ESP32-C6-Touch-LCD-1.47](https://www.waveshare.com/ESP32-C6-Touch-LCD-1.47.htm), an external CAN transceiver and a buck converter, housed in a printed bezel. My installed version:

- Uses the original handlebar remote for assist selection and other controls.
- Starts Walk on button press, without a deliberate hold-to-start delay, and stops on release.
- Displays battery charge from CAN as a percentage and a graphical bar. The field matched the Specialized app in a static check; see the [telemetry notes](docs/can-protocol.md#telemetry).
- Uses a momentary button and 10 kΩ resistor to wake the bike. The controller runs from the bike’s switched 12 V supply through the buck; that supply is off while the bike sleeps. The separate wake line can sit near pack voltage and is not used to power the ESP32. Firmware handles shutdown.

**The photo shows my experimental firmware, not the screen included in this repo.** Its speedometer, estimated torque, battery display and settings interface are not included in the basic reference screen.

![The finished replacement TCU installed in the bike, running the experimental interface](docs/images/finished-controller.jpg)

To build on the project, this repo includes the [base STL](hardware/3d-print/base.stl), [shroud STL](hardware/3d-print/shroud.stl), [wiring schematic and parts list](hardware/README.md), and [basic controller firmware](docs/build.md). The STL files cover the printed parts only; the salvaged rubber button cap and switch are not included. Check the fit for your frame and hardware. You can also use the CAN protocol code without this screen or enclosure.

## Try the protocol offline

```sh
cc -std=c11 -Iprotocol examples/encode_turbo.c protocol/levo_protocol.c -o /tmp/encode-turbo
/tmp/encode-turbo
python3 tools/test.py
```

These commands print an example and run tests. They do not connect to a bike.

## Scope and contributions

This is a record of a one-bike project, not a supported product. Individual build troubleshooting, compatibility checks and feature requests are outside its scope. No further releases are promised.

Corrections to the published protocol, pinouts or code are welcome, especially with measurements or a reproducible example. A report from another bike should identify its TCU and distinguish observed behavior from assumptions. The redacted test fixtures are included; private captures, ride recordings and the experimental app are not part of this release.

## Credit

[Jamie Magee's specialized-turbo](https://github.com/JamieMagee/specialized-turbo) enabled the first Bluetooth experiments. [LevoEsp32Ble](https://github.com/Sepp62/LevoEsp32Ble) supplied additional BLE field definitions, and [OpenTCU research](https://github.com/ReadieFur/OpenTCU/tree/research) provided CAN research context. The encoder and timing here were checked against captures from the project bike.

The display uses Waveshare's board initialization and JD9853 driver. See [credits and licenses](docs/credits.md).
