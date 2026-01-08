#ifndef CONFIG_H
#define CONFIG_H

// ================= USER CONFIGURATION =================
// WiFi Station Mode (Connect to existing router)
const char* const WIFI_SSID = "";
const char* const WIFI_PASS = "";

// Access Point Mode (Create own network)
const char* const AP_SSID = "ESP32-C6-GPS";
const char* const AP_PASS = NULL;

// Hardware definitions
// SparkFun Qwiic Pocket ESP32-C6 Pins
#define LED_PIN 23          // Blue STAT LED on GPIO 23
#define LED_BLINK_DURATION_MS 100

// I2C Pins for Qwiic Connector on ESP32-C6
#define I2C_SDA 6
#define I2C_SCL 7

// Network Ports
#define TCP_PORT 2947
#define WEB_PORT 80

#endif
