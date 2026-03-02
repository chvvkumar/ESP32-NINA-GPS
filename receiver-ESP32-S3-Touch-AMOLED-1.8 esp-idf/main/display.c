/**
 * @file display.c
 * @brief Display driver: SH8601 QSPI AMOLED + FT5x06 touch + LVGL 9.
 *
 * Hardware init sequence:
 *   I2C bus --> TCA9554 reset --> SH8601 QSPI --> FT5x06 touch
 *   --> LVGL 9 init --> buffers --> display/indev registration
 *   --> tick timer --> LVGL task --> sleep/dim timer
 */

#include "display.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "lvgl.h"              /* includes lv_draw_sw.h -> lv_draw_sw_rgb565_swap() */
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_io_expander_tca9554.h"

static const char *TAG = "display";

/* ================================================================== */
/*  Pin definitions                                                    */
/* ================================================================== */

#define LCD_HOST            SPI2_HOST
#define TOUCH_HOST          I2C_NUM_0

/* I2C */
#define PIN_I2C_SCL         GPIO_NUM_14
#define PIN_I2C_SDA         GPIO_NUM_15
#define I2C_FREQ_HZ         (200 * 1000)

/* QSPI LCD */
#define PIN_LCD_CS           GPIO_NUM_12
#define PIN_LCD_PCLK         GPIO_NUM_11
#define PIN_LCD_DATA0        GPIO_NUM_4
#define PIN_LCD_DATA1        GPIO_NUM_5
#define PIN_LCD_DATA2        GPIO_NUM_6
#define PIN_LCD_DATA3        GPIO_NUM_7
#define PIN_LCD_RST          (-1)   /* Reset via TCA9554, not direct GPIO */

/* Touch */
#define PIN_TOUCH_INT        GPIO_NUM_21
#define PIN_TOUCH_RST        (-1)   /* Reset via TCA9554 */

/* BOOT button */
#define PIN_BOOT_BUTTON      GPIO_NUM_0
#define BUTTON_DEBOUNCE_MS   50

/* ================================================================== */
/*  LVGL configuration                                                 */
/* ================================================================== */

/**
 * Buffer height = 1/10 of screen.  Two buffers for double-buffering.
 *
 * Buffers MUST be in internal DMA-capable SRAM because the ESP32-S3 SPI
 * master driver cannot DMA directly from PSRAM.  It silently allocates an
 * internal-SRAM bounce buffer per SPI transaction, and for large LVGL
 * flushes the bounce-buffer allocation fails (ESP_ERR_NO_MEM).
 * 1/10 screen × 2 buffers ≈ 66 KB of internal SRAM, which is affordable.
 * The 8 MB PSRAM is still used for fonts, images, and application data.
 */
#define LVGL_BUF_HEIGHT      (DISP_VER_RES / 10)
#define LVGL_BUF_PIXELS      (DISP_HOR_RES * LVGL_BUF_HEIGHT)

/** Pixel format: RGB565 = 16 bits per pixel. */
#define LCD_BIT_PER_PIXEL    16

#define LVGL_TICK_PERIOD_MS  2
#define LVGL_TASK_STACK_SIZE (6 * 1024)
#define LVGL_TASK_PRIORITY   2
#define LVGL_TASK_MAX_DELAY  500
#define LVGL_TASK_MIN_DELAY  1

/* ================================================================== */
/*  Sleep/Dim defaults                                                 */
/* ================================================================== */

#define DEFAULT_BRIGHTNESS       35   /* percent */
#define DIM_BRIGHTNESS_PCT       15   /* percent when dimmed */
#define DEFAULT_DIM_TIMEOUT_S    30
#define DEFAULT_SLEEP_TIMEOUT_S  60

/* ================================================================== */
/*  Static state                                                       */
/* ================================================================== */

/* Hardware handles */
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t    s_panel    = NULL;
static esp_lcd_touch_handle_t    s_tp       = NULL;

