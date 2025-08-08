# Auto_Caller

![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/InzynierDomu/Auto_Caller/main.yml?logo=github&style=flat-square)
![GitHub release (latest SemVer)](https://img.shields.io/github/v/release/InzynierDomu/Auto_Caller?style=flat-square)
<a href="https://discord.gg/KmW6mHdg">![Discord](https://img.shields.io/discord/815929748882587688?logo=discord&logoColor=green&style=flat-square)</a>
![GitHub](https://img.shields.io/github/license/InzynierDomu/PhECMeter?style=flat-square)

## Description

Device for regular bell ringing and playing random audio files from SD card using ESP32.

## Features

- Cyclical bell ringing using two GPIO pins (for relays, buzzers, etc.).
- Audio playback triggered by a limit switch or button.
- Random audio file selection from `/records` directory on SD card.
- Configurable ringing interval and audio sampling rate via `config.txt`.
- I2S output for playing audio through external DAC or speaker.

<div align="center">
<h2>Support</h2>

<p>If any of my projects have helped you in your work, studies, or simply made your day better, you can buy me a coffee. <a href="https://buycoffee.to/inzynier-domu" target="_blank"><img src="https://buycoffee.to/img/share-button-primary.png" style="width: 195px; height: 51px" alt="Postaw mi kawę na buycoffee.to"></a></p>
</div>

## Required environment

- **Board**: ESP32 Dev Module  
- **Platform**: PlatformIO [video](https://platformio.org/)
- **Framework**: Arduino  

## Installation

### Parts

- ESP32
- 2 transistors to control the bell coils
- 48V DC power supply
- 48V DC to 5V DC converter

### 📌 Pinout

| Function          | ESP32 Pin |
|-------------------|-----------|
| SD Card – MISO    | GPIO 19   |
| SD Card – MOSI    | GPIO 23   |
| SD Card – SCK     | GPIO 18   |
| SD Card – CS      | GPIO 5    |
| I2S – BCLK        | GPIO 26   |
| I2S – WS          | GPIO 25   |
| I2S – DOUT        | GPIO 22   |
| Bell output 1     | GPIO 12   |
| Bell output 2     | GPIO 14   |
| Limit switch IN   | GPIO 4    |
| Limit switch GND  | GND       |

---

### 💾 SD Card Setup

1. Format the SD card as FAT32.
2. Create a folder named `/records` and add `.wav` files.
3. Create a `config.txt` file in the root of the card with two lines:
 ```
60000       ; ringing interval in milliseconds (e.g. 60000 = 60 sec)
16000       ; audio sample rate in Hz
```
4. Upload the code to ESP32 using PlatformIO or Arduino IDE.
5. Open Serial Monitor (`115200 baud`) to debug or test behavior.

## Usage

- After the configured time interval, the device starts the bell ringing sequence.
- If the limit switch is pressed during ringing, it stops and plays a random audio file.
- After playback, the system resets to idle and waits for the next interval.