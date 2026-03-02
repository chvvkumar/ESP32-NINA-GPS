#include "wifi.h"
#include "types.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

// ---------------------------------------------------------------------------
// Configuration (matches ESPHome YAML)
// ---------------------------------------------------------------------------
#define WIFI_STA_SSID       "IoT"
#define WIFI_STA_PASS       "kkkkkkkk"
#define WIFI_AP_SSID        "Amoled Fallback Hotspot"
#define WIFI_AP_PASS        "kkkkkkkk"
#define WIFI_MAX_RETRY      0  // 0 = retry forever

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool s_sta_connected = false;
static char s_ip_str[16]    = "0.0.0.0";
static char s_mac_str[18]   = "00:00:00:00:00:00";

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting to '%s'...", WIFI_STA_SSID);
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected to '%s'", WIFI_STA_SSID);
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                s_sta_connected = false;
                strncpy(s_ip_str, "0.0.0.0", sizeof(s_ip_str));
                ESP_LOGW(TAG, "STA disconnected, reconnecting...");
                esp_wifi_connect();  // auto-reconnect
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *evt =
                    (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "AP client connected: %02X:%02X:%02X:%02X:%02X:%02X",
                         evt->mac[0], evt->mac[1], evt->mac[2],
                         evt->mac[3], evt->mac[4], evt->mac[5]);
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *evt =
                    (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "AP client disconnected: %02X:%02X:%02X:%02X:%02X:%02X",
                         evt->mac[0], evt->mac[1], evt->mac[2],
                         evt->mac[3], evt->mac[4], evt->mac[5]);
                break;
            }

            default:
                break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&evt->ip_info.ip));
        s_sta_connected = true;

        // Publish MAC on connect (matches ESPHome on_connect lambda)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_mac_str, sizeof(s_mac_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        ESP_LOGI(TAG, "STA IP: %s  MAC: %s", s_ip_str, s_mac_str);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t wifi_init(void)
{
    // NVS is required by WiFi driver
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    // WiFi driver init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // AP+STA mode (required for ESP-NOW + WiFi coexistence)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // STA config
    wifi_config_t sta_cfg = {
        .sta = {
            .ssid     = WIFI_STA_SSID,
            .password = WIFI_STA_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    // AP config
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = WIFI_AP_SSID,
            .password       = WIFI_AP_PASS,
            .ssid_len       = strlen(WIFI_AP_SSID),
            .channel        = 1,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    // Disable power save (matches ESPHome power_save_mode: none)
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Start
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP+STA initialized");
    ESP_LOGI(TAG, "  STA -> '%s'", WIFI_STA_SSID);
    ESP_LOGI(TAG, "  AP  -> '%s'", WIFI_AP_SSID);

    return ESP_OK;
}

const char* wifi_get_ip_str(void)
{
    return s_ip_str;
}

const char* wifi_get_mac_str(void)
{
    return s_mac_str;
}

bool wifi_is_connected(void)
{
    return s_sta_connected;
}
