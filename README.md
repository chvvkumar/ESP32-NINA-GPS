# ESP32 GPS Broadcast & Web Server

A robust ESP32 firmware that bridges GNSS data to the network and other devices. It reads from a u-blox module, serves data via TCP (NetGPS) and a Web Dashboard, and broadcasts real-time telemetry via ESP-NOW to remote displays.

![Dashboard](/images/dashboard.png)

## Key Features

- **Multi-Protocol Distribution:**
  - **TCP Server (Port 2947):** Compatible with standard GPSD clients.
  - **Web Dashboard:** Real-time satellite view, location data, and signal strength.
  - **ESP-NOW:** Low-latency, connectionless broadcasting to nearby ESP32 receivers (e.g., dashboard displays).
- **WiFi Modes:** Automatic fallback from Station (client) to Access Point mode.
- **OTA Updates:** Update firmware wirelessly via the web interface (`/update`).
- **ESPHome Receiver:** Includes a reference configuration for an ESP32-C6 display receiver.

## Architecture

1.  **Sender (This Repository):**
    *   Connects to a u-blox NEO-M9N (or compatible) via I2C.
    *   Host IP: `2947` for raw NMEA/JSON streams.
    *   Host IP: `80` for Web UI.
    *   Broadcasts parsed GPS data packets via ESP-NOW.

2.  **Receiver:**
    *   Listens for ESP-NOW packets.
    *   Displays telemetry on a screen (e.g., Waveshare 1.47" LCD).
    *   Integrates with Home Assistant.

## Hardware Requirements

**Sender:**
- **MCU:** ESP32 (Standard, S3, C3, etc.)
- **GNSS:** u-blox module (e.g., NEO-M9N, M8N) supported by SparkFun's library.
- **Connection:** I2C (SDA/SCL).

**Receiver (Optional):**
- **MCU:** ESP32-C6 (or similar) with display support.
- **Display:** ST7789 or compatible (Reference config provided for Waveshare 1.47" LCD).

## Installation

### 1. Sender (Firmware)

**Dependencies:**
Install the following libraries via Arduino Library Manager or PlatformIO:
- `SparkFun u-blox GNSS Arduino Library`
- `ESPAsyncWebServer` & `AsyncTCP` (or `AsyncTCP32` for S3/C3)
- `ArduinoJson`
- `ElegantOTA`

**Configuration (`Config.h`):**
Edit `Config.h` to match your hardware and network:
```cpp
// Network
const char* const WIFI_SSID = "Your_SSID"; // Leave empty for AP Mode
const char* const WIFI_PASS = "Your_Pass";

// Pins (Check your board's pinout!)
#define I2C_SDA 21 // Common for ESP32 WROOM
#define I2C_SCL 22
```

**Build & Upload:**
Select your board in Arduino IDE/PlatformIO and upload `ESP32-NINA-GPS.ino`.

**Important Note for ESP32-S3 USB CDC Flashing:**
This project follows the [ESP32 CDC/DFU flashing guidelines](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html) for ESP32-S3:
- **USB Mode:** USB-OTG (TinyUSB)
- **USB CDC On Boot:** Enabled
- **Upload Mode:** UART0 / Hardware CDC
- After flashing for the **first time**, you need to **manually reset the device** (press RESET button)
- The Serial Monitor baud rate is set to **115200** (as configured in `Serial.begin(115200)`)

Using the PowerShell script `compile-and-upload.ps1` automatically applies these settings.

### 2. Receiver (ESPHome)

![Receiver Device](Receiver/Receiver.jpg)

A complete ESPHome configuration is provided in the `Receiver/` directory.

1.  Install [ESPHome](https://esphome.io/).
2.  Copy `Receiver/esphome_receiver.yaml` to your ESPHome configuration directory.
3.  Edit the YAML to set your WiFi credentials and encryption keys.
4.  Flash the receiver device:
    ```bash
    esphome run esphome_receiver.yaml
    ```
5.  *Note:* The receiver must add the Sender's MAC address in the `espnow -> peers` section if `auto_add_peer` is disabled, though the current config uses `auto_add_peer: true`.

## Usage

**Web Dashboard:**
Navigate to `http://<ESP32_IP>` to view status.

**TCP Stream:**
Connect via NetGPS/GPSD clients to `<ESP32_IP>:2947`.

**ESP-NOW:**
The device automatically broadcasts GPS updates. Ensure receivers are on the same WiFi channel (often channel 1 by default if not connected to WiFi, or the router's channel if connected).

## Directory Structure

- `ESP32-NINA-GPS.ino`: Main entry point.
- `Config.h`: User configuration.
- `GpsLogic.*`: GNSS polling and parsing.
- `EspNowSender.*`: ESP-NOW broadcast logic.
- `TcpServer.*`: TCP socket handling.
- `WebServer.*`: HTTP server and dashboard assets.
- `Receiver/`: ESPHome YAML configuration for receiver nodes.
