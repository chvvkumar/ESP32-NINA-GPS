#ifndef CONFIG_H
#define CONFIG_H

// ================= USER CONFIGURATION =================
// WiFi Station Mode (Connect to existing router)
const char* const WIFI_SSID = "";
const char* const WIFI_PASS = "";

// Access Point Mode (Create own network)
const char* const AP_SSID = "GPS-ESP32-AP";
const char* const AP_PASS = "kkkkkkkk";

// Hardware definitions
#define LED_PIN 2            
#define LED_BLINK_DURATION_MS 100 
#define I2C_SDA 21
#define I2C_SCL 22

// Network Ports
#define TCP_PORT 2947
#define WEB_PORT 80

#endif
