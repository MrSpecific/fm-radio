# FM Radio Project Context

## Hardware

| Component | Model |
|---|---|
| MCU | LilyGO TTGO T-Display (ESP32, 240 MHz, 4MB flash) |
| Radio module | ScoutMakes FM Radio Board (RDA5807M) via Qwiic/STEMMA I2C |
| Encoder | DT-067 rotary encoder with push button |
| Display | Built-in 1.14" ST7789 TFT, 135×240 px |

## Pin Assignments

| GPIO | Function |
|---|---|
| 0 | Button 2 — backlight brightness (INPUT_PULLUP) |
| 4 | TFT backlight (PWM) |
| 13 | Encoder SW (push button) |
| 14 | ADC_EN |
| 25 | Encoder CLK (A) |
| 26 | I2C SDA → radio board |
| 27 | I2C SCL → radio board |
| 32 | Encoder DT (B) |
| 33 | TFT sleep control |
| 34 | ADC input |
| 35 | Button 1 — enter/exit VOLUME mode (INPUT_PULLUP) |

TFT SPI pins (managed internally by TFT_eSPI, configured in library User_Setup):
MOSI=19, SCLK=18, CS=5, DC=16, RST=23, BL=4

## DT-067 Wiring

```
DT-067 Pin  →  T-Display
GND         →  GND
+           →  3.3V
SW          →  GPIO 13
DT          →  GPIO 32
CLK         →  GPIO 25
```

## Libraries

| Library | Purpose |
|---|---|
| TFT_eSPI | Display driver |
| radio (radiostationlibraries) | RDA5807M FM driver |
| RDSParser | RDS metadata parsing |
| AiEsp32RotaryEncoder | DT-067 encoder with ISR |

Install AiEsp32RotaryEncoder via Arduino Library Manager.

## UI Layout (135×240 display)

- `(14, 84)` — large current frequency (cleared with black rect before redraw)
- `(14, 114)` — mode indicator: "PRESET" or "SCAN"
- `(12, sy+8+i*15)` — white dot = selected preset indicator
- `sy=130, sy+i*15` — station name column (color3)
- `fx=88, fy=134+i*15` — frequency column (color2)

## Project Structure

Arduino requires the sketch file to live in a folder matching its name:
```
FMRadio/
  FMRadio.ino   ← active sketch
  fm.h          ← original bitmap (kept but no longer included)
  fonts.h
  oldfonts.h
.vscode/
  arduino.json  ← sketch path: "FMRadio/FMRadio.ino"
CLAUDE.md
```

## Display Layout (135×240, programmatic — no bitmap background)

| Y range | Content |
|---|---|
| 0–14 | Top bar (dark): "FM" label, 4-bar signal, ST/MN, M (mute), B (bass) |
| 15 | Separator line |
| 17–47 | Large display — font 4 (26px): frequency or "VOL X" |
| 49–60 | RDS station name, or volume bar |
| 62–72 | Mode label: PRESET / SCAN / VOLUME |
| 73 | Dim separator |
| 75–205 | Preset list (5 rows × 26px): dot · station name · frequency |

Top bar refreshes every 2s via `maybeRefreshStatus()`. Full-screen redraws (`drawFullUI`) only happen on mode transitions.

## Operating Modes

**PRESET mode** (default)
- Encoder rotation: cycles through 5 station presets (circular wrap)
- Encoder button: switch to SCAN mode

**SCAN mode**
- Encoder rotation: changes frequency in 10kHz steps, 87.5–108.0 MHz
- Acceleration enabled (fast spin = bigger jumps)
- Encoder button: switch back to PRESET mode

**VOLUME mode** (overlay — entered from either PRESET or SCAN)
- Button 1 (GPIO 35): enter volume mode; press again to exit and restore previous mode
- Encoder button: also exits volume mode
- Encoder rotation: adjusts volume 0–15; displays "VOL X" + proportional bar at y=100

**Button 2 (GPIO 0):** short press = cycle backlight brightness; long press = toggle bass boost

## All Controls Summary

| Control | Short press / turn | Long press |
|---|---|---|
| Encoder knob (turn) | Navigate presets / scan freq / adjust volume | — |
| Encoder push | Toggle PRESET↔SCAN; exit VOLUME | Seek up (SCAN mode only) |
| Button 1 (GPIO 35) | Enter/exit VOLUME mode | Toggle mute |
| Button 2 (GPIO 0) | Cycle backlight (5 levels) | Toggle bass boost |

## Features

- **Signal strength**: 4-bar display in top bar, updated every 2s
- **Stereo indicator**: ST/MN badge in top bar
- **RDS name**: shown below frequency, cleared on freq change
- **Auto-seek**: long-press encoder in SCAN mode; polls for STC bit with 4s timeout
- **Mute**: long-press Button 1; "M" indicator in top bar
- **Bass boost**: long-press Button 2; "B" indicator in top bar
- **Volume bar**: proportional bar shown in RDS area while in VOLUME mode
- **Idle sleep**: backlight off after 60s idle; any input wakes + restores brightness
- **Long/short press**: all buttons use millis()-based detection (600ms threshold)

## Preset Stations

| Name | Frequency |
|---|---|
| KOPB | 91.5 FM |
| KMHD | 89.1 FM |
| Jam'n | 107.5 FM |
| KBOO | 90.7 FM |
| KINK | 102.9 FM |

## Arduino Build Config (VSCode arduino.json)

- Board: `esp32:esp32:lilygo_t_display`
- Port: `/dev/tty.usbserial-537A0163171`
- Upload speed: 921600
- CPU: 240 MHz, Flash: QIO 80 MHz, 4M

## Frequency Unit Convention

All internal frequency values use **10kHz units** (matching `radio.setBandFrequency`):
- 91.5 MHz = 9150
- `formatFreq(int f)` converts to display string: `f/100` . `(f%100)/10`
