#include "espnow.h"
#include "types.h"

#include <string.h>
#include <sys/time.h>

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"

static const char *TAG = "espnow";

// ---------------------------------------------------------------------------
// Peer MACs (from ESPHome YAML: espnow.peers)
// ---------------------------------------------------------------------------
static const uint8_t PEER_MAC_1[6] = {0x10, 0xB4, 0x1D, 0xEA, 0xE0, 0x68};
static const uint8_t PEER_MAC_2[6] = {0xB4, 0x3A, 0x45, 0x99, 0x70, 0x80};

#define ESPNOW_TIMEOUT_US  (30 * 1000 * 1000LL)  // 30 seconds in microseconds

// ---------------------------------------------------------------------------
// Forward: time sync helper
// ---------------------------------------------------------------------------
static void sync_time_from_local_time_str(const gps_espnow_packet_t *pkt);

// ---------------------------------------------------------------------------
// Add a single peer (ignore "already exists" error)
// ---------------------------------------------------------------------------
static esp_err_t add_peer(const uint8_t mac[6])
{
    esp_now_peer_info_t info = {0};
    memcpy(info.peer_addr, mac, 6);
    info.channel = 0;        // use current WiFi channel
    info.encrypt = false;

    esp_err_t err = esp_now_add_peer(&info);
    if (err == ESP_ERR_ESPNOW_EXIST) {
        return ESP_OK;  // already registered, not an error
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add peer %02X:%02X:%02X:%02X:%02X:%02X: %s",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 esp_err_to_name(err));
    }
    return err;
}

// ---------------------------------------------------------------------------
// Send pong response back to sender
// ---------------------------------------------------------------------------
static void send_pong(const uint8_t *src_mac, uint32_t ping_counter)
{
    // Auto-add unknown peers before replying (matches ESPHome auto_add_peer)
    if (!esp_now_is_peer_exist(src_mac)) {
        ESP_LOGI(TAG, "Auto-adding unknown peer for pong response");
        add_peer(src_mac);
    }

    pong_packet_t pong = {
        .ping_counter = ping_counter
    };

    esp_err_t err = esp_now_send(src_mac, (const uint8_t *)&pong, sizeof(pong));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Pong send failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Pong sent for ping #%u", (unsigned)ping_counter);
    }
}

// ---------------------------------------------------------------------------
// Receive callback -- runs in WiFi task context (keep fast)
// ---------------------------------------------------------------------------
static void on_data_recv(const esp_now_recv_info_t *recv_info,
                         const uint8_t *data, int len)
{
    if (len != sizeof(gps_espnow_packet_t)) {
        ESP_LOGW(TAG, "Unexpected packet size: %d (expected %u)",
                 len, (unsigned)sizeof(gps_espnow_packet_t));
        return;
    }

    gps_espnow_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    ESP_LOGI(TAG, "Ping #%u from %02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)pkt.ping_counter,
             recv_info->src_addr[0], recv_info->src_addr[1],
             recv_info->src_addr[2], recv_info->src_addr[3],
             recv_info->src_addr[4], recv_info->src_addr[5]);

    // --- Populate global GPS state ---
    g_gps.lat          = pkt.lat;
    g_gps.lon          = pkt.lon;
    g_gps.alt          = pkt.alt;
    g_gps.speed        = pkt.speed;
    g_gps.heading      = pkt.heading;
    g_gps.sats         = pkt.sats;
    g_gps.sats_visible = pkt.sats_visible;
    g_gps.fix_type     = pkt.fix_type;

    // Safe copy of local_time (ensure NUL termination)
    memcpy(g_gps.local_time, pkt.local_time, sizeof(pkt.local_time));
    g_gps.local_time[sizeof(g_gps.local_time) - 1] = '\0';

    g_gps.pdop         = pkt.pdop;
    g_gps.hdop         = pkt.hdop;
    g_gps.vdop         = pkt.vdop;
    g_gps.h_acc        = pkt.h_acc;
    g_gps.v_acc        = pkt.v_acc;
    g_gps.station_ip   = pkt.station_ip;
    g_gps.ping_counter = pkt.ping_counter;

    // --- Derived fields ---
    g_gps.speed_mph = pkt.speed * 2.23694f;

    switch (pkt.fix_type) {
        case 0:  strncpy(g_gps.fix_status, "No Fix", sizeof(g_gps.fix_status)); break;
        case 1:  strncpy(g_gps.fix_status, "Dead Reckoning", sizeof(g_gps.fix_status)); break;
        case 2:  strncpy(g_gps.fix_status, "2D Fix", sizeof(g_gps.fix_status)); break;
        case 3:  strncpy(g_gps.fix_status, "3D Fix", sizeof(g_gps.fix_status)); break;
        default: strncpy(g_gps.fix_status, "Unknown", sizeof(g_gps.fix_status)); break;
    }

    // Station IP string (little-endian uint32 -> dotted decimal)
    snprintf(g_gps.station_ip_str, sizeof(g_gps.station_ip_str),
             "%d.%d.%d.%d",
             (pkt.station_ip >>  0) & 0xFF,
             (pkt.station_ip >>  8) & 0xFF,
             (pkt.station_ip >> 16) & 0xFF,
             (pkt.station_ip >> 24) & 0xFF);

    // Update max satellites (for NVS persistence, done in main loop)
    if (pkt.sats_visible > g_gps.max_sats) {
        g_gps.max_sats = pkt.sats_visible;
    }

    // --- Connection tracking ---
    g_gps.connected = true;
    g_gps.last_packet_time = esp_timer_get_time();

    // --- Signal LVGL update ---
    g_new_gps_data = true;

    // --- GPS time sync (one-shot) ---
    sync_time_from_local_time_str(&pkt);

    // --- Send pong ---
    send_pong(recv_info->src_addr, pkt.ping_counter);
}

