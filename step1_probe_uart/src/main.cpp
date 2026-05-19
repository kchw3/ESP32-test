#include <Arduino.h>

#define LED_PIN  15   // active-LOW
#define TX_PIN   16   // D6 → TOF050F RXD
#define RX_PIN   17   // D7 ← TOF050F TXD

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

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    delay(2000);

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  TOF050F  Modbus RTU Distance Reader ║");
    Serial.println("╚══════════════════════════════════════╝");
    Serial.printf("RX=GPIO%d(D7)  TX=GPIO%d(D6)  115200 baud\n\n", RX_PIN, TX_PIN);

    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (Serial1.available()) Serial1.read();
}

void loop() {
    updateLED();

    // Frame: 01 03 02 [distH] [distL] [crcL] [crcH]
    static uint8_t buf[7];
    static int idx = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        buf[idx] = b;

        // Sync on 3-byte header 01 03 02
        if (idx == 0 && b != 0x01) continue;
        if (idx == 1 && b != 0x03) { idx = 0; continue; }
        if (idx == 2 && b != 0x02) { idx = 0; continue; }

        idx++;
        if (idx < 7) continue;
        idx = 0;

        uint16_t rxCRC   = buf[5] | ((uint16_t)buf[6] << 8);
        uint16_t calcCRC = modbusCRC(buf, 5);
        if (rxCRC != calcCRC) {
            Serial.printf("[%7lu ms]  CRC error (got %04X, expected %04X)\n",
                          millis(), rxCRC, calcCRC);
            continue;
        }

        uint16_t dist = ((uint16_t)buf[3] << 8) | buf[4];
        Serial.printf("[%7lu ms]  Distance: %4d mm\n", millis(), dist);
    }
}
