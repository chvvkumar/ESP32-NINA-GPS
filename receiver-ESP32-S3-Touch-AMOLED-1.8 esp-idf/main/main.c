/**
 * @file main.c
 * @brief Application entry point for GPS receiver on Waveshare
 *        ESP32-S3-Touch-AMOLED-1.8.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_sleep.h"

#include "types.h"
#include "display.h"
#include "config.h"
#include "wifi.h"
#include "espnow.h"
#include "web_server.h"
#include "ui_tabs.h"

static const char *TAG = "main";

/* ---- Global GPS state (declared extern in types.h) ---- */
gps_data_t g_gps = {0};
volatile bool g_new_gps_data = false;

/* ---- BOOT button: next tab ---- */
static void next_tab_cb(void)
{
    int total = ui_tabs_get_tab_count();
    int current = ui_tabs_get_active();
    int next = (current + 1) % total;
    ESP_LOGI(TAG, "Button: tab %d -> %d", current, next);

    if (display_lock(LVGL_LOCK_TIMEOUT_MS)) {
        ui_tabs_set_active(next, true);
        display_unlock();
    }
}

/* ---- Settings callbacks (bridge UI -> display/config) ---- */
static void on_brightness_changed(float pct)
{
    display_set_brightness((uint8_t)pct);
    config_set_brightness(pct);
}

static void on_dim_timeout_changed(int s)
{
    display_set_dim_timeout((uint16_t)s);
    config_set_dim_timeout(s);
}

static void on_sleep_timeout_changed(int s)
{
    display_set_sleep_timeout((uint16_t)s);
    config_set_sleep_timeout(s);
}

static void on_dim_enable_changed(bool en)
{
    display_set_dim_enabled(en);
    config_set_dim_enabled(en);
}

static void on_sleep_enable_changed(bool en)
{
    display_set_sleep_enabled(en);
    config_set_sleep_enabled(en);
}

static void on_turnoff_pressed(void)
{
    display_turn_off();
}

static void on_reboot_pressed(void)
{
    ESP_LOGW(TAG, "Reboot requested by user");
    esp_restart();
}

static void on_tab_changed(int idx)
{
    config_set_last_page(idx);
}

/* ---- LVGL timer: GPS data -> UI pipeline (1s periodic) ---- */
static void gps_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Check ESP-NOW connection timeout */
    espnow_check_timeout();

    /* Push GPS data to UI if new data available */
    if (g_new_gps_data) {
        g_new_gps_data = false;

        ui_tabs_update_gps((const gps_data_t *)&g_gps);

        /* Brief LED blink */
        ui_tabs_led_on();
    } else {
        ui_tabs_led_off();
    }

    /* Update device IP on Network tab */
    ui_tabs_set_device_ip(wifi_get_ip_str());
}

void app_main(void)
{
    /* ---- 1. NVS ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ---- 2. Load config from NVS ---- */
    config_init();

    /* ---- 3. Display + LVGL init ---- */
    display_init();

    /* Apply persisted settings to display driver */
    display_set_brightness((uint8_t)config_get_brightness());
    display_set_dim_timeout((uint16_t)config_get_dim_timeout());
    display_set_sleep_timeout((uint16_t)config_get_sleep_timeout());
    display_set_dim_enabled(config_get_dim_enabled());
    display_set_sleep_enabled(config_get_sleep_enabled());

    /* ---- 4. Create UI (under LVGL lock) ---- */
    if (display_lock(-1)) {
        lv_obj_t *scr = lv_screen_active();

        static ui_tabs_callbacks_t ui_cb = {
            .on_brightness_changed   = on_brightness_changed,
            .on_dim_timeout_changed  = on_dim_timeout_changed,
            .on_sleep_timeout_changed = on_sleep_timeout_changed,
            .on_dim_enable_changed   = on_dim_enable_changed,
            .on_sleep_enable_changed = on_sleep_enable_changed,
            .on_turnoff_pressed      = on_turnoff_pressed,
            .on_reboot_pressed       = on_reboot_pressed,
            .on_tab_changed          = on_tab_changed,
        };
        ui_tabs_create(scr, &ui_cb);

        /* Restore NVS settings into UI widgets */
        ui_tabs_restore_settings(
            config_get_brightness(),
            config_get_dim_timeout(),
            config_get_sleep_timeout(),
            config_get_dim_enabled(),
            config_get_sleep_enabled()
        );

        /* Restore active tab: from RTC memory on deep sleep wake,
         * or from NVS on normal boot */
        esp_sleep_wakeup_cause_t wake_cause = display_check_wake_cause();
        int restore_tab;
        if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
            restore_tab = display_get_saved_tab();
            ESP_LOGI(TAG, "Deep sleep wake: restoring tab %d from RTC", restore_tab);
        } else {
            restore_tab = config_get_last_page();
            ESP_LOGI(TAG, "Normal boot: restoring tab %d from NVS", restore_tab);
        }
        ui_tabs_set_active(restore_tab, false);

        /* Create 1-second LVGL timer for GPS-to-UI data pipeline */
        lv_timer_create(gps_ui_timer_cb, 1000, NULL);

        display_unlock();
    }

    /* ---- 5. Register BOOT button callback ---- */
    display_set_boot_button_cb(next_tab_cb);

    /* ---- 6. WiFi ---- */
    wifi_init();

    /* ---- 7. ESP-NOW ---- */
    espnow_init();

    /* ---- 8. Web server (OTA) ---- */
    web_server_init();

    ESP_LOGI(TAG, "=== App init complete ===");

    /* app_main returns -- FreeRTOS scheduler runs the created tasks. */
}
