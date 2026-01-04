#ifndef CONFIG_H
#define CONFIG_H

// ================= USER CONFIGURATION =================
// WiFi Station Mode (Connect to existing router)
const char* const WIFI_SSID = "IoT";
const char* const WIFI_PASS = "kkkkkkkk";

// Access Point Mode (Create own network)
const char* const AP_SSID = "ESP32-GPS";
const char* const AP_PASS = "kkkkkkkk";  // Set a password (min 8 chars) to avoid Windows blocking

// Hardware definitions
// QT Py ESP32-S3 uses the onboard NeoPixel. 
// We rely on PIN_NEOPIXEL and NEOPIXEL_POWER defined by the board variant.
#define LED_BLINK_DURATION_MS 100 

// I2C Pins - STEMMA QT connector on Wire1
#define I2C_SDA 41  // SDA1
#define I2C_SCL 40  // SCL1

// Network Ports
#define TCP_PORT 2947
#define WEB_PORT 8080

#endif
