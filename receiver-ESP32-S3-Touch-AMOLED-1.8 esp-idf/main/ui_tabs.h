/**
 * @file ui_tabs.h
 * @brief GPS receiver 6-tab LVGL 9 UI -- public API.
 *
 * All functions must be called from the LVGL task (or under the LVGL mutex).
 */

#ifndef UI_TABS_H
#define UI_TABS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#include "types.h"  /* gps_data_t -- single definition shared with espnow.c */

/* ------------------------------------------------------------------ */
/*  Settings callback -- UI fires these; main.c wires to HW           */
/* ------------------------------------------------------------------ */
typedef struct {
    void (*on_brightness_changed)(float percent);       /* 10..100 */
    void (*on_dim_timeout_changed)(int seconds);        /* 5..300  */
    void (*on_sleep_timeout_changed)(int seconds);      /* 5..300  */
    void (*on_dim_enable_changed)(bool enabled);
    void (*on_sleep_enable_changed)(bool enabled);
    void (*on_turnoff_pressed)(void);
    void (*on_reboot_pressed)(void);
    void (*on_tab_changed)(int tab_index);              /* 0..5    */
} ui_tabs_callbacks_t;

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Create the 6-tab tabview on the given parent (screen).
 * @param parent   Active LVGL screen object.
 * @param cb       Pointer to callback struct (NULL-safe per member).
 */
void ui_tabs_create(lv_obj_t *parent, const ui_tabs_callbacks_t *cb);

/* ------------------------------------------------------------------ */
/*  Data update -- call from LVGL timer or ESP-NOW handler             */
/* ------------------------------------------------------------------ */

/** Push a full GPS packet into all tab widgets. */
void ui_tabs_update_gps(const gps_data_t *data);

/** Set the receiver (this device) IP string on the Network tab. */
void ui_tabs_set_device_ip(const char *ip_str);

/* ------------------------------------------------------------------ */
/*  LED indicator                                                      */
/* ------------------------------------------------------------------ */

/** Turn the ESP-NOW LED indicator on (call on packet rx). */
void ui_tabs_led_on(void);

/** Turn the ESP-NOW LED indicator off (call after blink delay). */
void ui_tabs_led_off(void);

/* ------------------------------------------------------------------ */
/*  Tab navigation                                                     */
/* ------------------------------------------------------------------ */

/** Programmatically switch to a tab (0..5). */
void ui_tabs_set_active(int tab_index, bool animate);

/** Return the currently active tab index. */
int  ui_tabs_get_active(void);

/** Return the total number of tabs (always 6). */
int  ui_tabs_get_tab_count(void);

/* ------------------------------------------------------------------ */
/*  Settings restore -- call once after create to push NVS values      */
/* ------------------------------------------------------------------ */
void ui_tabs_restore_settings(float brightness_pct,
                              int dim_timeout_s, int sleep_timeout_s,
                              bool dim_enabled, bool sleep_enabled);

#ifdef __cplusplus
}
#endif

#endif /* UI_TABS_H */
