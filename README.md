# PS4 Ambilight IR Bridge

> ## ⚠️ Status: NOT YET TESTED
> This project is a work in progress. The code has **not** been run on real hardware
> with a PS4 yet. Expect bugs. First real tests are planned; this README will be
> updated with the results. Reports are welcome, see [CONTRIBUTING.md](CONTRIBUTING.md).

## Goal

The excellent [PS4-Ambilight](https://github.com/mitchshimy/PS4-Ambilight) plugin
needs a WLED controller and an addressable LED strip (WS2812B).

The goal of this project is to make that plugin **usable without the special strip**,
with a **normal, cheap LED strip** (plain RGB with an IR remote), and **without
cutting, soldering or rewiring** the strip.

It does this by replacing WLED with an ESP32 that:

1. Pretends to be a WLED controller on the network (receives DDP packets on UDP 4048).
2. Averages the screen colours the plugin sends into one colour.
3. "Presses" the closest colour button on the strip's own IR remote, using an IR LED.

```
PS4 game  ->  PS4-Ambilight plugin  --DDP over Wi-Fi-->  ESP32 (this project)
                                                              |  IR LED
                                                              v
                                              strip's IR receiver / controller
```

## Limitations (by design)

- The whole strip shows **one colour**, so it is mood lighting, not true ambilight.
- Only the remote's fixed colours (about 12 to 20) can be shown.
- IR is slow, so a few updates per second at most.
- Needs line of sight between the IR LED and the strip's receiver (a few cm is fine).

## What you need

- PS4 with GoldHEN, with the PS4-Ambilight plugin and companion app installed
- ESP32 dev board (any common one)
- IR LED (940 nm) + 100 Ω resistor, or a ready-made ESP32 IR blaster board
- An IR receiver module (VS1838B / TSOP38238), only to capture your remote's codes
- Jumper wires / breadboard (no soldering)
- An RGB strip with a 24-key IR remote
- Arduino IDE with: **esp32 by Espressif** board package and the **IRremoteESP8266** library

## Setup

### 1. Capture your remote's codes (once)
1. Wire the IR receiver: OUT -> GPIO 14, VCC -> 3V3, GND -> GND.
2. Open the example **IRrecvDumpV2** (IRremoteESP8266), set `kRecvPin` to `14`, upload.
3. Open Serial Monitor at 115200 baud, press each remote button, note the codes.
4. Compare with [docs/ir-codes.md](docs/ir-codes.md). If they match, skip editing the table.

### 2. Wire the IR LED
`GPIO 4 -> 100 Ω resistor -> IR LED anode (long leg)`, cathode (short leg) -> `GND`.
Point it at the strip controller's IR receiver from 2 to 5 cm.
Check it with a phone camera: the LED shows a purple flash when sending.

### 3. Flash the firmware
1. Open `firmware/ps4_ambilight_ir_bridge/ps4_ambilight_ir_bridge.ino` in Arduino IDE.
2. Copy `config.example.h` to `config.h` (same folder) and enter your Wi-Fi name and password.
3. Select your ESP32 board and port, upload.
4. Open Serial Monitor (115200). It prints the ESP32's **IP address**.

The ESP32 must join your **home Wi-Fi**, the same network as the PS4. If it can't
connect it starts a fallback hotspot `PS4-Ambilight`, page at `192.168.4.1`.

### 4. Use the web page
Open `http://<ESP32 IP>` (or `http://ps4ambilight.local`, which not all Android phones
support). It shows the live colour, the chosen key, packet count and IP, lets you change
settings (enabled, minimum ms between IR sends, off threshold) and has test buttons for
every colour. **Test the IR buttons here first**, before involving the PS4.

Tip: reserve the ESP32's IP in your router so it never changes.

### 5. Connect the PS4 plugin
1. In GoldHEN: Settings -> GoldHEN -> Plugin Settings -> enable the plugin loader.
2. Install the PS4-Ambilight companion app and plugin (see its README).
3. In the companion app, **Set up**:
   - WLED host: the ESP32's IP
   - Port: `4048`
   - LED counts: any small numbers (e.g. 4 per edge), the layout is ignored
   - Color order: `RGB`
4. Press **Test Strip**, then start a game.

## Tuning

- Colours look wrong: edit the RGB values in `keys[]` in the sketch.
- Misses changes: move the IR LED closer, or lower the minimum interval.
- Flickers between colours: raise the minimum interval (e.g. 800 ms).

## Credits

This project stands on the work of others. It does not copy their code, but it would
not exist without them:

- **[mitchshimy / PS4-Ambilight](https://github.com/mitchshimy/PS4-Ambilight)** (GPL-3.0):
  the PS4 plugin and companion app this bridge is built for. All the hard work of
  capturing the PS4 video output is theirs.
- **[GoldHEN](https://github.com/GoldHEN)** and its plugin SDK, which the plugin runs on.
- **[WLED](https://github.com/Aircoookie/WLED)**, whose network behaviour this bridge imitates.
- **DDP (Distributed Display Protocol)** by 3waylabs, the packet format used.
- **[IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266)** (LGPL-2.1),
  used for sending and capturing IR codes.
- **Arduino core for the ESP32** by Espressif.

This project is independent and **not affiliated with or endorsed by** any of the above.
Anyone who contributes will be listed here.

## Disclaimer

Running plugins requires a jailbroken console, which may carry risks and may not be
allowed in your region or by Sony's terms. You are responsible for your own console.
Software is provided as is, with no warranty. See [LICENSE](LICENSE) (MIT).
