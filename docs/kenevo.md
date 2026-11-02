# Using this on a Specialized Turbo Kenevo

The upstream protocol was measured on one **2021 Turbo Levo with an SBC-D03 TCU**. The
**2020+ Turbo Kenevo** (180/180 mm, 27.5") shares the platform: Specialized **2.1 motor**,
**M2 battery** (500/700 Wh) and the same style of **Turbo Connect Unit in the top tube** with
LED bar and a left-hand remote. That makes it a plausible candidate, not a verified one.

> Earlier (MY2018–2019) Kenevos used the previous motor/TCU generation. Check that yours has the
> 2.1 motor and the top-tube TCU with LED battery bar, and note the TCU part number before
> starting.

## Differences to expect

| Item | Levo (tested) | Kenevo | Effect |
|---|---|---|---|
| Wheel circumference (DID `0206`) | Levo value | Different (27.5" DH tyres) | Read at startup; nothing hard-coded |
| Shuttle setting (DID `020D`) | Read | Kenevo marketing emphasises Shuttle; may be set differently | Read-only here; no change |
| Motor/battery firmware | One version | Unknown | **All eight startup reads must succeed.** An unsupported DID stops startup, which is the safe failure. |
| Captured profile values | Eco 35/35, Trail 50/70, Turbo 100/100 | Possibly different factory tune | The allowlist only sends the Levo values. They are percentages of the motor's capability, but check how they feel. |
| Suspension | 150/140 mm | 180/180 mm | More sag and fork dive: re-tune grade braking hold if needed |

## Recommended path

1. **Sniff first, stock TCU in place.** Build with `CONFIG_LEVO_SNIFF_ONLY=y`. The transceiver
   then runs listen-only: it sends no data, no ACK and no error frames. Tap CAN H/L in parallel
   with the stock TCU (a 5-pin male/female Julet Y-splice, or the extension cable trick from
   the [hardware guide](../hardware/README.md#pigtails)). **Do not add a second 120 Ω terminator**
   for a short stub. Power the ESP32 from USB (a power bank), and connect only CAN H, CAN L
   and ground.
2. Record a session with `idf.py monitor | tee kenevo-stock.log`: wake, cycle Eco/Trail/Turbo,
   Walk, ride a little, and shut down.
3. Check it: `python3 tools/compare_capture.py kenevo-stock.log`. The script verifies that every
   `0x300` frame passes the documented check nibble, that companions match the table, and that
   the timing is about 50 ms. It also lists the profile payloads your bike actually uses and
   the telemetry IDs present.
4. If everything matches: the ride features (grade, tricks, challenges) already work in sniff
   mode, so you can tune them on real rides while the stock TCU controls the motor.
5. Only then build the normal firmware, replace the TCU, and do the workstand checks from
   [build.md](build.md#initial-workstand-checks) with the rear wheel clear.

If the profile payloads differ (for example a different Trail tune), add them as a separate
profile table rather than editing the Levo values, and keep the `bench_send()` allowlist exact.
