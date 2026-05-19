#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#define LED_PIN   15
#define SDA_PIN   22
#define SCL_PIN   23
#define RX_PIN    17   // D7 ← UART TOF TXD
#define TX_PIN    16   // D6 → UART TOF RXD

#define CALIB_UART   0      // mm offset for UART sensor
#define CALIB_I2C    0      // mm offset for I2C sensor
#define EMA_ALPHA    0.5f

// Display geometry: 63px | 2px divider | 63px = 128px
#define HALF_W    63
#define R_OFF     65        // right-half x origin
#define CHART_Y   15
#define CHART_H   (64 - CHART_Y)   // 49px
#define CHART_MAX 300       // mm full-scale for both waveforms

// VL6180X (I2C sensor)
#define VL_ADDR   0x29

// Swap to SH1106 if display is blank (1.3" panel)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

// ── Modbus CRC-16 ──────────────────────────────────────────────────────────
static uint16_t modbusCRC(const uint8_t* buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

void updateLED() {
    static uint32_t last = 0; static bool on = false;
    if (millis() - last >= 500) { last = millis(); on = !on; digitalWrite(LED_PIN, on ? LOW : HIGH); }
}

// ── EMA ───────────────────────────────────────────────────────────────────
struct EMA {
    float val = 0; bool ready = false;
    uint16_t update(uint16_t raw) {
        float v = raw;
        if (!ready) { val = v; ready = true; }
        else val = EMA_ALPHA * v + (1.0f - EMA_ALPHA) * val;
        return (uint16_t)(val + 0.5f);
    }
};

// ── Chart ring buffer ──────────────────────────────────────────────────────
struct Chart {
    uint16_t buf[HALF_W] = {};
    int head = 0;
    void push(uint16_t v) { buf[head] = v; head = (head + 1) % HALF_W; }
};

// ── VL6180X register I/O ──────────────────────────────────────────────────
static void vl_write8(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(VL_ADDR);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(val);
    Wire.endTransmission();
}

static uint8_t vl_read8(uint16_t reg) {
    Wire.beginTransmission(VL_ADDR);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)VL_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static bool vlPresent = false;

static void vlInit() {
    if (vl_read8(0x0000) != 0xB4) {
        Serial.println("VL6180X not found");
        return;
    }
    if (vl_read8(0x0016) & 0x01) {
        // Mandatory private registers (ST app note AN4545)
        vl_write8(0x0207,0x01); vl_write8(0x0208,0x01);
        vl_write8(0x0096,0x00); vl_write8(0x0097,0xFD);
        vl_write8(0x00E3,0x00); vl_write8(0x00E4,0x04);
        vl_write8(0x00E5,0x02); vl_write8(0x00E6,0x01);
        vl_write8(0x00E7,0x03); vl_write8(0x00F5,0x02);
        vl_write8(0x00D9,0x05); vl_write8(0x00DB,0xCE);
        vl_write8(0x00DC,0x03); vl_write8(0x00DD,0xF8);
        vl_write8(0x009F,0x00); vl_write8(0x00A3,0x3C);
        vl_write8(0x00B7,0x00); vl_write8(0x00BB,0x3C);
        vl_write8(0x00B2,0x09); vl_write8(0x00CA,0x09);
        vl_write8(0x0198,0x01); vl_write8(0x01B0,0x17);
        vl_write8(0x01AD,0x00); vl_write8(0x00FF,0x05);
        vl_write8(0x0100,0x05); vl_write8(0x0199,0x05);
        vl_write8(0x01A6,0x1B); vl_write8(0x01AC,0x3E);
        vl_write8(0x01A7,0x1F); vl_write8(0x0030,0x00);
        vl_write8(0x0016,0x00);  // clear fresh-out-of-box
    }
    // Recommended public settings
    vl_write8(0x0011,0x10);  // avg sample period
    vl_write8(0x010A,0x30);  // analogue gain
    vl_write8(0x003F,0x46);  // range check enables
    vl_write8(0x0031,0xFF);  // max convergence time
    vl_write8(0x0040,0x63);  // range ignore threshold
    vl_write8(0x002E,0x01);
    vl_write8(0x001B,0x09);  // intermeasurement period
    vl_write8(0x003E,0x31);
    vl_write8(0x0014,0x24);  // interrupt on new sample ready
    vlPresent = true;
    Serial.println("VL6180X init OK");
}

static void    vlStartRange() { vl_write8(0x0018, 0x01); }
static int16_t vlReadRange()  {   // -1 = not ready; 255 = no target
    if (!(vl_read8(0x004F) & 0x04)) return -1;
    uint8_t r = vl_read8(0x0062);
    vl_write8(0x0015, 0x07);
    return r;  // 255 means out-of-range; caller discards it
}

// ── Draw one display half ──────────────────────────────────────────────────
static void drawHalf(int xOff, const char* label, uint16_t dist, bool valid, Chart& ch) {
    // Label top-left
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(xOff, 11, label);

    // Distance top-right
    if (valid) {
        char num[12];
        snprintf(num, sizeof(num), "%d mm", dist);
        display.setFont(u8g2_font_8x13_tf);
        display.drawStr(xOff + HALF_W - display.getStrWidth(num), 13, num);
    } else {
        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(xOff + HALF_W/2 - 5, 11, "---");
    }

    display.drawHLine(xOff, CHART_Y - 1, HALF_W);

    // Scrolling heartbeat waveform
    for (int i = 0; i < HALF_W - 1; i++) {
        int d0 = (i     - ch.head + HALF_W) % HALF_W;
        int d1 = (i + 1 - ch.head + HALF_W) % HALF_W;
        if (d0 < 3 || d1 < 3) continue;  // 3-px cursor gap

        uint16_t v0 = min(ch.buf[i],     (uint16_t)CHART_MAX);
        uint16_t v1 = min(ch.buf[i + 1], (uint16_t)CHART_MAX);

        int y0 = CHART_Y + CHART_H - 1 - (int)((long)v0 * (CHART_H - 1) / CHART_MAX);
        int y1 = CHART_Y + CHART_H - 1 - (int)((long)v1 * (CHART_H - 1) / CHART_MAX);

        display.drawLine(xOff + i, y0, xOff + i + 1, y1);
    }
}

// ── State ─────────────────────────────────────────────────────────────────
EMA   uartEma, i2cEma;
Chart uartChart, i2cChart;

uint16_t uartDist = 0; bool uartValid = false; uint32_t uartLast = 0;
uint16_t i2cDist  = 0; bool i2cValid  = false; uint32_t i2cLast  = 0;

// ─────────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    delay(1000);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    display.begin();
    display.clearBuffer();
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(20, 35, "Initialising...");
    display.sendBuffer();

    vlInit();

    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (Serial1.available()) Serial1.read();

    Serial.println("Ready — UART(left) | I2C(right)");
}

