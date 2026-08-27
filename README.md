# HealthTrack Pro

**A real-time patient monitoring system powered by ESP32 and FreeRTOS.**

## Features
- Monitors ECG, heart rate, SpO2, ambient temperature and humidity, and probe temperature using MAX30102, AD8232, DHT22, and DS18B20 sensors.
- Real-time multitasking using FreeRTOS with task prioritization and dual-core task pinning.
- Data visualization and cloud logging via ThingSpeak.
- Python-based ECG waveform plotting for sensor validation.

## Hardware Used
- ESP32 Dev Board
- MAX30102 (Pulse Oximeter & Heart Rate)
- AD8232 (ECG Sensor)
- DHT22 (Temperature & Humidity)
- DS18B20 (Digital Temperature Sensor)
- 4.7k ohm resistor for the DS18B20 data-line pull-up
- Jumper Wires, Breadboard

## ESP32 Wiring Used by the Firmware

| Component | Signal | ESP32 pin / setting |
|---|---|---|
| MAX30102 | SDA / SCL | GPIO 21 / GPIO 22 |
| I2C LCD | SDA / SCL | GPIO 21 / GPIO 22, address `0x27` (`0x3F` if required) |
| DHT22 | DATA | GPIO 4 |
| DS18B20 | DATA | GPIO 5 with a 4.7k ohm pull-up to 3.3V |
| AD8232 | OUTPUT | GPIO 34 |
| AD8232 | LO+ / LO- | GPIO 32 / GPIO 33 |

The MAX30102 and LCD share the I2C bus; the firmware protects that bus with a mutex.

## Software Stack
- PlatformIO (VS Code)
- C++ with FreeRTOS
- Python (for data validation)
- ThingSpeak (Cloud logging)
- Git & GitHub

## Architecture
- FreeRTOS tasks handle each sensor independently using core pinning and mutex-protected shared data.
- Sensor data is collected, processed, and published to the cloud.
- Real-time graphs plotted using Python for debugging ECG signals.

## Setup
1. Clone the repository:
```sh
git clone https://github.com/shrishail2728/HealthTrack-Pro.git
cd HealthTrack-Pro
```

2. Open with VS Code + PlatformIO extension.
3. Copy `include/secrets.example.h` to `include/secrets.h` and enter your Wi-Fi and ThingSpeak values. `include/secrets.h` is ignored and must not be committed.
4. Connect the ESP32 board.
5. Upload the code via PlatformIO.
6. Open the serial monitor at 115200 baud.
7. (Optional) Set `PORT` in `src/ecg_plot.py` and run it to plot ECG samples.

## Folder Structure
- `/src`: Main firmware code
- `/include`: Header files
- `/src/ecg_plot.py`: Python ECG plotting utility
- `platformio.ini`: Project Configuration file which includes library

## ThingSpeak Fields

| Field | Value |
|---|---|
| 1 | MAX30102 heart rate (BPM) |
| 2 | MAX30102 SpO2 (%) |
| 3 | AD8232 ECG ADC value |
| 4 | DHT22 ambient temperature (C) |
| 5 | DHT22 humidity (%) |
| 6 | DS18B20 probe temperature (C) |

## Cloud Integration
- Uses ThingSpeak REST API for posting real-time sensor data.
- Visual dashboards created on ThingSpeak channels.
