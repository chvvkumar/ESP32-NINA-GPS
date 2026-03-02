// types.h
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// ESP-NOW wire-format packets -- MUST stay byte-identical to sender
// Source of truth: GPS Sender EspNowSender.cpp  GpsEspNowPacket
// ============================================================================

typedef struct __attribute__((packed)) {
    double   lat;          //  0: degrees
    double   lon;          //  8: degrees
    float    alt;          // 16: metres
    float    speed;        // 20: m/s
    float    heading;      // 24: degrees
    uint8_t  sats;         // 28: satellites in use
    uint8_t  sats_visible; // 29: satellites visible
    uint8_t  fix_type;     // 30: 0=none 1=DR 2=2D 3=3D
    char     local_time[10]; // 31: "HH:MM:SS\0" (10 bytes on wire)
    float    pdop;         // 41: position DOP
    float    hdop;         // 45: horizontal DOP
    float    vdop;         // 49: vertical DOP
    float    h_acc;        // 53: horizontal accuracy metres
    float    v_acc;        // 57: vertical accuracy metres
    uint32_t station_ip;   // 61: sender IP (little-endian)
    uint32_t ping_counter; // 65: monotonic ping counter
} gps_espnow_packet_t;    // 69 bytes total

_Static_assert(sizeof(gps_espnow_packet_t) == 69,
               "gps_espnow_packet_t size must match sender (69 bytes)");

typedef struct __attribute__((packed)) {
    uint32_t ping_counter;
} pong_packet_t;           // 4 bytes

_Static_assert(sizeof(pong_packet_t) == 4,
               "pong_packet_t size must be 4 bytes");

// ============================================================================
// Receiver-side parsed GPS state (superset of wire data)
// ============================================================================

typedef struct {
    // --- From ESP-NOW packet ---
    double   lat;
    double   lon;
    float    alt;
    float    speed;          // m/s
    float    heading;        // degrees
    uint8_t  sats;
    uint8_t  sats_visible;
    uint8_t  fix_type;       // 0-3
    char     local_time[10]; // "HH:MM:SS\0"
    float    pdop;
    float    hdop;
    float    vdop;
    float    h_acc;
    float    v_acc;
    uint32_t station_ip;     // sender IP
    uint32_t ping_counter;   // last received ping

    // --- Receiver-computed ---
    char     fix_status[20]; // "3D Fix", "2D Fix", "No Fix", "Dead Reckoning"
    char     station_ip_str[16]; // "x.x.x.x"
    float    speed_mph;      // speed * 2.23694

    // --- Connection state ---
    bool     connected;          // true if packet received within timeout
    int64_t  last_packet_time;   // esp_timer_get_time() of last rx
    bool     time_synced;        // true after first settimeofday

    // --- Statistics ---
    uint8_t  max_sats;           // persistent in NVS
} gps_data_t;

// Global instance (defined in main.c, extern everywhere else)
extern gps_data_t g_gps;

// Volatile flag set by ESP-NOW ISR callback, consumed by LVGL timer
extern volatile bool g_new_gps_data;

#endif // TYPES_H
