# ESP32-NINA-GPS

Lightweight firmware to provide GPS location and satellite status from a u-blox GNSS module to NINA. The ESP32 runs a TCP server (port 2947) and serves a web dashboard with live GPS and supports OTA updates.

![Dashboard](/images/dashboard.png)

**Key features:**
- TCP GPS stream on port 2947
- Web dashboard with satellite view and firmware update
- Station and Access Point WiFi modes

**Quick Start — Configure your ESP32 variant**

1. Hardware
- ESP32 board (any common variant: `ESP32 DevKitC`, `ESP32-WROOM-32`, `ESP32-S3`, etc.)
- u-blox GNSS module connected via I2C (SDA, SCL) and a status LED (optional)

2. Libraries
Install these libraries with the Arduino Library Manager or PlatformIO:
- SparkFun u-blox GNSS Arduino Library
- ESPAsyncWebServer and AsyncTCP (or AsyncTCP32 for ESP32-S3 where applicable)
- ArduinoJson
- ElegantOTA

3. Edit `Config.h`
- Set WiFi credentials for Station mode or leave blank to use AP mode.
- Configure pins to match your ESP32 variant:

```cpp
// Example: ESP32 DevKitC
#define LED_PIN 2
#define I2C_SDA 21
#define I2C_SCL 22

// WiFi (station)
const char* const WIFI_SSID = "your_ssid";
const char* const WIFI_PASS = "your_password";
```

4. Build & upload
- With Arduino IDE: select the correct ESP32 board/variant and upload the sketch `ESP32-NINA-GPS.ino`.

5. Connect
- After boot the device will attempt Station mode; if that fails it will fall back to AP mode.
- Visit the device IP in a browser to open the dashboard (see the screenshot above).
- Point NINA to the ESP32 IP on port 2947 to receive GPS data.

Support and notes
- Ensure I2C wiring and pull-ups are correct for your GNSS module.
- Some ESP32-S3 variants require `AsyncTCP32` instead of `AsyncTCP`.
