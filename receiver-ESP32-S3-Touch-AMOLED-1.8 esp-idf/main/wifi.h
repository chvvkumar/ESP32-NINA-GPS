#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize WiFi in AP+STA mode.
 * - Starts SoftAP "Amoled Fallback Hotspot" immediately
 * - Begins STA connection to "IoT" network
 * - Registers event handlers for auto-reconnect
 * Must be called before espnow_init().
 */
esp_err_t wifi_init(void);

/**
 * Get the current STA IP address as a string.
 * Returns "0.0.0.0" if not connected.
 */
const char* wifi_get_ip_str(void);

/**
 * Get the WiFi STA MAC address as a string "XX:XX:XX:XX:XX:XX".
 */
const char* wifi_get_mac_str(void);

/**
 * Returns true if STA is connected to the router.
 */
bool wifi_is_connected(void);

#endif // WIFI_H
