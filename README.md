# ESP32-test

Firmware experiments for the **Seeed XIAO ESP32C6** targeting a **TOF050F** laser distance sensor and a **128×64 OLED** display. Built with PlatformIO and deployed via a Universal Embedded Workbench (RFC2217 remote flash).

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | Seeed XIAO ESP32C6 |
| Distance sensor | TOF050F (Modbus RTU over UART) |
| Display | 128×64 OLED — SSD1306 or SH1106 @ I²C 0x3C |

### Wiring

```
XIAO ESP32C6       TOF050F
──────────────     ───────
D6 / GPIO16  TX → RXD
D7 / GPIO17  RX ← TXD
5V           ──── VIN
GND          ──── GND

XIAO ESP32C6       OLED 128×64
──────────────     ───────────
D4 / GPIO22  SDA → SDA
D5 / GPIO23  SCL → SCL
3.3V         ───── VCC
GND          ───── GND

GPIO15 — onboard LED (active-LOW, heartbeat blink)
```

---

## TOF050F Protocol

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

> Sub-mm or 1 decimal place display is not meaningful given the 3 mm native quantisation.

---

## Projects

### `step1_probe` — I²C Scanner

Scans the I²C bus (SDA=D4, SCL=D5) for connected devices. Runs 5 confirmation probes per address to reject phantom ACKs (IDF 5.x issue). Identifies known devices (OLED, IMU, VL6180X, BME280) and runs chip-specific probes.

**Result**: OLED found at 0x3C. TOF050F does **not** appear on I²C — it communicates via UART only.

### `step1_probe_uart` — UART Baud Sweep + Modbus Decoder

Sweeps 9600 / 19200 / 38400 / 57600 / 115200 baud, captures raw bytes, and attempts to match known frame formats (TFmini, 0xAA-header, ASCII keywords). Identified Modbus RTU at 115200 baud. Transitions to a continuous raw monitor at the discovered baud.

**Result**: 7-byte Modbus RTU frame confirmed, distance values 42–300 mm observed.

### `step2_display` — Live Display with EMA Smoothing

Production-ready firmware combining the sensor and display:

- Reads and CRC-validates streaming Modbus RTU frames
- Applies **Exponential Moving Average** (EMA) smoothing
- Applies a configurable **calibration offset**
- Renders on the OLED:
  - Top strip: label + current distance (mm)
  - Bottom half: **scrolling heartbeat-style line chart** (128 px wide, ~14 s per sweep, 3-px cursor gap)
- Prints `raw / calib / ema` to USB serial at 115200

#### Tuning (edit `step2_display/src/main.cpp`)

```cpp
#define CALIB_OFFSET  0      // subtract from raw (mm); set after measuring against a ruler
#define EMA_ALPHA     0.5f   // 0=frozen 1=raw; 0.5 ≈ 200 ms settling, 0.2 ≈ 500 ms
#define CHART_MAX_MM  300    // full-scale distance for the waveform
```

If the OLED is blank, the panel is SH1106 (common on 1.3" modules). Change line 21:

```cpp
// from:
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(...);
// to:
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(...);
```

---

## Build & Flash (PlatformIO + Workbench)

```bash
# Build
cd step2_display
pio run

# Flash via workbench API (ESP32C6 bootloader at 0x0000)
cd .pio/build/seeed_xiao_esp32c6
curl -X POST http://<workbench-ip>:8080/api/flash \
  -F slot=SLOT2 -F chip=esp32c6 -F baud=921600 \
  -F 'bin@0x0000=@bootloader.bin' \
  -F 'bin@0x8000=@partitions.bin' \
  -F 'bin@0x10000=@firmware.bin'
```

Serial monitor (115200 baud) shows live `raw / calib / ema` readings for calibration.

---

## Next Steps

- RS-485 multi-drop: assign unique Modbus addresses to each TOF050F, share one bus
- Multiple UART ports: `Serial1` + `Serial2` for two independent sensors
- Calibration: measure against a known reference and set `CALIB_OFFSET`