// ---------------------------------------------------------------------------
// Send callback (optional, for diagnostics)
// ---------------------------------------------------------------------------
static void on_data_sent(const esp_now_send_info_t *send_info, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        const uint8_t *mac_addr = send_info->des_addr;
        ESP_LOGW(TAG, "Pong delivery failed to %02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    }
}

// ---------------------------------------------------------------------------
// GPS time sync via settimeofday()
// ---------------------------------------------------------------------------
static void sync_time_from_local_time_str(const gps_espnow_packet_t *pkt)
{
    // Only sync once, and only when we have a real fix
    if (g_gps.time_synced) return;
    if (pkt->fix_type < 2) return;
    if (strlen(pkt->local_time) < 8) return;  // need "HH:MM:SS"

    int hour = 0, minute = 0, second = 0;
    if (sscanf(pkt->local_time, "%d:%d:%d", &hour, &minute, &second) != 3) {
        ESP_LOGW(TAG, "Failed to parse local_time: '%s'", pkt->local_time);
        return;
    }

    // Get current system time, replace H:M:S with GPS values
    // This matches the ESPHome approach: keep existing date, update time only
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    timeinfo.tm_hour = hour;
    timeinfo.tm_min  = minute;
    timeinfo.tm_sec  = second;

    time_t new_time = mktime(&timeinfo);
    if (new_time < 0) {
        ESP_LOGW(TAG, "mktime failed for GPS time");
        return;
    }

    struct timeval tv = { .tv_sec = new_time, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGI(TAG, "System time synced from GPS: %s", pkt->local_time);
        g_gps.time_synced = true;
    } else {
        ESP_LOGE(TAG, "settimeofday() failed");
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t espnow_init(void)
{
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    // Register static peers
    add_peer(PEER_MAC_1);
    add_peer(PEER_MAC_2);

    // Log our own MAC for sender configuration
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Receiver MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "ESP-NOW initialized with 2 static peers + auto-add");
    return ESP_OK;
}

void espnow_check_timeout(void)
{
    if (!g_gps.connected) return;

    int64_t elapsed = esp_timer_get_time() - g_gps.last_packet_time;
    if (elapsed > ESPNOW_TIMEOUT_US) {
        g_gps.connected = false;
        strncpy(g_gps.fix_status, "No Signal", sizeof(g_gps.fix_status));
        ESP_LOGW(TAG, "Sender timeout (no packet for %.1fs)", elapsed / 1e6);
    }
}
