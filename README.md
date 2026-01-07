# ESP32 GPS Broadcast System

A multi-protocol GPS data distribution system built on ESP32. This project reads GNSS data from a u-blox module and distributes it via TCP, HTTP, and ESP-NOW to connected clients and remote displays.

![Dashboard](/images/dashboard.png)

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Dependencies](#software-dependencies)
- [Installation](#installation)
  - [GPS Sender Setup](#gps-sender-setup)
  - [Receiver Setup](#receiver-setup)
- [Configuration](#configuration)
- [Usage](#usage)
- [Web Dashboard](#web-dashboard)
- [ESP-NOW Protocol](#esp-now-protocol)
- [TCP Server Protocol](#tcp-server-protocol)
- [Directory Structure](#directory-structure)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Overview

This system consists of two main components:

1. **GPS Sender**: An ESP32-based device that reads GPS data from a u-blox GNSS receiver and broadcasts it through multiple channels simultaneously.

2. **Receivers**: ESP32-based display devices that receive GPS telemetry via ESP-NOW and present it on LCD/AMOLED screens. Two receiver configurations are provided for different Waveshare display modules.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      GPS Sender                             │
│                   (XIAO ESP32-S3)                           │
│                                                             │
│  ┌──────────┐    ┌──────────────────────────────────────┐   │
│  │  u-blox  │ ─> │  GPS Logic                           │   │
│  │  GNSS    │    │  - Position, Speed, Heading          │   │
│  │  Module  │    │  - Satellite Info                    │   │
│  └──────────┘    │  - DOP Values                        │   │
│                  │  - Accuracy Estimates                │   │
│                  └──────────────────────────────────────┘   │
│                              │                              │
│           ┌──────────────────┼──────────────────┐           │
│           ▼                  ▼                  ▼           │
│    ┌────────────┐    ┌────────────┐    ┌────────────┐       │
│    │ TCP Server │    │ Web Server │    │  ESP-NOW   │       │
│    │ Port 2947  │    │  Port 80   │    │ Broadcast  │       │
│    │ NMEA/GPSD  │    │ Dashboard  │    │            │       │
│    └────────────┘    └────────────┘    └────────────┘       │
│                                              │              │
└──────────────────────────────────────────────│──────────────┘
                                               │
                    ┌──────────────────────────┴───────────────┐
                    │                                          │
                    ▼                                          ▼
┌─────────────────────────────────────┐   ┌──────────────────────────────────────┐
│    Receiver #1: ESP32-C6 LCD        │   │  Receiver #2: ESP32-S3 AMOLED        │
│                                     │   │                                      │
│  ┌───────────────────────────────┐  │   │  ┌────────────────────────────────┐  │
│  │  ESP-NOW Receiver             │  │   │  │  ESP-NOW Receiver              │  │
│  │  - Packet validation          │  │   │  │  - Packet validation           │  │
│  │  - Pong response sender       │  │   │  │  - Pong response sender        │  │
│  └───────────────┬───────────────┘  │   │  └───────────────┬────────────────┘  │
│                  │                  │   │                  │                   │
│       ┌──────────┼────────────┐     │   │       ┌──────────┼─────────────┐     │
│       ▼          ▼            ▼     │   │       ▼          ▼             ▼     │
│  ┌─────────┐ ┌──────┐ ┌──────────┐  │   │  ┌──────────┐ ┌──────┐ ┌──────────┐  │
│  │ Home    │ │ WiFi │ │ Web      │  │   │  │ Home     │ │ WiFi │ │ Web      │  │
│  │ Assist. │ │      │ │ Server   │  │   │  │ Assist.  │ │      │ │ Server   │  │
│  │ API     │ │      │ │ Port 80  │  │   │  │ API      │ │      │ │ Port 80  │  │
│  └─────────┘ └──────┘ └──────────┘  │   │  └──────────┘ └──────┘ └──────────┘  │
│                  │                  │   │                  │                   │
│                  ▼                  │   │                  ▼                   │
│  ┌───────────────────────────────┐  │   │  ┌────────────────────────────────┐  │
│  │  LVGL Display Engine          │  │   │  │  LVGL Display Engine           │  │
│  │  - 4 Pages (Home, Accuracy,   │  │   │  │  - 4 Pages (Home, Accuracy,    │  │
│  │    Speed, Network Info)       │  │   │  │    Speed, Network Info)        │  │
│  │  - Dynamic satellite bar      │  │   │  │  - Dynamic satellite bar       │  │
│  │  - Real-time GPS data         │  │   │  │  - Real-time GPS data          │  │
│  │  - Status LED indicator       │  │   │  │  - Status LED indicator        │  │
│  └───────────────┬───────────────┘  │   │  └───────────────┬────────────────┘  │
│                  │                  │   │                  │                   │
│                  ▼                  │   │       ┌──────────┴──────────┐        │
│  ┌───────────────────────────────┐  │   │       ▼                     ▼        │
│  │  ST7789V Display Driver       │  │   │  ┌──────────┐  ┌──────────────────┐  │
│  │  - 172x320 SPI LCD            │  │   │  │ FT5x06   │  │ MIPI SPI AMOLED  │  │
│  │  - Backlight control          │  │   │  │ Touch    │  │ 368x448 Display  │  │
│  │  - Rotation support           │  │   │  │ Screen   │  │ - Brightness Ctl │  │
│  └───────────────────────────────┘  │   │  │          │  │ - Rotation Ctl   │  │
│                                     │   │  └──────────┘  └──────────────────┘  │
│  ┌───────────────────────────────┐  │   │                                      │
│  │  Persistent Storage           │  │   │  ┌────────────────────────────────┐  │
│  │  - Display settings           │  │   │  │  Physical Controls             │  │
│  │  - LED preferences            │  │   │  │  - Brightness cycle button     │  │
│  │  - Page state                 │  │   │  │  - Page navigation button      │  │
│  └───────────────────────────────┘  │   │  │  - PCA9554 GPIO expander       │  │
│                                     │   │  └────────────────────────────────┘  │
│  ┌───────────────────────────────┐  │   │                                      │
│  │  RGB LED (SK6812)             │  │   │  ┌────────────────────────────────┐  │
│  │  - Configurable idle color    │  │   │  │  Persistent Storage            │  │
│  │  - ESP-NOW activity blink     │  │   │  │  - Display settings            │  │
│  │  - Brightness control         │  │   │  │  - Page state                  │  │
│  └───────────────────────────────┘  │   │  │  - LED blink duration          │  │
│                                     │   │  └────────────────────────────────┘  │
└─────────────────────────────────────┘   └──────────────────────────────────────┘
```

## Features

### GPS Sender

- **Multi-Protocol Output**
  - TCP server on port 2947 (GPSD-compatible JSON and NMEA sentences)
  - HTTP web dashboard with real-time updates
  - ESP-NOW broadcast for low-latency wireless display updates

- **GPS Data**
  - Latitude, Longitude, Altitude (HAE and MSL)
  - Ground speed and heading
  - Satellite count (used and visible)
  - Fix type and status
  - DOP values (PDOP, HDOP, VDOP)
  - Horizontal and vertical accuracy estimates
  - UTC and local time calculation

- **Statistics Tracking**
  - Min/max altitude
  - Maximum speed
  - Maximum satellites
  - Minimum DOP values
  - Minimum accuracy values
  - Persistent storage in flash memory

- **Network Features**
  - Dual-mode WiFi (Station + Access Point)
  - WiFi credential storage and web-based configuration
  - OTA firmware updates via ElegantOTA

- **Hardware Indicators**
  - Configurable LED modes (off, on, blink on read, blink on fix, blink on movement)

### Receivers

- Real-time GPS data display via LVGL
- Multiple display pages (Home, Accuracy, Speed, Network Info)
- Ping-pong connection monitoring with the sender
- Home Assistant integration via ESPHome
- Configurable display brightness and rotation
- Physical button support for page navigation

## Hardware Requirements

### GPS Sender

| Component | Specification |
|-----------|---------------|
| MCU | Seeed Studio XIAO ESP32-S3 (or compatible ESP32) |
| GNSS Module | u-blox NEO-M9N, NEO-M8N, or compatible |
| Connection | I2C (SDA: GPIO5, SCL: GPIO6 by default) |
| LED | Onboard LED on GPIO21 |

### Receiver Option 1: ESP32-C6 with 1.47" LCD

| Component | Specification |
|-----------|---------------|
| Board | Waveshare ESP32-C6-LCD-1.47 |
| Display | ST7789 172x320 |
| Interface | SPI |

### Receiver Option 2: ESP32-S3 with 1.8" AMOLED

| Component | Specification |
|-----------|---------------|
| Board | Waveshare ESP32-S3-Touch-AMOLED-1.8 |
| Display | MIPI SPI AMOLED 368x448 |
| Touch | FT5x06 capacitive touch |
| Interface | Quad SPI |

## Software Dependencies

### GPS Sender (Arduino)

Install via Arduino Library Manager:

| Library | Version | Purpose |
|---------|---------|---------|
| SparkFun u-blox GNSS Arduino Library | v2.2.28+ | GNSS communication |
| ESPAsyncWebServer | Latest | HTTP server |
| AsyncTCP | Latest | Async TCP support |
| ArduinoJson | v7+ | JSON serialization |
| ElegantOTA | Latest | OTA updates |

### Receivers (ESPHome)

- ESPHome 2024.1.0 or newer
- ESP-IDF framework

## Installation

### GPS Sender Setup

1. **Clone the repository**

```bash
git clone https://github.com/yourusername/esp32-gps-broadcast.git
cd esp32-gps-broadcast
```

2. **Configure hardware pins** in `GPS Sender - XIAO ESP32S3/Config.h`:

```cpp
// I2C pins for GNSS module
#define I2C_SDA 5
#define I2C_SCL 6

// LED pin
#define LED_PIN 21
```

3. **Configure WiFi** (optional) in `Config.h`:

```cpp
const char* const WIFI_SSID = "YourNetworkSSID";
const char* const WIFI_PASS = "YourNetworkPassword";
```

4. **Configure ESP-NOW receivers** in `EspNowSender.cpp`:

```cpp
// Replace with your receiver MAC addresses
uint8_t broadcastAddress1[] = {0xCC, 0xBA, 0x97, 0xF3, 0xC7, 0x28};
uint8_t broadcastAddress2[] = {0x30, 0xED, 0xA0, 0xAE, 0x0D, 0x60};
```

5. **Build and upload** using Arduino IDE or the provided PowerShell script:

```powershell
# Using the build script
cd "GPS Sender - XIAO ESP32S3"
.\compile-and-upload.ps1 -ComPort "COM11"

# Or compile only for OTA update
.\compile-and-upload.ps1 -SkipUpload -OutputFolder "C:\path\to\output"
```

### Receiver Setup

1. **Install ESPHome**

```bash
pip install esphome
```

2. **Configure the receiver YAML**

For ESP32-C6 LCD:
```bash
cd receiver-ESP32-C6-LCD-1.47
```

For ESP32-S3 AMOLED:
```bash
cd receiver-ESP32-S3-Touch-AMOLED-1.8
```

3. **Update the sender MAC address** in the YAML file:

```yaml
espnow:
  peers:
    - "10:B4:1D:EA:E0:68"  # Replace with your sender's MAC
```

4. **Update WiFi credentials**:

```yaml
wifi:
  ssid: "YourNetworkSSID"
  password: "YourNetworkPassword"
```

5. **Flash the receiver**:

```bash
esphome run receiver-ESP32-C6-LCD-1.47.yaml
# or
esphome run receiver-ESP32-S3-Touch-AMOLED-1.8.yaml
```

## Configuration

### GPS Sender Configuration Options

| Parameter | Default | Description |
|-----------|---------|-------------|
| `WIFI_SSID` | Empty | Station mode WiFi SSID |
| `WIFI_PASS` | Empty | Station mode WiFi password |
| `AP_SSID` | "ESP32-GPS" | Access Point SSID |
| `AP_PASS` | NULL | Access Point password (open if NULL) |
| `TCP_PORT` | 2947 | TCP server port |
| `WEB_PORT` | 80 | HTTP server port |
| `I2C_SDA` | 5 | I2C data pin |
| `I2C_SCL` | 6 | I2C clock pin |
| `LED_PIN` | 21 | Status LED pin |

### Runtime Configuration

The following can be changed via the web interface:

- **LED Mode**: Off, On, Blink on GPS Read, Blink on Fix, Blink on Movement
- **Update Rate**: 1s, 5s, 10s, 30s
- **WiFi Credentials**: Can be updated and saved to flash

## Usage

### Accessing the Web Dashboard

1. Connect to the ESP32's access point (`ESP32-GPS` by default) or ensure both devices are on the same network.

2. Navigate to the device IP address in a web browser:
   - Access Point mode: `http://192.168.4.1`
   - Station mode: Check serial output for assigned IP

### Connecting TCP Clients

The TCP server on port 2947 supports two modes:

**GPSD Mode** (send `?WATCH={"enable":true}` to activate):
- Returns JSON TPV (Time-Position-Velocity) messages

**NMEA Mode** (default):
- Returns standard NMEA sentences (GPRMC, GPGGA, GPGSA)

Example using netcat:
```bash
nc 192.168.1.100 2947
```

### OTA Updates

1. Navigate to `http://<device-ip>/update`
2. Select the firmware binary file
3. Wait for upload and automatic reboot

## Web Dashboard

The web dashboard provides:

- **Position Panel**: Latitude, longitude, altitude with copy buttons
- **Navigation Panel**: Compass display, speed, accuracy values
- **Signal Quality Panel**: Satellite counts, DOP values, uptime
- **Configuration Panel**: LED mode, update rate, network info
- **Map Panel**: Simple world map with position indicator
- **WiFi Setup Panel**: Network scanning and credential storage
- **System Panel**: ESP-NOW status, statistics reset, OTA access, reboot

## ESP-NOW Protocol

### Packet Structure (Sender to Receiver)

```cpp
struct GpsEspNowPacket {
  double lat;           // Latitude in degrees
  double lon;           // Longitude in degrees
  float alt;            // Altitude in meters
  float speed;          // Ground speed in m/s
  float heading;        // Heading in degrees
  uint8_t sats;         // Satellites used
  uint8_t satsVisible;  // Satellites visible
  uint8_t fixType;      // 0=No fix, 1=DR, 2=2D, 3=3D
  char localTime[10];   // Local time string "HH:MM:SS"
  float pdop;           // Position DOP
  float hdop;           // Horizontal DOP
  float vdop;           // Vertical DOP
  float hAcc;           // Horizontal accuracy (m)
  float vAcc;           // Vertical accuracy (m)
  uint32_t stationIp;   // Sender's IP address
  uint32_t pingCounter; // Sequence number
};
```

### Pong Response (Receiver to Sender)

```cpp
struct PongPacket {
  uint32_t pingCounter; // Echo of received ping counter
};
```

### Connection Monitoring

- Sender increments `pingCounter` with each transmission
- Receivers echo back the counter in a pong response
- Sender tracks last response time per receiver
- Receivers marked inactive after 10 second timeout

## TCP Server Protocol

### NMEA Output (Default)

Standard NMEA 0183 sentences:

- **GPRMC**: Recommended minimum specific GPS data
- **GPGGA**: GPS fix data
- **GPGSA**: GPS DOP and active satellites

### GPSD JSON Output

Send `?WATCH={"enable":true}` to receive JSON messages:

```json
{
  "class": "TPV",
  "device": "/dev/i2c",
  "mode": 3,
  "time": "2024-01-15T12:34:56Z",
  "lat": 38.758140,
  "lon": -90.571820,
  "alt": 152.300,
  "speed": 0.520,
  "track": 45.00
}
```

## Directory Structure

```
.
├── GPS Sender - XIAO ESP32S3/
│   ├── GPS Sender - XIAO ESP32S3.ino  # Main sketch
│   ├── Config.h                        # Hardware configuration
│   ├── Types.h                         # Data structures
│   ├── Context.h                       # Global state declarations
│   ├── GpsLogic.cpp/.h                 # GNSS polling and parsing
│   ├── EspNowSender.cpp/.h             # ESP-NOW broadcast logic
│   ├── TcpServer.cpp/.h                # TCP socket server
│   ├── WebServer.cpp/.h                # HTTP server and dashboard
│   ├── LedControl.cpp/.h               # LED indicator control
│   ├── Storage.cpp/.h                  # Persistent statistics
│   └── compile-and-upload.ps1          # Build script
│
├── receiver-ESP32-C6-LCD-1.47/
│   ├── receiver-ESP32-C6-LCD-1.47.yaml # ESPHome configuration
│   └── huge_app.csv                    # Partition table
│
├── receiver-ESP32-S3-Touch-AMOLED-1.8/
│   ├── receiver-ESP32-S3-Touch-AMOLED-1.8.yaml
│   └── atkinson_48.c                   # Custom font
│
└── README.md
```

## Troubleshooting

### GPS Module Not Detected

- Verify I2C connections (SDA, SCL, VCC, GND)
- Check I2C address (default 0x42 for u-blox)
- Ensure adequate power supply to the GNSS module
- Check serial output for initialization messages

### No GPS Fix

- Ensure clear sky view for the antenna
- Wait up to 30 seconds for cold start acquisition
- Verify antenna connection if using external antenna
- Check that the GNSS module has battery backup for faster subsequent fixes

### ESP-NOW Not Working

- Verify MAC addresses match between sender and receiver configurations
- Ensure both devices are on the same WiFi channel (or channel 0 for auto)
- Check that `numReceivers` constant matches the number of configured receivers
- Monitor serial output for ESP-NOW initialization status

### Web Dashboard Not Loading

- Verify WiFi connection
- Check that port 80 is not blocked
- Try accessing via Access Point mode if station mode fails
- Clear browser cache and try again

### OTA Update Fails

- Ensure stable WiFi connection during update
- Verify binary file is correct for your board
- Check that flash has sufficient space
- Try reducing sketch size or using larger partition scheme