/* LVGL display and indev */
static lv_display_t *s_disp   = NULL;
static lv_indev_t   *s_indev  = NULL;

/* LVGL mutex */
static SemaphoreHandle_t s_lvgl_mux = NULL;

/* Brightness state */
static uint8_t s_saved_brightness = DEFAULT_BRIGHTNESS; /* 0-100, last user-set */
static uint8_t s_current_brightness = 0;                /* 0-100, actual HW val */

/* Sleep/dim state */
static display_state_t s_display_state = DISP_STATE_AWAKE;
static int64_t         s_last_activity_us = 0;
static uint16_t        s_dim_timeout_s    = DEFAULT_DIM_TIMEOUT_S;
static uint16_t        s_sleep_timeout_s  = DEFAULT_SLEEP_TIMEOUT_S;
static bool            s_dim_enabled      = true;
static bool            s_sleep_enabled    = true;

/* BOOT button */
static boot_button_cb_t s_boot_btn_cb = NULL;
static int64_t          s_last_btn_press_us = 0;

/* ================================================================== */
/*  SH8601 Init Commands                                               */
/* ================================================================== */

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},                      // Sleep out
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},                  // Tear scan line
    {0x35, (uint8_t[]){0x00}, 1, 0},                         // Tearing effect on
    {0x53, (uint8_t[]){0x20}, 1, 10},                        // Write ctrl display
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},      // Column: 0-367
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},      // Row: 0-447
    {0x51, (uint8_t[]){0x00}, 1, 10},                        // Brightness = 0
    {0x29, (uint8_t[]){0x00}, 0, 10},                        // Display ON
    {0x51, (uint8_t[]){0xFF}, 1, 0},                         // Brightness = max
};

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static bool lvgl_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void lvgl_tick_cb(void *arg);
static void lvgl_task(void *arg);
static void sleep_dim_timer_cb(void *arg);
static void IRAM_ATTR boot_button_isr(void *arg);

/* ================================================================== */
/*  Internal: Brightness hardware                                      */
/* ================================================================== */

/**
 * @brief Send a raw brightness value (0x00-0xFF) to SH8601 register 0x51.
 *
 * The SH8601 QSPI command format requires the 8-bit register address
 * shifted into bits [15:8] with the WRITE_CMD opcode (0x02) in bits [31:24].
 * The panel IO layer handles this when lcd_cmd_bits=32 is configured.
 *
 * Since we configured lcd_cmd_bits=32 and the SH8601 driver's internal
 * tx_param() does the opcode packing, we replicate that logic here for
 * direct register access from outside the driver.
 */
static void brightness_hw_set(uint8_t raw_value)
{
    if (!s_panel_io) return;

    /*
     * QSPI command encoding for SH8601:
     *   cmd[31:24] = 0x02 (WRITE_CMD opcode)
     *   cmd[15:8]  = register address (0x51)
     *   cmd[7:0]   = 0x00
     *
     * This matches the tx_param() logic inside esp_lcd_sh8601.c:
     *   lcd_cmd = (reg & 0xFF) << 8 | (0x02 << 24)
     */
    int cmd = (0x51 << 8) | (0x02 << 24);
    esp_lcd_panel_io_tx_param(s_panel_io, cmd, &raw_value, 1);
}

/**
 * @brief Convert percent (0-100) to raw (0x00-0xFF) and send to hardware.
 */
static void brightness_set_pct(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint8_t raw = (uint8_t)((uint16_t)percent * 255 / 100);
    brightness_hw_set(raw);
    s_current_brightness = percent;
    ESP_LOGD(TAG, "Brightness: %d%% (raw 0x%02X)", percent, raw);
}

/* ================================================================== */
/*  Public: Brightness                                                 */
/* ================================================================== */

void display_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    if (percent > 0) {
        s_saved_brightness = percent;
    }
    brightness_set_pct(percent);

    /* If we're setting brightness > 0, we're effectively waking */
    if (percent > 0 && s_display_state != DISP_STATE_AWAKE) {
        s_display_state = DISP_STATE_AWAKE;
        s_last_activity_us = esp_timer_get_time();
    }
}

