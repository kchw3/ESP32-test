# ESP32-test

A collection of test projects and sketches for the ESP32 microcontroller using the Arduino framework.

## Overview

This repository contains experimental code, examples, and prototypes for ESP32 development.

## Requirements

- ESP32 board
- [Arduino IDE](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
- ESP32 Arduino core: [arduino-esp32](https://github.com/espressif/arduino-esp32)

## Getting Started

1. Install the ESP32 board support in Arduino IDE:
   - Go to **File > Preferences**
   - Add the following URL to "Additional Board Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to **Tools > Board > Board Manager**, search for `esp32`, and install.

2. Open any `.ino` sketch from this repo in Arduino IDE.

3. Select your ESP32 board under **Tools > Board** and upload.

## Project Structure

```
ESP32-test/
├── README.md
└── (sketches and projects go here)
```

## Notes

- All code is written using the Arduino framework for ESP32.
- Each subfolder contains an independent sketch or project.