void loop() {
    updateLED();

    // ── UART: Modbus RTU stream ────────────────────────────────────────────
    static uint8_t mbuf[7]; static int midx = 0;
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        mbuf[midx] = b;
        if (midx == 0 && b != 0x01) continue;
        if (midx == 1 && b != 0x03) { midx = 0; continue; }
        if (midx == 2 && b != 0x02) { midx = 0; continue; }
        midx++;
        if (midx < 7) continue;
        midx = 0;

        uint16_t rxCRC = mbuf[5] | ((uint16_t)mbuf[6] << 8);
        if (rxCRC != modbusCRC(mbuf, 5)) continue;

        uint16_t raw = ((uint16_t)mbuf[3] << 8) | mbuf[4];
        if (raw > 5000) continue;  // reject corrupt frames
        uint16_t cal = (raw > CALIB_UART) ? raw - CALIB_UART : 0;
        uartDist  = uartEma.update(cal);
        uartValid = true;
        uartLast  = millis();
        uartChart.push(uartDist);

        Serial.printf("UART %4d mm   I2C %4d mm\n", uartDist, i2cDist);
    }
    if (uartValid && millis() - uartLast > 2000) uartValid = false;

    // ── I2C: VL6180X — state machine, rate-limited to ~10 Hz ─────────────
    if (vlPresent) {
        static enum { IDLE, MEASURING } vlState = IDLE;
        static uint32_t vlNextTrigger = 0;

        if (vlState == IDLE && millis() >= vlNextTrigger) {
            vlStartRange();
            vlState = MEASURING;
        }
        if (vlState == MEASURING) {
            int16_t r = vlReadRange();
            if (r >= 0) {           // measurement complete (may be 255 = no target)
                if (r < 255) {
                    uint16_t cal = ((uint16_t)r > CALIB_I2C) ? (uint16_t)r - CALIB_I2C : 0;
                    i2cDist  = i2cEma.update(cal);
                    i2cValid = true;
                    i2cLast  = millis();
                    i2cChart.push(i2cDist);
                }
                vlState = IDLE;
                vlNextTrigger = millis() + 100;
            }
        }
    }
    if (i2cValid && millis() - i2cLast > 2000) i2cValid = false;

    // ── Display ────────────────────────────────────────────────────────────
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 100) {
        lastDraw = millis();
        display.clearBuffer();
        drawHalf(0,     "UART", uartDist, uartValid, uartChart);
        drawHalf(R_OFF, "I2C",  i2cDist,  i2cValid,  i2cChart);
        display.drawVLine(63, 0, 64);
        display.drawVLine(64, 0, 64);
        display.sendBuffer();
    }
}