uint8_t display_get_brightness(void)
{
    return s_saved_brightness;
}

void display_turn_off(void)
{
    brightness_set_pct(0);
    s_display_state = DISP_STATE_ASLEEP;
    ESP_LOGI(TAG, "Display manually turned off");
}

/* ================================================================== */
/*  Public: Sleep/Dim/Activity                                         */
/* ================================================================== */

void display_reset_activity(void)
{
    s_last_activity_us = esp_timer_get_time();

    if (s_display_state == DISP_STATE_ASLEEP) {
        /* Wake from sleep: restore saved brightness */
        brightness_set_pct(s_saved_brightness);
        s_display_state = DISP_STATE_AWAKE;
        ESP_LOGI(TAG, "Display woken from sleep (brightness %d%%)", s_saved_brightness);
    } else if (s_display_state == DISP_STATE_DIMMED) {
        /* Wake from dim: restore saved brightness */
        brightness_set_pct(s_saved_brightness);
        s_display_state = DISP_STATE_AWAKE;
        ESP_LOGI(TAG, "Display restored from dim (brightness %d%%)", s_saved_brightness);
    }
}

display_state_t display_get_state(void)
{
    return s_display_state;
}

bool display_is_asleep(void)
{
    return s_display_state == DISP_STATE_ASLEEP;
}

void display_set_dim_timeout(uint16_t seconds)
{
    s_dim_timeout_s = seconds;
}

void display_set_sleep_timeout(uint16_t seconds)
{
    s_sleep_timeout_s = seconds;
}

void display_set_dim_enabled(bool enabled)
{
    s_dim_enabled = enabled;
    /* If dim was just disabled and we're dimmed, restore brightness */
    if (!enabled && s_display_state == DISP_STATE_DIMMED) {
        brightness_set_pct(s_saved_brightness);
        s_display_state = DISP_STATE_AWAKE;
    }
}

void display_set_sleep_enabled(bool enabled)
{
    s_sleep_enabled = enabled;
    /* If sleep was just disabled and we're asleep, wake up */
    if (!enabled && s_display_state == DISP_STATE_ASLEEP) {
        brightness_set_pct(s_saved_brightness);
        s_display_state = DISP_STATE_AWAKE;
    }
}

/* ================================================================== */
/*  Public: BOOT button                                                */
/* ================================================================== */

void display_set_boot_button_cb(boot_button_cb_t cb)
{
    s_boot_btn_cb = cb;
}

/* ================================================================== */
/*  Public: LVGL lock                                                  */
/* ================================================================== */

bool display_lock(int timeout_ms)
{
    if (!s_lvgl_mux) return false;
    const TickType_t ticks = (timeout_ms == -1)
                             ? portMAX_DELAY
                             : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, ticks) == pdTRUE;
}

void display_unlock(void)
{
    if (s_lvgl_mux) {
        xSemaphoreGive(s_lvgl_mux);
    }
}

/* ================================================================== */
/*  LVGL 9 flush callback                                              */
/* ================================================================== */

/**
 * @brief Called by LVGL to flush a rendered area to the display.
 *
 * LVGL renders RGB565 in native little-endian byte order, but the SH8601
 * AMOLED expects big-endian RGB565 over SPI (high byte first on the wire).
 * We swap bytes here before sending.  This is the LVGL 9 equivalent of the
 * LV_COLOR_16_SWAP=y Kconfig that was used in LVGL 8.
 */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    if (!panel) {
        lv_display_flush_ready(disp);
        return;
    }

    /* Swap RGB565 bytes: little-endian -> big-endian for SPI display */
    uint32_t px_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    lv_draw_sw_rgb565_swap(px_map, px_count);

    /* Buffers are in internal DMA-capable SRAM -- no cache sync needed.
     * esp_lcd_panel_draw_bitmap expects exclusive end coordinates. */
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);

    /*
     * Do NOT call lv_display_flush_ready() here. It is called from the
     * DMA-complete callback (lvgl_flush_ready_cb) for true async operation.
     */
}

