# ESP32-test

Firmware experiments for the **Seeed XIAO ESP32C6** targeting a **TOF050F** laser distance sensor and a **128×64 OLED** display. Built with PlatformIO and deployed via a Universal Embedded Workbench (RFC2217 remote flash).

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | Seeed XIAO ESP32C6 |
| Distance sensor A | TOF050F — Modbus RTU over UART |
| Distance sensor B | TOF050F / VL6180X — I²C @ 0x29 |
| Display | 128×64 OLED — SSD1306 or SH1106 @ I²C 0x3C |

### Wiring

```
XIAO ESP32C6       TOF050F (UART)
──────────────     ──────────────
D6 / GPIO16  TX → RXD
D7 / GPIO17  RX ← TXD
5V           ──── VIN
GND          ──── GND

XIAO ESP32C6       TOF050F / VL6180X (I²C)
──────────────     ──────────────────────
D4 / GPIO22  SDA → SDA   (shared with OLED)
D5 / GPIO23  SCL → SCL   (shared with OLED)
3.3V         ───── VCC
GND          ───── GND

XIAO ESP32C6       OLED 128×64
──────────────     ───────────
D4 / GPIO22  SDA → SDA
D5 / GPIO23  SCL → SCL
3.3V         ───── VCC
GND          ───── GND

GPIO15 — onboard LED (active-LOW, heartbeat blink)
```

---

## Sensor Protocols

### UART sensor — TOF050F (Modbus RTU)

Discovered via the `step1_probe_uart` baud sweep:

- **Protocol**: Modbus RTU (slave address 0x01, function 0x03)
- **Baud rate**: 115200, 8N1
- **Mode**: autonomous streaming (~9 Hz, no poll request needed)
- **Frame** (7 bytes):

  ```
  01  03  02  [distH]  [distL]  [crcL]  [crcH]
  ```

- **Distance unit**: millimetres (integer)
- **Resolution**: 3 mm steps (all raw values are multiples of 3)
- **Accuracy**: ±3 mm or ±3%, whichever is larger
- **Update rate**: ~9 Hz (~110 ms per sample)

> Sub-mm or 1 decimal place display is not meaningful given the 3 mm native quantisation.

### I²C sensor — VL6180X at 0x29

- **Protocol**: I²C, 16-bit register addressing (ST VL6180X)
- **Mode**: single-shot, triggered by writing 0x01 to register 0x0018
- **Result register**: 0x0062 (8-bit, mm); 0xFF = no target / out of range
- **Distance unit**: millimetres (1 mm resolution)
- **Max range**: ~200 mm under typical indoor conditions
- **Update rate**: hardware capable of ~100 Hz; **rate-limited in firmware to ~10 Hz**

---

## EMA Smoothing — Important Note on Update Rate

Both sensors use the same Exponential Moving Average formula:

```
ema = α × new + (1 − α) × ema
```

**The effective settling time depends on both α AND the sample period**, not α alone:

```
τ ≈ −T / ln(1 − α)
```

| Sensor | Sample period (T) | α | Time constant (τ) |
|--------|-------------------|---|-------------------|
| UART   | ~110 ms           | 0.5 | ~159 ms |
| I²C (uncapped) | ~10 ms  | 0.5 | ~14 ms  |
| I²C (rate-limited to 10 Hz) | ~100 ms | 0.5 | ~144 ms |

Running the VL6180X at its native ~100 Hz with α = 0.5 made the smoothing 11× weaker than the UART sensor — the EMA was effectively transparent. The fix is to **rate-limit the I²C sensor to match the UART sample period** (100 ms), giving both sensors the same time constant.

> When comparing two sensors or tuning EMA, always consider the sample rate. A single α value does not produce the same smoothing behaviour across sensors with different update rates.

---

## Projects

### `step1_probe` — I²C Scanner

Scans the I²C bus (SDA=D4, SCL=D5) for connected devices. Runs 5 confirmation probes per address to reject phantom ACKs (IDF 5.x issue). Identifies known devices (OLED, IMU, VL6180X, BME280) and runs chip-specific probes.

**Result**: OLED found at 0x3C, VL6180X/TOF050F found at 0x29.

### `step1_probe_uart` — UART Baud Sweep + Modbus Decoder

Sweeps 9600 / 19200 / 38400 / 57600 / 115200 baud, captures raw bytes, and attempts to match known frame formats (TFmini, 0xAA-header, ASCII keywords). Transitions to a continuous raw monitor at the discovered baud.

**Result**: 7-byte Modbus RTU frame confirmed at 115200 baud.

### `step2_display` — Single Sensor Live Display

Reads the UART TOF050F, applies EMA smoothing and calibration offset, and renders on the full 128×64 OLED:

- Top strip: label + current distance (mm)
- Bottom half: scrolling heartbeat-style line chart (~14 s per sweep)
- Prints `raw / calib / ema` to USB serial

#### Tuning (`step2_display/src/main.cpp`)

```cpp
#define CALIB_OFFSET  0      // subtract from raw (mm)
#define EMA_ALPHA     0.5f   // 0=frozen, 1=raw; 0.5 ≈ 200 ms settling
#define CHART_MAX_MM  300    // full-scale for waveform
```

### `step3_dual` — Dual Sensor Split Display

Reads both sensors simultaneously and displays them side-by-side on the OLED:

- **Left half**: UART TOF050F (Modbus RTU, ~9 Hz)
- **Right half**: I²C VL6180X (single-shot, rate-limited to 10 Hz)
- Each half shows: distance number (top) + scrolling heartbeat chart (bottom)
- 2-pixel vertical divider between halves
- Both sensors use EMA (α = 0.5) with matched time constants

#### Tuning (`step3_dual/src/main.cpp`)

```cpp
#define CALIB_UART   0      // mm offset for UART sensor
#define CALIB_I2C    0      // mm offset for I²C sensor
#define EMA_ALPHA    0.5f
#define CHART_MAX    300    // mm full-scale for both waveforms
```

If the OLED is blank, swap to SH1106 driver on line 21:

```cpp
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(...);
```

---

## Connecting Multiple Sensors

### Option A — RS-485 multi-drop (3+ sensors)

Requires the UART TOF050F to support Modbus address change and polled mode (currently streams autonomously). All sensors share one 2-wire RS-485 bus via a MAX485 transceiver.

### Option B — Multiple hardware UARTs (2 sensors)

`Serial1` (D6/D7) + `Serial2` (D4/D5) — each sensor keeps address 0x01, streams independently. No address configuration needed.

---

## Build & Flash (PlatformIO + Workbench)

```bash
# Build (replace step3_dual with target project)
cd step3_dual
pio run

# Flash via workbench API (ESP32C6 bootloader at 0x0000)
cd .pio/build/seeed_xiao_esp32c6
curl -X POST http://<workbench-ip>:8080/api/flash \
  -F slot=SLOT2 -F chip=esp32c6 -F baud=921600 \
  -F 'bin@0x0000=@bootloader.bin' \
  -F 'bin@0x8000=@partitions.bin' \
  -F 'bin@0x10000=@firmware.bin'
```

Serial monitor at 115200 baud prints live `UART XXX mm  I2C XXX mm` for both sensors.
