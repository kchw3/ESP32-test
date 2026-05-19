#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#define LED_PIN       15
#define SDA_PIN       22
#define SCL_PIN       23
#define RX_PIN        17
#define TX_PIN        16

#define CALIB_OFFSET  0       // mm to subtract from raw reading
#define EMA_ALPHA     0.5f    // 0=frozen, 1=raw; 0.5 ≈ 200ms settling
#define CHART_MAX_MM  300     // full-scale distance for the waveform

#define CHART_Y  15           // chart top row
#define CHART_H  (64 - CHART_Y)  // 49 px
#define CHART_W  128

// Swap to U8G2_SH1106_128X64_NONAME_F_HW_I2C if display is blank (1.3" panel)
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
    static uint32_t last = 0;
    static bool on = false;
    if (millis() - last >= 500) {
        last = millis();
        on = !on;
        digitalWrite(LED_PIN, on ? LOW : HIGH);
    }
}

// ── EMA ───────────────────────────────────────────────────────────────────
static float emaVal   = 0;
static bool  emaReady = false;

static uint16_t applyEMA(uint16_t raw) {
    float v = (float)raw;
    if (!emaReady) { emaVal = v; emaReady = true; }
    else emaVal = EMA_ALPHA * v + (1.0f - EMA_ALPHA) * emaVal;
    return (uint16_t)(emaVal + 0.5f);
}

// ── Chart ring buffer (index == screen x) ─────────────────────────────────
static uint16_t chartBuf[CHART_W] = {0};
static int      chartHead = 0;  // next write position

static void pushChart(uint16_t v) {
    chartBuf[chartHead] = v;
    chartHead = (chartHead + 1) % CHART_W;
}

// ── OLED render ───────────────────────────────────────────────────────────
static void drawDisplay(uint16_t dist, bool valid) {
    display.clearBuffer();

    // Top strip: label left, distance right
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(0, 11, "TOF050F");

    if (valid) {
        char num[16];
        snprintf(num, sizeof(num), "%d mm", dist);
        display.setFont(u8g2_font_8x13_tf);
        display.drawStr(128 - display.getStrWidth(num), 13, num);
    } else {
        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(60, 11, "no signal");
    }

    display.drawHLine(0, CHART_Y - 1, 128);

    // Scrolling waveform: chartBuf[x] maps directly to screen column x.
    // chartHead is the next write position (oldest data).
    // A 3-pixel gap at chartHead acts as the scan-line cursor.
    for (int x = 0; x < CHART_W - 1; x++) {
        int d0 = (x     - chartHead + CHART_W) % CHART_W;
        int d1 = (x + 1 - chartHead + CHART_W) % CHART_W;
        if (d0 < 3 || d1 < 3) continue;  // cursor gap

        uint16_t v0 = min(chartBuf[x],     (uint16_t)CHART_MAX_MM);
        uint16_t v1 = min(chartBuf[x + 1], (uint16_t)CHART_MAX_MM);

        int y0 = CHART_Y + CHART_H - 1 - (int)((long)v0 * (CHART_H - 1) / CHART_MAX_MM);
        int y1 = CHART_Y + CHART_H - 1 - (int)((long)v1 * (CHART_H - 1) / CHART_MAX_MM);

        display.drawLine(x, y0, x + 1, y1);
    }

    display.sendBuffer();
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    delay(1000);

    Wire.begin(SDA_PIN, SCL_PIN);
    display.begin();
    display.setFont(u8g2_font_5x7_tf);
    display.clearBuffer();
    display.drawStr(20, 35, "Initialising...");
    display.sendBuffer();

    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (Serial1.available()) Serial1.read();

    Serial.println("TOF050F + OLED ready");
    Serial.println("raw  calib  ema (mm)");
}

void loop() {
    updateLED();

    static uint8_t  buf[7];
    static int      idx      = 0;
    static uint16_t lastDist = 0;
    static bool     valid    = false;
    static uint32_t lastFrame = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        buf[idx] = b;

        if (idx == 0 && b != 0x01) continue;
        if (idx == 1 && b != 0x03) { idx = 0; continue; }
        if (idx == 2 && b != 0x02) { idx = 0; continue; }

        idx++;
        if (idx < 7) continue;
        idx = 0;

        uint16_t rxCRC   = buf[5] | ((uint16_t)buf[6] << 8);
        uint16_t calcCRC = modbusCRC(buf, 5);
        if (rxCRC != calcCRC) continue;

        uint16_t raw   = ((uint16_t)buf[3] << 8) | buf[4];
        uint16_t calib = (raw > CALIB_OFFSET) ? raw - CALIB_OFFSET : 0;
        uint16_t ema   = applyEMA(calib);

        lastDist  = ema;
        valid     = true;
        lastFrame = millis();

        pushChart(ema);

        Serial.printf("[%7lu ms]  raw=%4d  calib=%4d  ema=%4d mm\n",
                      millis(), raw, calib, ema);
    }

    if (valid && millis() - lastFrame > 2000) valid = false;

    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 50) {
        lastDraw = millis();
        drawDisplay(lastDist, valid);
    }
}