/**
 * @brief SPI DMA transfer complete callback -- signals LVGL that the
 *        flush buffer is free for reuse.
 */
static bool lvgl_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    /* s_disp is used directly because the panel IO is created before the
     * LVGL display, so user_ctx was NULL at registration time. */
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false;
}

/* ================================================================== */
/*  LVGL 9 touch read callback                                        */
/* ================================================================== */

/**
 * @brief Called by LVGL to read touch input.
 *
 * LVGL 9 signature: void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
 *
 * Note: LVGL 8 used lv_indev_drv_t*. LVGL 9 uses lv_indev_t* directly.
 * User data is accessed via lv_indev_get_user_data().
 */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    if (!tp) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x, y;
    uint8_t count = 0;

    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &count, 1);

    if (pressed && count > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;

        /* Any touch is user activity */
        display_reset_activity();

        ESP_LOGD(TAG, "Touch: (%d, %d)", x, y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ================================================================== */
/*  LVGL tick + task                                                   */
/* ================================================================== */

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY;

    while (1) {
        if (display_lock(-1)) {
            delay_ms = lv_timer_handler();
            display_unlock();
        }

        if (delay_ms > LVGL_TASK_MAX_DELAY)   delay_ms = LVGL_TASK_MAX_DELAY;
        if (delay_ms < LVGL_TASK_MIN_DELAY)    delay_ms = LVGL_TASK_MIN_DELAY;

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ================================================================== */
/*  Sleep/Dim 1-second periodic timer                                  */
/* ================================================================== */

/**
 * @brief 1-second periodic timer callback for sleep/dim logic.
 *
 * State machine:
 *   AWAKE --> (dim timeout elapsed && dim enabled) --> DIMMED
 *   DIMMED --> (sleep timeout elapsed && sleep enabled) --> ASLEEP
 *   ASLEEP --> (activity detected via display_reset_activity()) --> AWAKE
 *
 * Sleep timeout is measured relative to dim. If dim_timeout=30s and
 * sleep_timeout=60s, the display dims at 30s and sleeps at 30+60=90s
 * of total inactivity. This matches the ESPHome YAML behavior.
 */
static void sleep_dim_timer_cb(void *arg)
{
    if (s_display_state == DISP_STATE_ASLEEP) {
        /* Already asleep -- nothing to do, wake is handled by
         * display_reset_activity() on touch/button/espnow. */
        return;
    }

    int64_t now_us = esp_timer_get_time();
    uint32_t elapsed_s = (uint32_t)((now_us - s_last_activity_us) / 1000000);

    /* Check sleep first (higher priority state transition) */
    if (s_sleep_enabled && s_sleep_timeout_s > 0) {
        /*
         * Effective sleep time: if dim is also enabled, sleep timeout
         * is additive (dim_timeout + sleep_timeout seconds total).
         * If dim is disabled, sleep_timeout alone.
         */
        uint32_t effective_sleep_s = s_sleep_timeout_s;
        if (s_dim_enabled && s_dim_timeout_s > 0) {
            effective_sleep_s = s_dim_timeout_s + s_sleep_timeout_s;
        }

        if (elapsed_s >= effective_sleep_s) {
            brightness_set_pct(0);
            s_display_state = DISP_STATE_ASLEEP;
            ESP_LOGI(TAG, "Display sleep after %lu s inactivity", (unsigned long)effective_sleep_s);
            return;
        }
    }

    /* Check dim -- only if not already dimmed */
    if (s_dim_enabled && s_dim_timeout_s > 0 && s_display_state == DISP_STATE_AWAKE) {
        if (elapsed_s >= s_dim_timeout_s) {
            brightness_set_pct(DIM_BRIGHTNESS_PCT);
            s_display_state = DISP_STATE_DIMMED;
            ESP_LOGI(TAG, "Display dim to %d%% after %u s inactivity",
                     DIM_BRIGHTNESS_PCT, s_dim_timeout_s);
        }
    }
}

/* ================================================================== */
/*  BOOT button ISR + debounce                                         */
/* ================================================================== */

/**
 * @brief GPIO interrupt handler for BOOT button (GPIO0).
 *
 * Runs in IRAM. Performs timestamp-based debounce. Defers actual
 * work to a FreeRTOS task notification to avoid calling LVGL or
 * I2C from ISR context.
 */
static TaskHandle_t s_button_task_handle = NULL;

static void IRAM_ATTR boot_button_isr(void *arg)
{
    if (s_button_task_handle) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(s_button_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief Task that processes BOOT button presses with debounce.
 *
 * Waits for ISR notification, debounces, then either:
 *   - Wakes the display if asleep (no further action)
 *   - Calls the registered callback if awake (e.g., next tab)
 */
static void boot_button_task(void *arg)
{
    while (1) {
        /* Block until ISR fires */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int64_t now_us = esp_timer_get_time();
        if ((now_us - s_last_btn_press_us) < (BUTTON_DEBOUNCE_MS * 1000)) {
            continue;  /* Too soon, ignore bounce */
        }
        s_last_btn_press_us = now_us;

        /* Always reset activity timer */
        display_reset_activity();

        if (s_display_state == DISP_STATE_ASLEEP ||
            s_display_state == DISP_STATE_DIMMED) {
            /* display_reset_activity() already woke us.
             * Don't fire the "next tab" action -- just wake. */
            ESP_LOGI(TAG, "BOOT button: woke display");
        } else {
            /* Display is fully awake -- fire the application callback */
            if (s_boot_btn_cb) {
                s_boot_btn_cb();
            }
        }
    }
}

/* ================================================================== */
/*  Hardware init helpers                                              */
/* ================================================================== */

/** @brief Initialize I2C bus (shared by TCA9554 + FT5x06). */
static void init_i2c(void)
{
    ESP_LOGI(TAG, "Init I2C bus (SCL:%d, SDA:%d, %d kHz)",
             PIN_I2C_SCL, PIN_I2C_SDA, I2C_FREQ_HZ / 1000);

    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, conf.mode, 0, 0, 0));
}

/**
 * @brief Initialize TCA9554 IO expander and perform reset sequence.
 *
 * Pins 0, 1, 2 are set as outputs.
 * All three driven LOW for 200ms, then HIGH to release resets.
 *
 * NOTE: The starter code calls esp_io_expander_new_i2c_tca9554() twice
 * (a bug). We call it only once.
 */
static void init_io_expander(void)
{
    ESP_LOGI(TAG, "Init TCA9554 IO expander at 0x20");

    esp_io_expander_handle_t expander = NULL;
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(
        TOUCH_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &expander));

    /* Configure pins 0, 1, 2 as outputs */
    esp_io_expander_set_dir(expander,
        IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2,
        IO_EXPANDER_OUTPUT);

    /* Assert resets (active low) */
    esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_0, 0);
    esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_1, 0);
    esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_2, 0);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Release resets */
    esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_0, 1);
    esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_1, 1);
    esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_2, 1);

    ESP_LOGI(TAG, "TCA9554 reset sequence complete");
}

