# Hardware

The reference build uses a Waveshare **ESP32-C6-Touch-LCD-1.47**, a 3.3 V CAN transceiver, a 12 V-to-5 V buck, and removable Julet pigtails. The stock TCU is removed. The handlebar remote connects directly to the ESP32 inside the shroud.

![Completed electronics](../docs/images/assembled-electronics.jpg)

> **Fork:** the ride features use the board's onboard QMI8658 IMU. No wiring changes are needed. Optional upgrades are described in [IMU options](imu-options.md).

## Wiring schematic

[Open the full schematic](diagrams/reference-schematic.svg).

![Reference wiring schematic](diagrams/reference-schematic.svg)

The schematic uses module terminal labels, connector pin numbers and signal names. It does not specify wire colors. U2A and U2B are the same ESP32 board; matching net labels and ground symbols are electrically connected. Connector geometry and mating-face numbering are in the [pinout drawings](pinouts.md).

## Parts

| Part | Requirement |
|---|---|
| ESP32/display | [Waveshare ESP32-C6-Touch-LCD-1.47](https://www.waveshare.com/ESP32-C6-Touch-LCD-1.47.htm), 8 MiB flash, 172 × 320 touch display; [board documentation](https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47) |
| CAN module | 3.3 V supply and logic, CAN H/L, transmit input and receive output; reference module exposes VCC/GND/CTX/CRX/CANH/CANL; its exact part number was not recorded |
| Buck | Common-ground 12 V input to regulated 5 V output, sized for display and controller load; photographed unit is marked LM2596 DC-DC |
| Bike pigtail | 5-pin male on replacement, mates to bike's 5-pin female |
| Remote pigtail | 6-pin female on replacement, mates to remote's 6-pin male |
| CAN termination | R1: one 120 Ω, 1%, ¼ W resistor across H/L, only if module has no terminator |
| Wake resistor | R2: 10 kΩ, 1%, ½ W, in series with normally-open momentary switch |
| Transmit pull-up | R3: separate 10 kΩ, 1%, ¼ W from transceiver CTX to ESP 3V3 |
| Remote common resistor | R4: 220 Ω, 1%, ¼ W between remote common and ground |
| Assembly | Narrow perfboard, insulated wiring, heat shrink, strain relief, removable 5 V link for USB servicing |

The prototype used two 220 Ω resistors in parallel, about 110 Ω, for CAN termination. Use the specified 120 Ω part in a new build. The 220 Ω remote resistor limits current in the button-common path. The separate 10 kΩ CTX pull-up holds the transceiver input recessive while the ESP transmit pin is high-impedance.

The [connector pinouts](pinouts.md) show both mating faces, key orientation and signal assignments for the 5-pin bike and 6-pin remote connectors. Numbers follow the builder's reference drawings, not a claimed Julet standard. **Do not wire a different cable by the prototype colors.** Identify signal continuity against your own connector before applying power.

## Pigtails

Buy a **5-pin male** for the bike and a **6-pin female** for the remote. A 6-pin male is not a substitute for the bike's 5-pin male lead. Count the actual contacts: male means pins, female means sockets. Match the shell size, key and mating face as well as the pin count. Listings sometimes use Julet/Higo names loosely.

The builder supplied these purchase links for the pigtails used in this project. **Affiliate disclosure:** Seth may earn a commission from purchases through these links.

| Connection | Purchase link | Select for the replacement TCU |
|---|---|---|
| Bike | [5-pin Julet pigtail](https://link.amazon/B07bNEtk0) | **5-pin male**. The link currently opens the female variant; change the selection before ordering. |
| Handlebar remote | [6-pin Julet pigtail](https://link.amazon/B03kR9njY) | **6-pin female** |

Confirm the selected variant, shell size and key against your harness before ordering. These links identify the project’s pigtail listings, not compatibility with every Julet connector.

A matching extension cable can also be cut to obtain a pigtail. Use an intact molded connector; this project does not require crimping the small contacts. With power disconnected, use continuity to map each numbered contact to its wire. Insulation colors are not a pinout.

## Connections by signal

| From | To |
|---|---|
| Bike ground | ESP GND, transceiver GND, buck IN− |
| Bike switched approximately 12 V | Buck IN+ |
| Buck OUT+ 5 V | ESP 5V/VBUS through removable isolation link |
| Buck OUT− | Common ground; verify continuity to IN− on your buck |
| Bike wake line, approximately pack voltage | 10 kΩ → normally-open momentary switch → ground |
| ESP 3V3 | CAN VCC and 10 kΩ CTX pull-up |
| ESP GPIO17 | CAN CTX/transmit input |
| ESP GPIO7 | CAN CRX/receive output |
| Bike CAN H / CAN L | Module CAN H / CAN L |
| CAN H | 120 Ω → CAN L |
| Remote common | 220 Ω → ESP GND |
| Remote plus / minus | GPIO3 / GPIO4 |
| Remote Walk / Specialized | GPIO16 / GPIO8 |
| Remote unused conductor | Insulate separately |

The board labels GPIO17 **RXD** and GPIO16 **TXD** for their UART defaults. Firmware remaps them: GPIO17 sends CAN commands; GPIO16 reads Walk. Leave the microSD slot empty because these buttons reuse its pins.

GPIO inputs use internal pull-ups. The wake switch is an electrical bike wake circuit, not an ESP input. Its open lead can sit near pack voltage. It must never connect to a GPIO.

## Power and termination

The bike's switched 12 V line powers the buck after wake. The ESP32 then maintains the CAN exchange. Shutdown releases the bike power supply. An added ESP battery is not required for this momentary-switch design.

With all power sources disconnected, the complete bus on this build measured about 60 Ω across CAN H/L. The far-end termination depended on the battery connector being connected. The [bike-side battery plug drawing](pinouts.md#battery-connector) identifies the two CAN contacts by position. Its other contacts and numbering are not mapped; do not infer them from a plug's wire color.

Disconnect the buck's 5 V link before powering through USB. This assembly has no verified power OR-ing circuit. Keep 12 V and pack-voltage conductors away from ESP and logic-analyzer inputs. A 3.3 V CAN transceiver handles the differential bus; the ESP GPIOs do not.

## Printed parts

[`base.stl`](3d-print/base.stl) and [`shroud.stl`](3d-print/shroud.stl) are the supplied reference parts. A **parametric rebuild** with STEP exports is in [`cad/`](cad/README.md). STL bounding boxes are 87.2 × 33 × 13 mm for the base and 71.7 × 30 × 11.7 mm for the shroud. Check fit against your frame opening and display before permanent assembly.

The rubber cap and momentary switch were salvaged from other projects and are not supplied by these models. Fabricate a suitable button assembly or adapt the design. A handlebar power-button alternative would require separate electrical validation; it is not implemented here. Heat shrink and a printed shroud do not establish an ingress-protection rating.

## Regenerate the schematic

Run `python3 tools/generate_schematic.py` from the repository root. This produces the SVG using the Python standard library. The schematic shows module interconnections, not a PCB layout or the modules’ internal circuits.
