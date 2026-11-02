# Reference screen controller

Normal operation only: Off, Eco, Trail, Turbo, hold-to-Walk and shutdown. The screen uses the earlier simple layout, with a colored mode header and large +/− controls. It displays controller state, not speed or battery telemetry.

This is a simplified source build, not an untouched archive binary. The saved screen/touch implementation is combined with the current protocol code and CAN fault cleanup. This exact build has not been checked on a workstand. See [validation and history](validation.md).

## Build and flash over USB

Hardware: **Waveshare ESP32-C6-Touch-LCD-1.47**, 8 MiB flash, external 3.3 V CAN transceiver. Toolchain: **ESP-IDF 5.4.2**. Follow [Espressif's setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/get-started/index.html), then export the IDF environment.

Ride options are under `idf.py menuconfig` → *Levo TCU ride features* (IMU axis, logging, sniff-only, loop-out guard).

```sh
cd firmware
idf.py build
# Disconnect the bike and the buck's 5 V connection before attaching USB.
idf.py -p YOUR_SERIAL_PORT flash monitor
```

Select the actual serial port, for example `/dev/cu.usbmodem…` on macOS. Version: `levo-tcu-0.5.0-ride` (fork; upstream is `0.4.0-reference`). Output: `build/levo_tcu_standalone.bin`.

Flash the complete build through USB, including its bootloader and partition table. This layout uses one factory application and has no OTA slots. **Do not upload this binary through an earlier build’s over-the-air updater. It removes wireless servicing.** Existing saved app settings are ignored; no erase-flash step is needed. Keep the USB port reachable; it is the only way to reflash.

## Controls

| Input | Action |
|---|---|
| Wake switch | Electrical wake through the 10 kΩ circuit |
| + / −, remote or screen | Raise / lower assist, one step per press |
| Specialized button | Off → Eco → Trail → Turbo → Eco |
| Hold − for two seconds, remote or screen | Off, then shutdown |
| Hold the screen's colored header for two seconds | Off, then shutdown |
| Hold Walk on the remote | Walk from Off; release to stop |
| Specialized button at Turbo (fork) | AUTO terrain assist; +/− then bias the map; Specialized leaves AUTO. See [ride features](ride-features.md#controls-additions-to-buildmd) |
| Hold + on the screen for 3 s (fork) | IMU level calibration; bike still on level ground |

Release the remote before selecting assistance. Walk requires a released input before it can start. Assistance stays Off until startup and the eight diagnostic reads finish. Touch gestures begun before readiness are discarded. There is no settings menu or inactivity timer.

Shutdown sends Off for five seconds, then the approximately two-second shutdown sequence. If USB is powering the board, its screen remains on after the bike shuts down. Faults leave the controller stopped; it does not retry automatically. A fault screen reports the first cause and requires a deliberate reset or power cycle.

## Initial workstand checks

With the driven wheel clear: verify Off at boot, both directions through the assist levels, Specialized cycling, Walk release, and shutdown. Repeat on the final buck supply with USB unplugged. Check startup and shutdown before sealing the assembly. Compilation and offline tests do not establish these physical behaviors.

USB monitor output includes CAN frames, button masks, selected modes and faults. Output is queued and may be dropped if USB is slow or absent; it is not a lossless recorder. No USB host is required to run the controller.