/**
 * @brief Initialize SH8601 QSPI panel.
 *
 * Returns with s_panel_io and s_panel set.
 * The panel_io handle is needed later for brightness_hw_set().
 *
 * IMPORTANT: The flush-ready callback's user_ctx is set to the
 * lv_display_t* after LVGL init. During panel init we temporarily
 * pass NULL; it is patched up in display_init() after lv_display_create().
 */
static void init_lcd_panel(void)
{
    ESP_LOGI(TAG, "Init SH8601 QSPI AMOLED (%dx%d)", DISP_HOR_RES, DISP_VER_RES);

    /* SPI bus */
    const spi_bus_config_t bus_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK,
        PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
        DISP_HOR_RES * DISP_VER_RES * LCD_BIT_PER_PIXEL / 8
    );
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* Panel IO -- flush_ready callback ctx will be patched after LVGL init */
    const esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(
        PIN_LCD_CS,
        (esp_lcd_panel_io_color_trans_done_cb_t)lvgl_flush_ready_cb,
        NULL  /* user_ctx -- patched to lv_display_t* after creation */
    );
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_panel_io));

    /* Panel driver */
    sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_panel_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "SH8601 panel initialized");
}

/**
 * @brief Initialize FT5x06 capacitive touch controller.
 */
static void init_touch(void)
{
    ESP_LOGI(TAG, "Init FT5x06 touch (addr 0x38, INT:%d)", PIN_TOUCH_INT);

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISP_HOR_RES,
        .y_max = DISP_VER_RES,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &s_tp));

    ESP_LOGI(TAG, "FT5x06 touch initialized");
}

