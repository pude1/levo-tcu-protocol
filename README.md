# Levo TCU Protocol: ride fork (IMU terrain assist and trick challenges)

> **This is a fork** of [setha-maker/levo-tcu-protocol](https://github.com/setha-maker/levo-tcu-protocol).
> It keeps the upstream protocol, hardware and safety design, and adds a ride layer that uses the
> **QMI8658 IMU already on the Waveshare board**. No extra parts are needed:
>
> * **AUTO assist**: Eco/Trail/Turbo chosen from the terrain grade. It uses only the validated profiles.
> * **Trick challenges**: wheelie, manual, endo, bunny hop, jump and 180/360, with medals and personal bests.
> * **Sniff-only build and capture checker**: verify a different bike (e.g. a **Turbo Kenevo**) before replacing its TCU.
>
> Start with [ride features](docs/ride-features.md), [IMU hardware options](hardware/imu-options.md)
> and [Kenevo notes](docs/kenevo.md). Status: builds with ESP-IDF 5.4.2 and passes the host tests,
> including synthetic rides. It has **not been ridden**.


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
