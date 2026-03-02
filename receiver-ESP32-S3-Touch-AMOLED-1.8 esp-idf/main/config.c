/**
 * @file config.c
 * @brief NVS persistent configuration implementation.
 *
 * Manages a "gps_cfg" NVS namespace with cached reads and write-through
 * persistence.  All values are loaded once at init and served from RAM
 * thereafter.  Setters clamp values, update the cache, and commit to NVS.
 */

#include "config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";

/* ── NVS namespace ─────────────────────────────────────────────────── */
#define NVS_NAMESPACE  "gps_cfg"

/* ── NVS key strings ───────────────────────────────────────────────── */
#define KEY_BRIGHTNESS     "brightness"
#define KEY_DIM_TIMEOUT    "dim_tmout"
#define KEY_SLEEP_TIMEOUT  "slp_tmout"
#define KEY_DIM_ENABLED    "dim_en"
#define KEY_SLEEP_ENABLED  "slp_en"
#define KEY_LAST_PAGE      "last_page"

/* ── Cached config values (RAM mirror) ─────────────────────────────── */
static float    s_brightness     = CONFIG_DEFAULT_BRIGHTNESS;
static int      s_dim_timeout    = CONFIG_DEFAULT_DIM_TIMEOUT;
static int      s_sleep_timeout  = CONFIG_DEFAULT_SLEEP_TIMEOUT;
static bool     s_dim_enabled    = CONFIG_DEFAULT_DIM_ENABLED;
static bool     s_sleep_enabled  = CONFIG_DEFAULT_SLEEP_ENABLED;
static int      s_last_page      = CONFIG_DEFAULT_LAST_PAGE;

/* ── NVS handle (opened once, kept open) ───────────────────────────── */
static nvs_handle_t s_nvs = 0;
static bool s_inited = false;

/* ── Helper: clamp int to [lo, hi] ─────────────────────────────────── */
static inline int clamp_int(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* ── Helper: clamp float to [lo, hi] ──────────────────────────────── */
static inline float clamp_float(float val, float lo, float hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* ================================================================== */
/*  Lifecycle                                                          */
/* ================================================================== */

esp_err_t config_init(void) {
    if (s_inited) return ESP_OK;

    /* Initialize NVS flash (handles already-initialized case) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated or version mismatch, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Open namespace */
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(\"%s\") failed: %s",
                 NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    /* ── Load each key (use default if not found) ──────────────── */
    uint8_t u8_val;
    uint16_t u16_val;

    /* brightness (stored as uint8, represents percent) */
    if (nvs_get_u8(s_nvs, KEY_BRIGHTNESS, &u8_val) == ESP_OK) {
        s_brightness = (float)u8_val;
    } else {
        s_brightness = CONFIG_DEFAULT_BRIGHTNESS;
    }

    /* dim_timeout (stored as uint16, seconds) */
    if (nvs_get_u16(s_nvs, KEY_DIM_TIMEOUT, &u16_val) == ESP_OK) {
        s_dim_timeout = (int)u16_val;
    } else {
        s_dim_timeout = CONFIG_DEFAULT_DIM_TIMEOUT;
    }

    /* sleep_timeout (stored as uint16, seconds) */
    if (nvs_get_u16(s_nvs, KEY_SLEEP_TIMEOUT, &u16_val) == ESP_OK) {
        s_sleep_timeout = (int)u16_val;
    } else {
        s_sleep_timeout = CONFIG_DEFAULT_SLEEP_TIMEOUT;
    }

    /* dim_enabled (stored as uint8, 0 or 1) */
    if (nvs_get_u8(s_nvs, KEY_DIM_ENABLED, &u8_val) == ESP_OK) {
        s_dim_enabled = (u8_val != 0);
    } else {
        s_dim_enabled = CONFIG_DEFAULT_DIM_ENABLED;
    }

    /* sleep_enabled (stored as uint8, 0 or 1) */
    if (nvs_get_u8(s_nvs, KEY_SLEEP_ENABLED, &u8_val) == ESP_OK) {
        s_sleep_enabled = (u8_val != 0);
    } else {
        s_sleep_enabled = CONFIG_DEFAULT_SLEEP_ENABLED;
    }

    /* last_page (stored as uint8) */
    if (nvs_get_u8(s_nvs, KEY_LAST_PAGE, &u8_val) == ESP_OK) {
        s_last_page = (int)u8_val;
    } else {
        s_last_page = CONFIG_DEFAULT_LAST_PAGE;
    }

    s_inited = true;

    ESP_LOGI(TAG, "Config loaded: bright=%.0f%%, dim=%ds(en=%d), "
             "sleep=%ds(en=%d), page=%d",
             s_brightness, s_dim_timeout, s_dim_enabled,
             s_sleep_timeout, s_sleep_enabled, s_last_page);

    return ESP_OK;
}

/* ================================================================== */
/*  Getters                                                            */
/* ================================================================== */

float config_get_brightness(void)    { return s_brightness; }
int   config_get_dim_timeout(void)   { return s_dim_timeout; }
int   config_get_sleep_timeout(void) { return s_sleep_timeout; }
bool  config_get_dim_enabled(void)   { return s_dim_enabled; }
bool  config_get_sleep_enabled(void) { return s_sleep_enabled; }
int   config_get_last_page(void)     { return s_last_page; }

/* ================================================================== */
/*  Setters                                                            */
/* ================================================================== */

void config_set_brightness(float percent) {
    s_brightness = clamp_float(percent, 10.0f, 100.0f);
    if (!s_inited) return;

    uint8_t val = (uint8_t)s_brightness;
    esp_err_t err = nvs_set_u8(s_nvs, KEY_BRIGHTNESS, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Saved brightness=%u%%", val);
    }
}

void config_set_dim_timeout(int seconds) {
    s_dim_timeout = clamp_int(seconds, 5, 300);
    if (!s_inited) return;

    uint16_t val = (uint16_t)s_dim_timeout;
    esp_err_t err = nvs_set_u16(s_nvs, KEY_DIM_TIMEOUT, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save dim_timeout: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Saved dim_timeout=%ds", s_dim_timeout);
    }
}

void config_set_sleep_timeout(int seconds) {
    s_sleep_timeout = clamp_int(seconds, 5, 300);
    if (!s_inited) return;

    uint16_t val = (uint16_t)s_sleep_timeout;
    esp_err_t err = nvs_set_u16(s_nvs, KEY_SLEEP_TIMEOUT, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sleep_timeout: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Saved sleep_timeout=%ds", s_sleep_timeout);
    }
}

void config_set_dim_enabled(bool enabled) {
    s_dim_enabled = enabled;
    if (!s_inited) return;

    uint8_t val = enabled ? 1 : 0;
    esp_err_t err = nvs_set_u8(s_nvs, KEY_DIM_ENABLED, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save dim_enabled: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Saved dim_enabled=%d", enabled);
    }
}

void config_set_sleep_enabled(bool enabled) {
    s_sleep_enabled = enabled;
    if (!s_inited) return;

    uint8_t val = enabled ? 1 : 0;
    esp_err_t err = nvs_set_u8(s_nvs, KEY_SLEEP_ENABLED, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sleep_enabled: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Saved sleep_enabled=%d", enabled);
    }
}

void config_set_last_page(int page) {
    s_last_page = clamp_int(page, 0, 5);
    if (!s_inited) return;

    uint8_t val = (uint8_t)s_last_page;
    esp_err_t err = nvs_set_u8(s_nvs, KEY_LAST_PAGE, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save last_page: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Saved last_page=%d", s_last_page);
    }
}