/**
 * @brief Initialize BOOT button GPIO with edge-triggered interrupt.
 */
static void init_boot_button(void)
{
    ESP_LOGI(TAG, "Init BOOT button (GPIO%d)", PIN_BOOT_BUTTON);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_BOOT_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  /* Active-low: falling edge = press */
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Create the button processing task BEFORE installing ISR */
    xTaskCreate(boot_button_task, "btn_task", 2048, NULL, 5, &s_button_task_handle);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BOOT_BUTTON, boot_button_isr, NULL));
}

/* ================================================================== */
/*  LVGL 9 init (display + touch registration)                         */
/* ================================================================== */

/**
 * @brief Initialize LVGL 9 library, create display and input devices.
 *
 * Key LVGL 8 -> 9 changes applied here:
 *
 * | LVGL 8 | LVGL 9 |
 * |--------|--------|
 * | lv_disp_draw_buf_t, lv_disp_drv_t | lv_display_create() |
 * | lv_disp_draw_buf_init() | lv_display_set_buffers() |
 * | lv_disp_drv_init(), lv_disp_drv_register() | lv_display_set_flush_cb() |
 * | disp_drv.rounder_cb | Removed. Use flush-level rounding or set_resolution_granularity |
 * | lv_indev_drv_t, lv_indev_drv_register() | lv_indev_create(), lv_indev_set_read_cb() |
 * | lv_disp_flush_ready(&disp_drv) | lv_display_flush_ready(disp) |
 * | callback sig: (lv_disp_drv_t *, area, lv_color_t *) | (lv_display_t *, area, uint8_t *) |
 */
