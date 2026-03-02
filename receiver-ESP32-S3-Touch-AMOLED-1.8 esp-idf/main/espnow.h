#ifndef ESPNOW_H
#define ESPNOW_H

#include "esp_err.h"

/**
 * Initialize ESP-NOW: register callbacks, add static peers.
 * Must be called AFTER wifi_init() (needs WiFi radio active).
 */
esp_err_t espnow_init(void);

/**
 * Check connection timeout. Call from main loop or timer (~1s).
 * Sets g_gps.connected = false if no packet in 30s.
 */
void espnow_check_timeout(void);

#endif // ESPNOW_H
