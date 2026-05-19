#include <Arduino.h>
#include <Wire.h>

#define SDA_PIN  22   // D4
#define SCL_PIN  23   // D5
#define LED_PIN  15   // onboard orange LED (active-LOW)

// ── LED heartbeat ──────────────────────────────────────────────────────────
void updateLED() {
  static uint32_t last = 0;
  static bool state = false;
  if (millis() - last >= 500) {
    last = millis();
    state = !state;
    digitalWrite(LED_PIN, state ? LOW : HIGH);
  }
}

// ── VL6180X: 16-bit register read ─────────────────────────────────────────
uint8_t vl6180x_read8(uint8_t addr, uint16_t reg) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// ── OLED driver detection ──────────────────────────────────────────────────
void probeOLED(uint8_t addr) {
  Wire.requestFrom(addr, (uint8_t)3);
  uint8_t b[3] = {0xFF, 0xFF, 0xFF};
  for (int i = 0; Wire.available() && i < 3; i++) b[i] = Wire.read();
  Serial.printf("    Status bytes (raw): 0x%02X 0x%02X 0x%02X\n", b[0], b[1], b[2]);
  uint8_t s = b[0];
  if      (s == 0xFF)           Serial.println("    Chip: unreadable");
  else if ((s & 0xC0) == 0x00)  Serial.println("    Chip guess: SSD1306");
  else if ((s & 0xC0) == 0x40)  Serial.println("    Chip guess: SH1106");
  else                           Serial.printf ("    Chip guess: uncertain (0x%02X)\n", s);
  Serial.println("    Note: 1.3\" panels are almost always SH1106.");
}

// ── VL6180X register probe ─────────────────────────────────────────────────
void probeVL6180X(uint8_t addr) {
  uint8_t id    = vl6180x_read8(addr, 0x0000);
  uint8_t revMj = vl6180x_read8(addr, 0x0001);
  uint8_t revMn = vl6180x_read8(addr, 0x0002);
  uint8_t fresh = vl6180x_read8(addr, 0x0016);
  Serial.printf("    Model ID  : 0x%02X  %s\n", id,
                id == 0xB4 ? "(VL6180X confirmed ✓)" : "(unexpected — check wiring)");
  Serial.printf("    Model Rev : %d.%d\n", revMj, revMn);
  Serial.printf("    Fresh-out : %s\n", (fresh & 0x01) ? "yes" : "no");
}

// ── Known device labels ────────────────────────────────────────────────────
const char* knownDevice(uint8_t addr) {
  switch (addr) {
    case 0x29: return "VL6180X / TOF050F";
    case 0x3C: return "OLED (SSD1306/SH1106) — 7-bit of 0x78";
    case 0x3D: return "OLED alt addr";
    case 0x68: return "IMU (MPU6050/ICM-42688)";
    case 0x69: return "IMU alt addr";
    case 0x76:
    case 0x77: return "BME280/BMP280";
    default:   return "";
  }
}

// ── Clean single-address probe ─────────────────────────────────────────────
// Probes CONFIRMATIONS times with a settle delay between each.
// No Wire re-init — that causes IDF 5.x phantom ACKs.
#define CONFIRMATIONS 5
#define PROBE_DELAY_MS 10

bool probeAddr(uint8_t addr) {
  for (int i = 0; i < CONFIRMATIONS; i++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission(true) != 0) return false;  // any miss = not real
    delay(PROBE_DELAY_MS);
  }
  return true;
}

// ── Full I2C scan ──────────────────────────────────────────────────────────
int runScan(bool detailed) {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    updateLED();
    if (!probeAddr(addr)) continue;
    found++;
    const char* label = knownDevice(addr);
    if (strlen(label) > 0) Serial.printf("  [0x%02X] %s\n", addr, label);
    else                    Serial.printf("  [0x%02X] Unknown\n", addr);
    if (detailed) {
      if (addr == 0x3C || addr == 0x3D) probeOLED(addr);
      if (addr == 0x29)                 probeVL6180X(addr);
      Serial.println();
    }
  }
  return found;
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);   // LED on while booting

  Serial.begin(115200);
  delay(2000);

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  XIAO ESP32C6 — I2C Scanner v3         ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.printf("SDA=GPIO%d(D4)  SCL=GPIO%d(D5)\n", SDA_PIN, SCL_PIN);
  Serial.printf("Confirmations per address: %d  Delay: %dms\n\n",
                CONFIRMATIONS, PROBE_DELAY_MS);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("Running detailed scan...\n");
  int found = runScan(true);
  Serial.printf("────────────────────────────────────────\n");
  Serial.printf("Scan complete: %d confirmed device(s)\n\n", found);
}

void loop() {
  updateLED();
  static uint32_t lastScan = 0;
  if (millis() - lastScan >= 10000) {
    lastScan = millis();
    Serial.println("--- Quick rescan ---");
    int found = runScan(false);
    Serial.printf("    %d confirmed device(s)\n", found);
  }
}
