# Ride features: terrain AUTO assist and trick challenges

This fork adds an IMU-based ride layer on top of the reference controller. It uses the
**QMI8658 6-axis IMU that is already on the Waveshare ESP32-C6-Touch-LCD-1.47**. If you built
the reference hardware, you don't need any extra parts. See [IMU hardware options](../hardware/imu-options.md)
for optional upgrades.

> **Status: compiles, host-tested on synthetic rides, not yet ridden.** Thresholds are first
> guesses for a heavy, long-travel e-MTB. Expect to tune them from your own logs
> (see [Tuning](#tuning-with-your-own-rides)). Test every change on a stand first.

## What it does

| Feature | Behaviour |
|---|---|
| Terrain grade | Frame pitch from gyro + accelerometer. The accelerometer is corrected with the rear-wheel speed derivative from CAN `0x201`, so accelerating does not read as climbing. |
| **AUTO assist** | Picks Eco / Trail / Turbo from the grade, with hysteresis and dwell times. Never selects Off, never invents new support/peak values, and never changes level during a trick, in the air or under hard braking. |
| Trick detection | Wheelie, manual, endo/stoppie, bunny hop, jump, plus air rotation (180/360). |
| Challenges | Each trick is scored against a bronze/silver/gold ladder. Personal bests are kept across rides (NVS) and points are counted per ride. A banner shows the result for 4 s. |
| Sniff-only build | Listen-only CAN for checking a new bike (e.g. Kenevo) and for trying the ride features beside the stock TCU. |

## Controls (additions to [build.md](build.md#controls))

| Input | Action |
|---|---|
| Specialized button | Off → Eco → Trail → Turbo → **AUTO** → Eco … (AUTO is skipped if no IMU is found) |
| + / − in AUTO | Shift the terrain map one level up/down (−1…+1), applied at once |
| − in AUTO at Eco with bias −1 | Leave AUTO → Off |
| Specialized in AUTO | Leave AUTO → Eco |
| Hold − 2 s / hold header 2 s | Off, then shutdown (unchanged, also in AUTO) |
| **Hold + on the screen for 3 s** | Level calibration (bike standing still on level ground, rider off) |

The header shows the active level (ECO/TRAIL/TURBO). In AUTO, the status line reads, for
example, `AUTO+ +7.5%` (the sign after AUTO is the bias). Outside AUTO, the bottom line shows
`GRADE +7.5%`.

## Default AUTO map

| Grade | Level |
|---|---|
| below −4 % (descent) | Eco, whatever the bias |
| up to 4 % | Eco |
| 4 % – 11 % | Trail |
| above 11 % | Turbo |

The hysteresis band is ±0.75 % around each threshold. A level goes up after the grade has
been above its threshold for 0.8 s, and down after 3 s, with at least 1.5 s between changes.
Edit `terrain_cfg_default()` in [`ride/terrain_assist.c`](../ride/terrain_assist.c).

The +/− bias gives you the familiar "more / less" feel without leaving AUTO. Example: with
bias +1, a 5 % climb gives Turbo instead of Trail.

## How the detection works

```
QMI8658 (224 Hz) ──► attitude.c ──► pitch, roll, yaw rate, |f|, terrain grade
CAN 0x201 speed ──┘      │                 │
CAN 0x203 cadence ───────┼──► tricks.c ────┴─► event ──► challenges.c ──► medal, PB, points
                         └──► terrain_assist.c (in the CAN control loop) ──► Eco/Trail/Turbo
```

* **Grade** is a 2 s low-pass of pitch. It is *frozen* while a trick candidate is open, so a
  wheelie on a 10 % climb is measured from the climb.
* **Wheelie / manual**: nose up ≥ 10° above the terrain at ≥ 3 km/h, confirmed after 0.4 s,
  ended below 4°. Minimum 1 s. Pedalling more than half the time (cadence ≥ 20 rpm) makes it
  a wheelie; otherwise it is a manual. Distance comes from the rear-wheel speed, which stays
  valid in a wheelie.
* **Ramp rejection**: if the pitch stays within 1.5° for 2.5 s, the bike is on a new slope,
  not balancing. The candidate is dropped and the grade snaps to the new slope.
* **Endo**: nose down ≥ 8° at ≤ 12 km/h for ≥ 0.3 s.
* **Air time**: specific force below 0.45 g for 40 ms. Landing is the first impact above
  1.5 g, or a soft return above 0.85 g.
  * **Bunny hop**: air time ≤ 0.65 s, a front lift ≥ 6° within 0.8 s before take-off, and
    ≤ 25 km/h. Height estimate h = g·t²/8 (0.4 s ≈ 20 cm).
  * **Jump**: everything else in the air. Yaw rate is integrated while airborne for 180/360.

Starting challenge ladders ([`ride/challenges.c`](../ride/challenges.c)):

| Trick | Metric | Bronze | Silver | Gold |
|---|---|---|---|---|
| Wheelie | time | 2 s | 5 s | 10 s |
| Manual | time | 1.5 s | 3 s | 6 s |
| Endo | time | 0.5 s | 1 s | 2 s |
| Bunny hop | height | 10 cm | 20 cm | 30 cm |
| Jump | air time | 0.5 s | 0.8 s | 1.2 s |

Points: bronze 1, silver 3, gold 5, +2 for a personal best with a medal, +5 for a 180 and
+10 for a 360.

## Setup

1. **Find the IMU forward axis** (one time, about two minutes). Build with
   `CONFIG_LEVO_RIDE_RAW_LOG=y`, run `idf.py monitor | tee orient.log`, stand the bike level
   for 3 s, then lift the front wheel and hold it. Run
   `python3 tools/imu_orientation.py orient.log` and put the printed line into `sdkconfig`
   (or use `idf.py menuconfig` → *Levo TCU ride features*).
2. **Level calibration on the bike**: bike on level ground, rider off, bike on and ready.
   Hold **+** on the screen for 3 s. The screen shows `HOLD STILL` and then `LEVEL SAVED`.
   This measures the exact mounting tilt, so the bezel angle does not matter. Tip: calibrate
   on a spot you know is flat, such as a garage floor. On a 2 % slope, every grade reading is
   off by 2 %.
3. The calibration and personal bests are written to NVS **only after the CAN session has
   ended** (after shutdown). A flash write stalls the single-core ESP32-C6 for tens of
   milliseconds, which would break the 50 ms command schedule. If power drops before the write
   completes, the previous values are kept.

## Tuning with your own rides

```sh
# Record (laptop on a backpack, or the bike on a stand / in a parking lot)
idf.py -p PORT monitor | tee ride.log          # firmware built with CONFIG_LEVO_RIDE_RAW_LOG=y
# Replay through exactly the same C code, on the computer
python3 tools/ride_replay.py ride.log --forward 3 --trace
```

Change values in `ride/*.c`, run the replay again, and compare the detected tricks. Run
`python3 tools/test.py` before flashing. The synthetic rides in
[`tests/ride_check.c`](../tests/ride_check.c) must still pass.

The USB log queue can drop lines at 224 Hz. The replay tolerates gaps, but a long gap in the
middle of a trick will distort that trick.

## Safety design

* The IMU is **advisory only**. IMU loss never faults or stops the CAN controller. AUTO exits
  and keeps the current level, and manual control continues.
* AUTO calls the same `tcu_bench_select()` and passes through the same `bench_send()` profile
  allowlist as the buttons. Only the four captured profiles can reach the bus.
* AUTO is disabled while stopping, before startup is ready, and during Walk.
* The IMU task runs at a lower priority (3) than the CAN loop (5) and shares the I2C bus with
  touch through the IDF bus lock. No flash writes happen while CAN runs.
* **Loop-out guard** (`CONFIG_LEVO_LOOPOUT_GUARD_DEG`, off by default) drops to Eco when a
  wheelie exceeds the set angle. Removing assist lets the front drop, which is the safe
  direction, but a sudden change can surprise you. Enable it only after testing.
* Tricks are dangerous. The detection is for fun and feedback. It is not a safety system.

## Known limits

* Fork dive under braking tilts the frame. AUTO holds its level while decelerating harder than
  2.5 m/s², but grade readings during long braking are biased nose-down.
* Sag differences between seated and standing change frame pitch by a degree or two.
* Manual vs. wheelie relies on cadence. Ratcheting the cranks during a manual can turn it into
  a "wheelie".
* Drop-ins and roll-ins can look like endos for a moment. Ramp rejection catches steady
  slopes, not short transitions. A [fork-unload sensor](../hardware/imu-options.md) would
  make the classification robust.
