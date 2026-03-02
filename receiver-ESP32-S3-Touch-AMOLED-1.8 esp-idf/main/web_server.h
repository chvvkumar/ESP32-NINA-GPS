#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

/**
 * Start the HTTP web server on port 80.
 * Provides: GET /, GET /status, POST /ota, POST /reboot
 * Must be called after wifi_init().
 */
esp_err_t web_server_init(void);

#endif // WEB_SERVER_H
