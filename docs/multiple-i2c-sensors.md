# Connecting Multiple TOF050F (VL6180X) Sensors on One I²C Bus

## The Problem

All VL6180X sensors boot with the same default I²C address: **0x29**. Putting more than one on the same bus without address separation causes collisions — both devices respond to every transaction simultaneously, corrupting the data.

Two standard approaches solve this.

---

## Option A — XSHUT Pin + Address Assignment (recommended)

The VL6180X has an **XSHUT** pin. Pulling it LOW holds the device in hardware reset (silent on the bus); HIGH = active. Use one GPIO per sensor to bring them online one at a time and assign each a unique address before enabling the next.

### Wiring (4 sensors)

```
XIAO ESP32C6        Sensor 1    Sensor 2    Sensor 3    Sensor 4
────────────        ────────    ────────    ────────    ────────
D4 / GPIO22  SDA ──── SDA ────── SDA ────── SDA ────── SDA
D5 / GPIO23  SCL ──── SCL ────── SCL ────── SCL ────── SCL
D0 / GPIO0  ──────── XSHUT
D1 / GPIO1  ─────────────────── XSHUT
D2 / GPIO2  ──────────────────────────────── XSHUT
D3 / GPIO3  ───────────────────────────────────────── XSHUT
3.3V ──────────────── VCC ────── VCC ────── VCC ────── VCC
GND  ──────────────── GND ────── GND ────── GND ────── GND
```

Add a single pair of pull-up resistors (4.7 kΩ) on SDA and SCL to 3.3 V.

### Boot Sequence

```cpp
// 1. Hold all sensors in reset
pinMode(XSHUT1, OUTPUT); digitalWrite(XSHUT1, LOW);
pinMode(XSHUT2, OUTPUT); digitalWrite(XSHUT2, LOW);
pinMode(XSHUT3, OUTPUT); digitalWrite(XSHUT3, LOW);
pinMode(XSHUT4, OUTPUT); digitalWrite(XSHUT4, LOW);
delay(10);

// 2. Bring up sensor 1, assign address 0x2A
digitalWrite(XSHUT1, HIGH); delay(10);
vl_write8_at(0x29, 0x0212, 0x2A);   // I2C_SLAVE__DEVICE_ADDRESS

// 3. Bring up sensor 2, assign address 0x2B
digitalWrite(XSHUT2, HIGH); delay(10);
vl_write8_at(0x29, 0x0212, 0x2B);

// 4. Bring up sensor 3, assign address 0x2C
digitalWrite(XSHUT3, HIGH); delay(10);
vl_write8_at(0x29, 0x0212, 0x2C);

// 5. Bring up sensor 4 — keep default 0x29 (or assign 0x2D)
digitalWrite(XSHUT4, HIGH); delay(10);
// vl_write8_at(0x29, 0x0212, 0x2D);  // optional

// All four sensors now have unique addresses and can be polled freely.
```

### Key Points

- **Address is volatile** — lost on every power cycle. The assignment sequence must run at every boot.
- Requires XSHUT to be physically broken out on the TOF050F module — check the pinout before purchasing.
- On the XIAO ESP32C6, pins D0/D1/D2/D3 (GPIO0–3) are free and suitable for XSHUT control.

---

## Option B — TCA9548A I²C Multiplexer

A TCA9548A provides 8 independent I²C channels. You select a channel before reading, so every sensor can keep address 0x29. No XSHUT pin required.

### Wiring

```
XIAO ESP32C6        TCA9548A (0x70)
────────────        ───────────────
D4 / SDA ─────────── SDA
D5 / SCL ─────────── SCL
3.3V ─────────────── VCC
GND  ─────────────── GND
A0, A1, A2 ──────── GND  (sets address to 0x70)

TCA9548A            Sensors
────────            ───────
SC0 / SD0 ────────── Sensor 1 (0x29)
SC1 / SD1 ────────── Sensor 2 (0x29)
SC2 / SD2 ────────── Sensor 3 (0x29)
SC3 / SD3 ────────── Sensor 4 (0x29)
SC4 / SD4 ────────── OLED     (0x3C)  ← optional
```

### Usage

```cpp
void selectChannel(uint8_t ch) {
    Wire.beginTransmission(0x70);
    Wire.write(1 << ch);   // bit mask: ch=0 → 0x01, ch=1 → 0x02, etc.
    Wire.endTransmission();
}

// Read sensor 2
selectChannel(1);
uint8_t dist = vl_read8(0x0062);   // sensor 2 at 0x29

// Read sensor 4
selectChannel(3);
dist = vl_read8(0x0062);           // sensor 4 at 0x29
```

### Key Points

- No address reassignment, no volatile state — simpler firmware.
- TCA9548A address is set by pins A0–A2 (0x70–0x77), so two muxes can share a bus (up to 16 sensors).
- Adds one component (~$1–2) and one I²C transaction overhead per sensor read.

---

## Comparison

| | XSHUT assignment | TCA9548A mux |
|---|---|---|
| Extra IC | None | TCA9548A |
| Extra GPIO pins | 1 per sensor (4 for 4 sensors) | 0 |
| XSHUT pin required | **Yes** | No |
| Address change volatile | **Yes** (re-run on every boot) | N/A |
| Firmware complexity | Boot sequence only | Channel select before every read |
| Max sensors | Limited by free GPIO | 8 per mux, stackable to 128 |
| Works if XSHUT not exposed | No | **Yes** |

---

## EMA Rate Consideration for Multiple Sensors

When polling N sensors sequentially, each sensor's effective sample rate drops:

```
sample_period_per_sensor = poll_loop_period × N
```

With 4 VL6180X sensors polled in rotation at ~10 ms each:
- Total loop = 40 ms per sensor → ~25 Hz per sensor

This is still faster than the UART TOF050F (~9 Hz), so **rate-limit each I²C sensor** independently (as done in `step3_dual`) to keep EMA time constants matched across all sensors. See [README](../README.md#ema-smoothing--important-note-on-update-rate) for the formula.

---

## Recommended Approach for This Project

1. **Check the TOF050F module pinout** — if XSHUT is broken out, use Option A (no extra IC, fewer wires).
2. **If XSHUT is not accessible**, use Option B with a TCA9548A.
3. Keep I²C clock at **100 kHz** when running 4+ devices; longer wire runs increase bus capacitance and can cause signal integrity issues at 400 kHz.
4. Rate-limit each sensor to the same period and match `EMA_ALPHA` across all channels for consistent smoothing.
