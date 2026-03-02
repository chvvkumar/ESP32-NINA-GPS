/**
 * @file config.h
 * @brief NVS persistent configuration for GPS receiver settings.
 *
 * Stores and retrieves user settings (brightness, timeouts, enable flags,
 * last active page) in the ESP-IDF NVS flash partition.
 *
 * NVS key/default table (from migration design):
 *   brightness    uint8   35
 *   dim_timeout   uint16  30    (seconds)
 *   sleep_timeout uint16  60    (seconds)
 *   dim_enabled   bool    true
 *   sleep_enabled bool    true
 *   last_page     uint8   0
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Defaults                                                           */
/* ------------------------------------------------------------------ */
#define CONFIG_DEFAULT_BRIGHTNESS      35.0f   /* percent */
#define CONFIG_DEFAULT_DIM_TIMEOUT     30      /* seconds */
#define CONFIG_DEFAULT_SLEEP_TIMEOUT   60      /* seconds */
#define CONFIG_DEFAULT_DIM_ENABLED     true
#define CONFIG_DEFAULT_SLEEP_ENABLED   true
#define CONFIG_DEFAULT_LAST_PAGE       0

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize NVS and load all config values.
 *        Call once from app_main() before any get/set calls.
 * @return ESP_OK on success, or NVS error code.
 */
esp_err_t config_init(void);

/* ------------------------------------------------------------------ */
/*  Getters -- return cached value (no NVS read per call)              */
/* ------------------------------------------------------------------ */

/** Brightness percentage (10..100). */
float config_get_brightness(void);

/** Dim timeout in seconds (5..300). */
int   config_get_dim_timeout(void);

/** Sleep timeout in seconds (5..300). */
int   config_get_sleep_timeout(void);

/** Whether dim-on-idle is enabled. */
bool  config_get_dim_enabled(void);

/** Whether sleep-on-idle is enabled. */
bool  config_get_sleep_enabled(void);

/** Last active tab index (0..5). */
int   config_get_last_page(void);

/* ------------------------------------------------------------------ */
/*  Setters -- update cache + persist to NVS                           */
/* ------------------------------------------------------------------ */

/** Set and persist brightness (percent, clamped 10..100). */
void config_set_brightness(float percent);

/** Set and persist dim timeout (seconds, clamped 5..300). */
void config_set_dim_timeout(int seconds);

/** Set and persist sleep timeout (seconds, clamped 5..300). */
void config_set_sleep_timeout(int seconds);

/** Set and persist dim enable flag. */
void config_set_dim_enabled(bool enabled);

/** Set and persist sleep enable flag. */
void config_set_sleep_enabled(bool enabled);

/** Set and persist last active page (clamped 0..5). */
void config_set_last_page(int page);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
