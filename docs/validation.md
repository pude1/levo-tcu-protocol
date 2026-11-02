# Validation and limits

## What worked on the bike

The original replacement operated Off, Eco, Trail, Turbo, Walk and shutdown on the builder’s [2021 Specialized Turbo Levo](https://www.specialized.com/us/en/turbo-levo/p/184430?color=293256-184430), identified by the builder as the tested model (Specialized product 184430, color 293256). Its stock TCU was an SBC-D03. It was exercised on a workstand and ridden by the builder.

Specialized lists a 2.1 motor and M2-500 500 Wh battery for this model. These are published model specifications, not a compatibility test of every motor or battery revision. The supported motor/battery firmware range remains unconfirmed.

Capture analysis matched 66,551 command frames across 12 payload states and 66,546 companion frames. The included startup fixture reproduces 426 pairs, or 852 frames, byte-for-byte. The diagnostic fixture preserves framing and request order with identifying response values replaced.

## Why this repo is smaller

Development later added games, stored sessions, a web app and wireless updates. A later build passed a boot check after a wireless install, but on the bike it repeatedly started in Wi-Fi mode and powered off. The cause has not been established; the boot check was not a ride test.

This revision removes those features and restores the earlier simple screen and touch layout. It retains the protocol library and later CAN cleanup/fault checks, enables all four remote buttons and includes hold-minus shutdown. It is therefore **a simplified reference revision, not the exact previously ridden binary**. No new firmware was installed on the bike while preparing it.

## Checks you can reproduce

```sh
python3 tools/test.py
```

Requires Python 3 and a C11 compiler. C tests use AddressSanitizer and UndefinedBehaviorSanitizer; use `--no-sanitizers` only if your compiler lacks them. Tests cover encoding, capture replay, startup diagnostics, sequencing, remote release gates, Walk, touch hit zones, stale motor data, fault latching and CAN cleanup. They do not access a device.

This revision passes 11 C test programs and 9 Python tests. The reference firmware also builds with ESP-IDF 5.4.2. Neither check verifies hardware timing, GPIO contacts, power stability or motor acceptance. This build has not had a workstand check on the final assembly.

## Fork additions (ride features)

* `tests/ride_check.c` drives the whole ride pipeline with synthetic 200 Hz IMU data, noise,
  gyro bias and 20 Hz CAN speed. It checks: grade on 8/16/−12 % within 1.5–2 %; 2 m/s²
  acceleration on the flat not reading as a climb; AUTO Eco→Trail→Turbo→Eco; a steady 30 %
  ramp not counted as a wheelie; wheelie, manual, endo, bunny hop (height) and a 180 jump;
  AUTO frozen during tricks and with an invalid IMU; no level hunting on rollers.
* `tests/test_ride_replay.py` runs a synthetic USB log through `tools/ride_replay.py`.
* Firmware builds with ESP-IDF 5.4.2 for the default, sniff-only, loop-out-guard + raw-log,
  and IMU-disabled configurations.
* **Not done:** any run on hardware, including reading the QMI8658, the axis orientation, the
  touch/IMU bus sharing under load, real trick signatures, and Kenevo compatibility.
  Synthetic data checks the logic, not the thresholds.

## Known limits

- All eight startup diagnostic reads are required. An unsupported DID, timeout or negative response stops startup. Response-pending is not supported.
- Torque and battery fields are partly inferred. Absolute torque is uncalibrated; see the [protocol evidence](can-protocol.md#telemetry).
- Connector drawings reflect the builder's harness and keyed-face sketches. Pin numbering is a project convention. The battery drawing assigns only the CAN contacts.
- CAN faults stop transmission and isolate the transmit pin. Panic, watchdog, brownout and unknown resets block CAN startup on the next boot. A deliberate reset or power cycle is required to retry. The displayed fault is lost when power is removed.
- A final Off frame cannot be guaranteed after a bus or controller failure. There is no redundant motor-disconnect circuit.
- Long-term weather, vibration, thermal, endurance and failure-injection testing has not been performed.

Private captures, credentials, ride recordings, full-flash backups and development tools are excluded.