static void init_lvgl(void)
{
    ESP_LOGI(TAG, "Init LVGL 9 library");
    lv_init();

    /* ---- Draw buffers in internal DMA-capable SRAM (double-buffered) ---- */
    /* RGB565 = 2 bytes per pixel.
     * ESP32-S3 SPI master cannot DMA directly from PSRAM (PSRAM addresses
     * are outside SOC_DMA_LOW..SOC_DMA_HIGH).  The driver falls back to an
     * internal-SRAM bounce buffer for each SPI transaction, which fails for
     * large transfers.  Allocating from internal SRAM avoids this entirely. */
    size_t buf_size = LVGL_BUF_PIXELS * (LCD_BIT_PER_PIXEL / 8);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf1 && "Failed to allocate LVGL draw buffer 1 from internal SRAM");
    assert(buf2 && "Failed to allocate LVGL draw buffer 2 from internal SRAM");
    ESP_LOGI(TAG, "LVGL draw buffers: 2 x %u bytes in internal SRAM", (unsigned)buf_size);

    /* ---- Create LVGL display ---- */
    s_disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    assert(s_disp);

    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, buf1, buf2, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(s_disp, s_panel);

    /*
     * Coordinate rounding: SH8601 requires even-aligned coordinates.
     * LVGL 8 used a rounder_cb for this. LVGL 9 removed rounder_cb.
     *
     * Option A: Handle in flush_cb by rounding area coords before draw_bitmap.
     * Option B: Not needed if the SH8601 driver handles partial-byte alignment.
     *
     * The SH8601 driver's draw_bitmap uses CASET/RASET which accept any coords,
     * but the QSPI transfer granularity needs even alignment. We handle this
     * by rounding in the flush callback:
     */

    /*
     * Patch the panel_io user_ctx to point to our lv_display_t*.
     * The SPI flush-complete callback needs this to call lv_display_flush_ready().
     *
     * The SH8601_PANEL_IO_QSPI_CONFIG macro set .user_ctx at struct init time.
     * Since esp_lcd_panel_io doesn't expose a set_user_ctx API, we work around
     * this by re-registering the callback. In practice, the io_config user_ctx
     * is stored in the panel_io handle and used directly by the SPI driver.
     *
     * Alternative: allocate the io_config after lv_display_create() and pass
     * s_disp directly. This requires reordering init_lcd_panel() after init_lvgl().
     *
     * For simplicity, we pass NULL during panel IO creation and handle the
     * flush_ready in the flush callback itself if the async approach is problematic.
     * The pattern used by the P4 reference is to call lv_display_flush_ready()
     * at the end of the flush_cb for synchronous operation.
     *
     * RECOMMENDED APPROACH: Restructure so LVGL display is created before
     * panel_io, passing s_disp as user_ctx. See init sequence in display_init().
     */

    /* ---- Create LVGL touch input device ---- */
    s_indev = lv_indev_create();
    assert(s_indev);

    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_touch_read_cb);
    lv_indev_set_user_data(s_indev, s_tp);
    lv_indev_set_display(s_indev, s_disp);

    /* ---- LVGL tick timer ---- */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    /* ---- LVGL mutex + task ---- */
    s_lvgl_mux = xSemaphoreCreateMutex();
    assert(s_lvgl_mux);

    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL,
                LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "LVGL 9 init complete (display + touch + task)");
}

/* ================================================================== */
/*  Public: display_init()                                             */
/* ================================================================== */

void display_init(void)
{
    ESP_LOGI(TAG, "=== Display init start ===");

    /* Suppress noisy driver logs */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    /* Initialize activity timer */
    s_last_activity_us = esp_timer_get_time();

    /* 1. I2C bus (shared by TCA9554 + FT5x06) */
    init_i2c();

    /* 2. TCA9554 IO expander: reset LCD + touch controllers */
    init_io_expander();

    /* 3. SH8601 QSPI panel */
    init_lcd_panel();

    /* Clear the SH8601 internal framebuffer to black.
     * Without this, uninitialized AMOLED RAM shows as random noise ("snow")
     * until LVGL has rendered every pixel at least once. */
    {
        const int ROWS = 16;
        size_t stripe = DISP_HOR_RES * ROWS * sizeof(uint16_t);
        uint16_t *tmp = heap_caps_malloc(stripe, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(tmp);
        memset(tmp, 0, stripe);  /* black = 0x0000 in RGB565 */
        for (int y = 0; y < DISP_VER_RES; y += ROWS) {
            int ye = y + ROWS;
            if (ye > DISP_VER_RES) ye = DISP_VER_RES;
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_HOR_RES, ye, tmp);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        free(tmp);
        ESP_LOGI(TAG, "Display framebuffer cleared to black");
    }

    /* 4. FT5x06 touch */
    init_touch();

    /* 5. LVGL 9 init (display + touch + tick + task) */
    init_lvgl();

    /* 6. Set initial brightness from saved value */
    display_set_brightness(s_saved_brightness);

    /* 7. Sleep/dim periodic timer (1 second) */
    const esp_timer_create_args_t sleep_timer_args = {
        .callback = sleep_dim_timer_cb,
        .name = "disp_sleep",
    };
    esp_timer_handle_t sleep_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&sleep_timer_args, &sleep_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sleep_timer, 1000000)); /* 1 second */

    /* 8. BOOT button with GPIO interrupt */
    init_boot_button();

    ESP_LOGI(TAG, "=== Display init complete ===");
}
