/**
 * @file display.h
 * @brief Display driver: SH8601 QSPI AMOLED + FT5x06 touch + LVGL 9 init.
 *
 * Owns all hardware init (I2C bus, TCA9554, SH8601, FT5x06), LVGL display/
 * input-device registration, brightness control, and sleep/dim timers.
 *
 * Thread safety: all public functions are safe to call from any task.
 * LVGL operations require the display lock (display_lock / display_unlock).
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* ------------------------------------------------------------------ */
/*  Display constants                                                  */
/* ------------------------------------------------------------------ */

#define DISP_HOR_RES        368
#define DISP_VER_RES        448

/** LVGL mutex timeout (ms). All callers use this to prevent deadlocks. */
#define LVGL_LOCK_TIMEOUT_MS 1000

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize all display hardware and LVGL.
 *
 * Call once from app_main(). Performs:
 *   1. I2C bus init (SCL:14, SDA:15, 200 kHz)
 *   2. TCA9554 IO expander reset sequence
 *   3. SH8601 QSPI panel init
 *   4. FT5x06 touch controller init
 *   5. LVGL 9 library init
 *   6. Double-buffered PSRAM draw buffers
 *   7. LVGL display + touch input device registration
 *   8. LVGL tick timer (2ms periodic)
 *   9. LVGL task creation (with mutex)
 *   10. Sleep/dim periodic timer (1s)
 */
void display_init(void);

/* ------------------------------------------------------------------ */
/*  LVGL lock                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Acquire the LVGL mutex. Must be held for all LVGL API calls.
 * @param timeout_ms  Timeout in ms, or -1 for infinite.
 * @return true if lock acquired, false on timeout.
 */
bool display_lock(int timeout_ms);

/** @brief Release the LVGL mutex. */
void display_unlock(void);

/* ------------------------------------------------------------------ */
/*  Brightness                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Set display brightness.
 * @param percent  0-100. Maps linearly to SH8601 reg 0x51 (0x00-0xFF).
 *                 0 = fully off, 100 = maximum brightness.
 *
 * Also updates the internal saved_brightness if percent > 0,
 * so wake-from-sleep restores the correct level.
 */
void display_set_brightness(uint8_t percent);

/**
 * @brief Get the current saved brightness (0-100).
 * @return The last non-zero brightness set, or the boot default.
 */
uint8_t display_get_brightness(void);

/**
 * @brief Turn off the display (brightness 0) without changing saved brightness.
 *
 * The next call to display_reset_activity() or display_set_brightness(>0)
 * restores the saved brightness.
 */
void display_turn_off(void);

/* ------------------------------------------------------------------ */
/*  Sleep / Dim / Activity                                             */
/* ------------------------------------------------------------------ */

/** Display power state. */
typedef enum {
    DISP_STATE_AWAKE,   /**< Normal brightness */
    DISP_STATE_DIMMED,  /**< Reduced brightness (15%) after dim timeout */
    DISP_STATE_ASLEEP,  /**< Brightness 0 after sleep timeout */
} display_state_t;

/**
 * @brief Reset the inactivity timer. Call on any user interaction:
 *        touch event, button press, ESP-NOW packet received.
 *
 * If the display is dimmed or asleep, this wakes it immediately
 * (restores saved brightness) before resetting the timer.
 */
void display_reset_activity(void);

/**
 * @brief Get the current display power state.
 */
display_state_t display_get_state(void);

/**
 * @brief Check if the display is asleep (brightness 0).
 * @return true if state == DISP_STATE_ASLEEP.
 */
bool display_is_asleep(void);

/* ------------------------------------------------------------------ */
/*  Sleep/Dim configuration (persisted by config.c)                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Update dim/sleep timeouts and enable flags at runtime.
 *
 * Called by config.c after loading NVS, or by UI settings callbacks.
 * Values take effect on the next 1s timer tick.
 */
void display_set_dim_timeout(uint16_t seconds);
void display_set_sleep_timeout(uint16_t seconds);
void display_set_dim_enabled(bool enabled);
void display_set_sleep_enabled(bool enabled);

/* ------------------------------------------------------------------ */
/*  BOOT button (GPIO0)                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Callback type for BOOT button press.
 *
 * The display driver handles debounce and wake-from-sleep internally.
 * If the display was asleep, it wakes without invoking this callback.
 * If awake, this callback fires (e.g., for "next tab" logic).
 */
typedef void (*boot_button_cb_t)(void);

/**
 * @brief Register a callback for BOOT button press when display is awake.
 * @param cb  Function to call, or NULL to disable.
 */
void display_set_boot_button_cb(boot_button_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
