# ESP32-test Project — Claude Context

## Embedded Workbench Platform

- **Web Portal / API:** http://192.168.1.245:8080/
- **Device Slot:** SLOT2
- **Serial Port:** rfc2217://192.168.1.245:4002

## Platform Skills

Install the 12 Claude Code skills from https://github.com/SensorsIot/Universal-Embedded-Workbench:

```bash
cp -r .claude/skills/. ~/.claude/skills/
```

| Skill | Purpose |
|-------|---------|
| `esp-pio-handling` | PlatformIO build/flash/monitor lifecycle |
| `esp-idf-handling` | ESP-IDF build/flash/monitor |
| `esp32-test-harness` | Reset, NVS erase, captive portal control |
| `workbench-debug` | Remote GDB debugging via OpenOCD |
| `workbench-wifi` | WiFi AP/STA/scan/HTTP relay |
| `workbench-ble` | BLE scan/connect/write |
| `workbench-logging` | Serial monitor with pattern matching |
| `workbench-mqtt` | Mosquitto broker control |
| `workbench-test-handling` | Test progress tracking, UI prompts |
| `workbench-integration` | One-shot WiFi/OTA/BLE setup |
| `signal-generator` | RF source (Si5351, 8 kHz–160 MHz) |
| `fsd-writer` | Functional Specification Document generator |

## Key API Endpoints (base: http://192.168.1.245:8080)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/devices` | GET | List slots, chip IDs, debug ports |
| `/api/serial/reset` | POST | DTR/RTS reset |
| `/api/serial/monitor` | POST | Wait for serial pattern |
| `/api/debug/start` | POST | Launch OpenOCD |
| `/api/wifi/ap_start` | POST | Start SoftAP |
| `/api/wifi/sta_join` | POST | Join external network |
| `/api/ble/scan` | POST | BLE peripheral scan |
| `/api/ble/connect` | POST | Connect by MAC |
| `/api/ble/write` | POST | Write GATT characteristic |
| `/api/gpio/set` | POST | Drive Pi GPIO pin |
| `/api/firmware/upload` | POST | Upload firmware (multipart) |
| `/api/test/update` | POST | Test progress tracking |
| `/api/udplog` | GET | Fetch buffered UDP logs |

## Flashing Firmware (SLOT2)

```bash
esptool --port rfc2217://192.168.1.245:4002 --chip esp32 \
  --before default-reset --after no-reset \
  write-flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 firmware.bin
```

## Serial Monitor (SLOT2)

```python
import serial
ser = serial.serial_for_url("rfc2217://192.168.1.245:4002", baudrate=115200)
```

## Network Ports

| Port | Purpose |
|------|---------|
| 8080 | HTTP API / Web portal |
| 4002 | RFC2217 serial — SLOT2 |
| 3334+ | GDB remote debugging |
| 5555 | UDP debug logs |
