/**
 * @file ui_tabs.c
 * @brief GPS receiver 6-tab LVGL 9 UI -- full implementation.
 *
 * Recreates the ESPHome YAML tabview (receiver-ESP32-S3-Touch-AMOLED-1.8.yaml)
 * using only LVGL 9 C API.  Display: 368x448 portrait AMOLED.
 */

#include "ui_tabs.h"
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

/* Custom large font for speed display (generated via lv_font_conv) */
LV_FONT_DECLARE(font_speed_144);

static const char *TAG = "ui_tabs";

/* ── Display geometry ───────────────────────────────────────────────── */
#define DISP_W       368
#define DISP_H       448
#define TAB_BAR_PCT  0    /* tab bar hidden -- BOOT button + swipe for nav */
#define TAB_COUNT    6

/* ── Theme colours (from YAML) ──────────────────────────────────────── */
#define COL_BG          0x000000   /* background black */
#define COL_RED         0xE10522   /* primary accent red */
#define COL_DARK        0x190101   /* dark surface (sliders, buttons) */
#define COL_TAB_BG      0x000000   /* tab bar background */
#define COL_TAB_TEXT    0x6E0211   /* inactive tab text */
#define COL_TAB_CHK_BG 0x6E0211   /* checked tab bg */
#define COL_TAB_CHK_TX 0xE10522   /* checked tab text */
#define COL_BAR_BG     0x770313   /* satellite bar background */
#define COL_BAR_IND    0xE10522   /* satellite bar indicator */
#define COL_KNOB       0xE10522   /* slider knob */
#define COL_SLIDER_IND 0x6E0211   /* slider indicator fill */
#define COL_BORDER     0xE10522   /* border accent */
#define COL_KNOB_RING  0x4A0A0A   /* switch knob border dark red */
#define COL_SW_OFF     0x770313   /* switch track when unchecked (dark red) */

/* ── Timeout step table (shared by dim + sleep sliders) ─────────────── */
static const int timeout_steps[] = {5, 10, 15, 30, 60, 120, 180, 240, 300};
#define TIMEOUT_STEP_COUNT  9
#define TIMEOUT_SLIDER_MAX  (TIMEOUT_STEP_COUNT - 1)   /* 0..8 */

/* ── Callbacks (set once at create) ─────────────────────────────────── */
static const ui_tabs_callbacks_t *s_cb = NULL;

/* ── Root tabview ───────────────────────────────────────────────────── */
static lv_obj_t *tabview = NULL;

/* ── Tab 1: Home ────────────────────────────────────────────────────── */
static lv_obj_t *lbl_time       = NULL;
static lv_obj_t *led_indicator  = NULL;
static lv_obj_t *lbl_fix_home   = NULL;
static lv_obj_t *lbl_sats_home  = NULL;
static lv_obj_t *lbl_lat_val    = NULL;
static lv_obj_t *lbl_lon_val    = NULL;
static lv_obj_t *lbl_alt_val    = NULL;

/* ── Tab 2: GPS Accuracy ───────────────────────────────────────────── */
static lv_obj_t *lbl_fix_gps    = NULL;
static lv_obj_t *lbl_sats_gps   = NULL;
static lv_obj_t *bar_sats       = NULL;
static lv_obj_t *lbl_hdop_val   = NULL;
static lv_obj_t *lbl_vdop_val   = NULL;
static lv_obj_t *lbl_pdop_val   = NULL;
static lv_obj_t *lbl_hacc_val   = NULL;
static lv_obj_t *lbl_vacc_val   = NULL;

/* ── Tab 3: Speed ──────────────────────────────────────────────────── */
static lv_obj_t *lbl_speed_val  = NULL;

/* ── Tab 4: Network ────────────────────────────────────────────────── */
static lv_obj_t *lbl_sender_ip  = NULL;
static lv_obj_t *lbl_device_ip  = NULL;

/* ── Tab 5: Settings ───────────────────────────────────────────────── */
static lv_obj_t *lbl_brightness_val     = NULL;
static lv_obj_t *slider_brightness      = NULL;
static lv_obj_t *lbl_dim_timeout_val    = NULL;
static lv_obj_t *slider_dim             = NULL;
static lv_obj_t *lbl_sleep_timeout_val  = NULL;
static lv_obj_t *slider_sleep           = NULL;
static lv_obj_t *sw_dim_enable          = NULL;
static lv_obj_t *sw_sleep_enable        = NULL;
static lv_obj_t *btn_turnoff            = NULL;

/* ── Tab 6: Reboot ─────────────────────────────────────────────────── */
static lv_obj_t *btn_reboot     = NULL;

/* ── Reusable styles (init'd once) ─────────────────────────────────── */
static lv_style_t sty_red_line;   /* separator lines */
static bool styles_inited = false;

static void init_styles(void) {
    if (styles_inited) return;
    styles_inited = true;

    lv_style_init(&sty_red_line);
    lv_style_set_line_color(&sty_red_line, lv_color_hex(COL_RED));
    lv_style_set_line_width(&sty_red_line, 2);
    lv_style_set_line_rounded(&sty_red_line, true);
}

/**
 * Create a horizontal red separator line inside a tab.
 *
 * YAML reference (all separator lines share this pattern):
 *   - line:
 *       points: [20,5  348,5]
 *       line_color: 0xE10522
 *       line_rounded: true
 */
static lv_obj_t *create_separator(lv_obj_t *parent, int y_pos) {
    static lv_point_precise_t pts[2];
    pts[0].x = 20;  pts[0].y = 0;
    pts[1].x = 348; pts[1].y = 0;

    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_add_style(line, &sty_red_line, 0);
    lv_obj_set_pos(line, 0, y_pos);
    lv_obj_set_size(line, DISP_W, 10);
    return line;
}

/**
 * Create a centered section title label (e.g., "LATITUDE", "LONGITUDE").
 * Uses montserrat_36, red accent, full-width centered.
 */
static lv_obj_t *create_section_title(lv_obj_t *parent, const char *text,
                                       int x, int y, int w, int h,
                                       const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, w, h);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

/**
 * Create a red-accented value label (large font, centered, DOT long-mode).
 */
static lv_obj_t *create_value_label(lv_obj_t *parent, const char *text,
                                     int x, int y, int w, int h,
                                     const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, w, h);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

/**
 * Build Tab 1 "Home" -- mirrors YAML `name: Home`
 *
 * Layout (full 448px height, tab bar removed):
 *   y=5    Time (48pt)           + LED indicator at (330,5)
 *   y=55   Fix status (right)    + Sat count (left)
 *   y=97   ── red separator ──
 *   y=115  "LATITUDE" title
 *   y=155  Latitude value (48pt)
 *   y=212  ── red separator ──
 *   y=230  "LONGITUDE" title
 *   y=270  Longitude value (48pt)
 *   y=327  ── red separator ──
 *   y=345  "ALTITUDE" title
 *   y=390  Altitude value (48pt)
 */
static void build_tab_home(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    /* Time -- 48pt red, full width, centered */
    lbl_time = lv_label_create(tab);
    lv_obj_set_pos(lbl_time, 0, 5);
    lv_obj_set_size(lbl_time, 365, 48);
    lv_label_set_text(lbl_time, "00:00:00");
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);

    /* LED indicator -- 30x30, top-right corner */
    led_indicator = lv_led_create(tab);
    lv_obj_set_pos(led_indicator, 330, 5);
    lv_obj_set_size(led_indicator, 30, 30);
    lv_led_set_color(led_indicator, lv_color_hex(0xFF0000));
    lv_led_set_brightness(led_indicator, 55);  /* ~21% of 255 */
    lv_led_off(led_indicator);

    /* Fix status (right-aligned) */
    lbl_fix_home = lv_label_create(tab);
    lv_obj_set_pos(lbl_fix_home, 234, 55);
    lv_label_set_text(lbl_fix_home, "No Fix");
    lv_obj_set_style_text_color(lbl_fix_home, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl_fix_home, &lv_font_montserrat_36, 0);

    /* Satellite count (left-aligned) */
    lbl_sats_home = lv_label_create(tab);
    lv_obj_set_pos(lbl_sats_home, 0, 55);
    lv_label_set_text(lbl_sats_home, "00 / 00");
    lv_label_set_long_mode(lbl_sats_home, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(lbl_sats_home, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl_sats_home, &lv_font_montserrat_36, 0);

    /* ── Separator 1 (y=97) ── */
    create_separator(tab, 97);

    /* LATITUDE section */
    create_section_title(tab, "LATITUDE", 0, 115, 365, 40, &lv_font_montserrat_36);
    lbl_lat_val = create_value_label(tab, "0.00000", 0, 155, 365, 55,
                                     &lv_font_montserrat_48);

    /* ── Separator 2 (y=212) ── */
    create_separator(tab, 212);

    /* LONGITUDE section */
    create_section_title(tab, "LONGITUDE", 0, 230, 365, 40, &lv_font_montserrat_36);
    lbl_lon_val = create_value_label(tab, "0.00000", 0, 270, 365, 55,
                                     &lv_font_montserrat_48);

    /* ── Separator 3 (y=327) ── */
    create_separator(tab, 327);

    /* ALTITUDE section */
    create_section_title(tab, "ALTITUDE", 0, 345, 365, 40, &lv_font_montserrat_36);
    lbl_alt_val = create_value_label(tab, "0.0 m", 0, 390, 365, 50,
                                     &lv_font_montserrat_48);
}

/**
 * Build Tab 2 "GPS Accuracy" -- mirrors YAML `name: GPS`
 *
 * Layout (full 448px height, tab bar removed):
 *   y=15   Fix status (right) + Sat count (left)   -- 40pt
 *   y=80   Satellite bar chart (370x40)
 *   y=140  ── red separator ──
 *   y=175  HDOP / VDOP / PDOP column headers (30pt)
 *   y=220  HDOP / VDOP / PDOP values (40pt)
 *   y=295  ── red separator ──
 *   y=330  H.Acc / V.Acc headers (20pt)
 *   y=370  H.Acc / V.Acc values (40pt)
 */
static void build_tab_gps(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    /* Fix status (right) */
    lbl_fix_gps = lv_label_create(tab);
    lv_obj_set_pos(lbl_fix_gps, 222, 15);
    lv_label_set_text(lbl_fix_gps, "No Fix");
    lv_obj_set_style_text_color(lbl_fix_gps, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl_fix_gps, &lv_font_montserrat_40, 0);

    /* Sat count (left) */
    lbl_sats_gps = lv_label_create(tab);
    lv_obj_set_pos(lbl_sats_gps, 0, 15);
    lv_label_set_text(lbl_sats_gps, "00 / 00");
    lv_label_set_long_mode(lbl_sats_gps, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(lbl_sats_gps, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl_sats_gps, &lv_font_montserrat_40, 0);

    /* Satellite bar (used / visible) */
    bar_sats = lv_bar_create(tab);
    lv_obj_set_pos(bar_sats, 0, 80);
    lv_obj_set_size(bar_sats, 370, 40);
    lv_bar_set_range(bar_sats, 0, 24);
    lv_bar_set_value(bar_sats, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_sats, lv_color_hex(COL_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_sats, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_sats, lv_color_hex(COL_BAR_IND), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_sats, LV_OPA_COVER, LV_PART_INDICATOR);

    /* ── Separator (y=140) ── */
    create_separator(tab, 140);

    /* DOP column headers */
    create_section_title(tab, "HDOP",  0,   175, 122, 30, &lv_font_montserrat_30);
    create_section_title(tab, "VDOP",  128, 175, 122, 30, &lv_font_montserrat_30);
    create_section_title(tab, "PDOP",  251, 175, 117, 30, &lv_font_montserrat_30);

    /* DOP values */
    lbl_hdop_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_hdop_val, 11, 220);
    lv_label_set_text(lbl_hdop_val, "0.00");
    lv_obj_set_style_text_color(lbl_hdop_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_hdop_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_hdop_val, &lv_font_montserrat_40, 0);

    lbl_vdop_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_vdop_val, 133, 220);
    lv_label_set_text(lbl_vdop_val, "0.00");
    lv_obj_set_style_text_color(lbl_vdop_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl_vdop_val, &lv_font_montserrat_40, 0);

    lbl_pdop_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_pdop_val, 260, 220);
    lv_label_set_text(lbl_pdop_val, "0.00");
    lv_obj_set_style_text_color(lbl_pdop_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl_pdop_val, &lv_font_montserrat_40, 0);

    /* ── Separator (y=295) ── */
    create_separator(tab, 295);

    /* Accuracy headers */
    create_section_title(tab, "H.Acc", 51,  330, 80, 20, &lv_font_montserrat_20);
    create_section_title(tab, "V.Acc", 250, 330, 80, 20, &lv_font_montserrat_20);

    /* Accuracy values */
    lbl_hacc_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_hacc_val, 14, 370);
    lv_label_set_text(lbl_hacc_val, "0.0 m");
    lv_obj_set_style_text_color(lbl_hacc_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_hacc_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_hacc_val, &lv_font_montserrat_40, 0);

    lbl_vacc_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_vacc_val, 220, 370);
    lv_label_set_text(lbl_vacc_val, "0.0 m");
    lv_label_set_long_mode(lbl_vacc_val, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(lbl_vacc_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_vacc_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_vacc_val, &lv_font_montserrat_40, 0);
}

/**
 * Build Tab 3 "Speed" -- mirrors YAML `name: Speed`
 *
 * Single large centered speed value (mph).
 * Conversion: m/s * 2.23694 = mph.
 * Vertically centered in full 448px height (tab bar removed).
 *
 * Uses custom font_speed_144 (144px Montserrat, digits + period only).
 * "99.9" at 144px ≈ 310px wide, fits 368px display.
 *
 * Layout (448px height):
 *   Content: 144 + 10 + 30 = 184px
 *   y_start = (448-184)/2 = 132
 */
static void build_tab_speed(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    /* Speed value -- 144px font, centered */
    lbl_speed_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_speed_val, 0, 132);
    lv_obj_set_size(lbl_speed_val, DISP_W, 150);
    lv_label_set_text(lbl_speed_val, "0.0");
    lv_obj_set_style_text_color(lbl_speed_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_speed_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_speed_val, &font_speed_144, 0);

    /* "mph" unit label below */
    lv_obj_t *lbl_unit = lv_label_create(tab);
    lv_obj_set_pos(lbl_unit, 0, 286);
    lv_obj_set_size(lbl_unit, DISP_W, 30);
    lv_label_set_text(lbl_unit, "mph");
    lv_obj_set_style_text_color(lbl_unit, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_30, 0);
}

/**
 * Build Tab 4 "Network" -- mirrors YAML `name: Network`
 *
 * Layout (centred in full 448px height):
 *   y=80   ── top red separator ──
 *   y=105  "GPS ESP32 IP" title (30pt)
 *   y=150  Sender IP value (48pt)
 *   y=230  ── middle red separator ──
 *   y=255  "This Device" title (30pt)
 *   y=300  Device IP value (48pt)
 */
static void build_tab_network(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    /*
     * Vertically centered layout (no top separator):
     *   Content block: 40+5+60 +20+10+15+ 40+5+60 = 255px
     *   start_y = (448-255)/2 ≈ 97
     */

    /* Sender IP title */
    create_section_title(tab, "GPS ESP32 IP", 0, 97, DISP_W, 40,
                         &lv_font_montserrat_30);

    /* Sender IP value */
    lbl_sender_ip = lv_label_create(tab);
    lv_obj_set_pos(lbl_sender_ip, 0, 142);
    lv_obj_set_size(lbl_sender_ip, DISP_W, 60);
    lv_label_set_text(lbl_sender_ip, "0.0.0.0");
    lv_obj_set_style_text_color(lbl_sender_ip, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_sender_ip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_sender_ip, &lv_font_montserrat_48, 0);

    /* Separator */
    create_separator(tab, 222);

    /* Device IP title */
    create_section_title(tab, "This Device", 0, 247, DISP_W, 40,
                         &lv_font_montserrat_30);

    /* Device IP value */
    lbl_device_ip = lv_label_create(tab);
    lv_obj_set_pos(lbl_device_ip, 0, 292);
    lv_obj_set_size(lbl_device_ip, DISP_W, 60);
    lv_label_set_text(lbl_device_ip, "0.0.0.0");
    lv_obj_set_style_text_color(lbl_device_ip, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_device_ip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_device_ip, &lv_font_montserrat_48, 0);
}

/* ── Settings event callbacks ───────────────────────────────────────── */

/**
 * Brightness slider changed.
 * YAML formula: brightness = 10 + (slider_value * 0.9)
 * Slider range: 0..100 -> brightness: 10%..100%
 */
static void brightness_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    float brightness = 10.0f + (val * 0.9f);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f%%", brightness);
    lv_label_set_text(lbl_brightness_val, buf);

    ESP_LOGI(TAG, "Brightness slider -> %.0f%%", brightness);
    if (s_cb && s_cb->on_brightness_changed) {
        s_cb->on_brightness_changed(brightness);
    }
}

/**
 * Dim timeout slider changed.
 * Maps slider index 0..8 to timeout_steps[] = {5,10,15,30,60,120,180,240,300}
 */
static void dim_timeout_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int idx = lv_slider_get_value(slider);
    if (idx < 0) idx = 0;
    if (idx > TIMEOUT_SLIDER_MAX) idx = TIMEOUT_SLIDER_MAX;
    int timeout = timeout_steps[idx];

    char buf[32];
    if (timeout < 60) {
        snprintf(buf, sizeof(buf), "%ds", timeout);
    } else {
        snprintf(buf, sizeof(buf), "%dm", timeout / 60);
    }
    lv_label_set_text(lbl_dim_timeout_val, buf);

    ESP_LOGI(TAG, "Dim timeout slider -> index %d (%ds)", idx, timeout);
    if (s_cb && s_cb->on_dim_timeout_changed) {
        s_cb->on_dim_timeout_changed(timeout);
    }
}

/**
 * Sleep timeout slider changed.
 * Same mapping as dim timeout.
 */
static void sleep_timeout_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int idx = lv_slider_get_value(slider);
    if (idx < 0) idx = 0;
    if (idx > TIMEOUT_SLIDER_MAX) idx = TIMEOUT_SLIDER_MAX;
    int timeout = timeout_steps[idx];

    char buf[32];
    if (timeout < 60) {
        snprintf(buf, sizeof(buf), "%ds", timeout);
    } else {
        snprintf(buf, sizeof(buf), "%dm", timeout / 60);
    }
    lv_label_set_text(lbl_sleep_timeout_val, buf);

    ESP_LOGI(TAG, "Sleep timeout slider -> index %d (%ds)", idx, timeout);
    if (s_cb && s_cb->on_sleep_timeout_changed) {
        s_cb->on_sleep_timeout_changed(timeout);
    }
}

/**
 * Dim enable switch toggled.
 */
static void dim_enable_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ESP_LOGI(TAG, "Dim timeout %s", checked ? "enabled" : "disabled");
    if (s_cb && s_cb->on_dim_enable_changed) {
        s_cb->on_dim_enable_changed(checked);
    }
}

/**
 * Sleep enable switch toggled.
 */
static void sleep_enable_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ESP_LOGI(TAG, "Sleep timeout %s", checked ? "enabled" : "disabled");
    if (s_cb && s_cb->on_sleep_enable_changed) {
        s_cb->on_sleep_enable_changed(checked);
    }
}

/**
 * Turn-off button pressed -- manual display off.
 */
static void turnoff_btn_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Turn-off button pressed");
    if (s_cb && s_cb->on_turnoff_pressed) {
        s_cb->on_turnoff_pressed();
    }
}

/**
 * Helper: create a styled slider matching YAML pattern.
 *
 * YAML slider style:
 *   bg_color: 0x190101, border_color: 0xE10522
 *   knob: bg_color: 0xE10522, border_width: 2, radius: 100
 *   indicator: bg_color: 0x6E0211
 */
static lv_obj_t *create_styled_slider(lv_obj_t *parent,
                                       int x, int y, int w, int h,
                                       int min_val, int max_val, int init_val) {
    lv_obj_t *s = lv_slider_create(parent);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_size(s, w, h);
    lv_slider_set_range(s, min_val, max_val);
    lv_slider_set_value(s, init_val, LV_ANIM_OFF);

    /* Main track */
    lv_obj_set_style_bg_color(s, lv_color_hex(COL_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s, lv_color_hex(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s, 1, LV_PART_MAIN);

    /* Indicator (filled portion) */
    lv_obj_set_style_bg_color(s, lv_color_hex(COL_SLIDER_IND), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);

    /* Knob */
    lv_obj_set_style_bg_color(s, lv_color_hex(COL_KNOB), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(s, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(s, lv_color_hex(COL_KNOB), LV_PART_KNOB);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    return s;
}

/**
 * Helper: create a styled switch matching YAML pattern.
 *
 * YAML switch style:
 *   bg_color: 0x190101, border_width: 1, border_color: 0xE10522
 *   indicator: bg_color: 0xE10522
 *   knob: bg_color: 0xE10522, border_color: 0x9CA3AF, border_width: 1, radius: 9999
 */
static lv_obj_t *create_styled_switch(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, w, h);

    /* Track (unchecked) -- black background */
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, lv_color_hex(COL_BORDER), LV_PART_MAIN);

    /* Track (checked) -- dark red (must target CHECKED state to override theme) */
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_SW_OFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);

    /* Knob */
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_KNOB), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(sw, lv_color_hex(COL_KNOB_RING), LV_PART_KNOB);
    lv_obj_set_style_border_width(sw, 1, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    return sw;
}

/**
 * Build Tab 5 "Settings" -- mirrors YAML `name: Settings`
 *
 * Layout (fits 448px, no scrolling):
 *   y=5    "Brightness" label (left) + value (right)
 *   y=38   Brightness slider (0..100 -> 10%..100%)
 *   y=118  "Dim Timeout" label + value (y=114)
 *   y=151  Dim timeout slider (0..8 index)
 *   y=231  "Sleep Timeout" label + value (y=227)
 *   y=264  Sleep timeout slider (0..8 index)
 *   y=355  Turn-Off button | Dim switch | Sleep switch
 */
static void build_tab_settings(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    /* ── Brightness ─────────────────────────────────────────────── */
    create_section_title(tab, "Brightness", 0, 5, 185, 25, &lv_font_montserrat_20);

    lbl_brightness_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_brightness_val, 185, 5);
    lv_obj_set_size(lbl_brightness_val, 180, 30);
    lv_label_set_text(lbl_brightness_val, "35%");
    lv_obj_set_style_text_color(lbl_brightness_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_brightness_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_brightness_val, &lv_font_montserrat_30, 0);

    slider_brightness = create_styled_slider(tab, 0, 38, DISP_W, 55, 0, 100, 28);
    lv_obj_add_event_cb(slider_brightness, brightness_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Dim Timeout ────────────────────────────────────────────── */
    create_section_title(tab, "Dim Timeout", 0, 118, 185, 25,
                         &lv_font_montserrat_20);

    lbl_dim_timeout_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_dim_timeout_val, 185, 114);
    lv_obj_set_size(lbl_dim_timeout_val, 180, 30);
    lv_label_set_text(lbl_dim_timeout_val, "30s");
    lv_obj_set_style_text_color(lbl_dim_timeout_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_dim_timeout_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_dim_timeout_val, &lv_font_montserrat_30, 0);

    slider_dim = create_styled_slider(tab, 0, 151, DISP_W, 55, 0,
                                      TIMEOUT_SLIDER_MAX, 3);
    lv_obj_add_event_cb(slider_dim, dim_timeout_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Sleep Timeout ──────────────────────────────────────────── */
    create_section_title(tab, "Sleep Timeout", 0, 231, 185, 25,
                         &lv_font_montserrat_20);

    lbl_sleep_timeout_val = lv_label_create(tab);
    lv_obj_set_pos(lbl_sleep_timeout_val, 185, 227);
    lv_obj_set_size(lbl_sleep_timeout_val, 180, 30);
    lv_label_set_text(lbl_sleep_timeout_val, "1m");
    lv_obj_set_style_text_color(lbl_sleep_timeout_val, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(lbl_sleep_timeout_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_sleep_timeout_val, &lv_font_montserrat_30, 0);

    slider_sleep = create_styled_slider(tab, 0, 264, DISP_W, 55, 0,
                                        TIMEOUT_SLIDER_MAX, 4);
    lv_obj_add_event_cb(slider_sleep, sleep_timeout_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Bottom row: Turn-Off button + Dim switch + Sleep switch ── */

    /* Turn-Off button */
    btn_turnoff = lv_button_create(tab);
    lv_obj_set_pos(btn_turnoff, 3, 355);
    lv_obj_set_size(btn_turnoff, 130, 60);
    lv_obj_set_style_bg_color(btn_turnoff, lv_color_hex(COL_DARK), 0);
    lv_obj_set_style_bg_opa(btn_turnoff, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_turnoff, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(btn_turnoff, 1, 0);
    lv_obj_set_style_shadow_width(btn_turnoff, 0, 0);
    lv_obj_add_event_cb(btn_turnoff, turnoff_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_turnoff = lv_label_create(btn_turnoff);
    lv_label_set_text(lbl_turnoff, "Turn-Off");
    lv_obj_set_style_text_color(lbl_turnoff, lv_color_hex(COL_RED), 0);
    lv_obj_center(lbl_turnoff);

    /* Dim enable label + switch */
    create_section_title(tab, "Dim", 133, 355, 120, 20, &lv_font_montserrat_16);

    sw_dim_enable = create_styled_switch(tab, 145, 385, 100, 40);
    lv_obj_add_event_cb(sw_dim_enable, dim_enable_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Sleep enable label + switch */
    create_section_title(tab, "Sleep", 245, 355, 120, 20, &lv_font_montserrat_16);

    sw_sleep_enable = create_styled_switch(tab, 258, 385, 100, 40);
    lv_obj_add_event_cb(sw_sleep_enable, sleep_enable_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * Build Tab 6 "Reboot" -- mirrors YAML `name: Reboot`
 *
 * Single large reboot button centered vertically.
 */
static void reboot_btn_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Reboot button pressed");
    if (s_cb && s_cb->on_reboot_pressed) {
        s_cb->on_reboot_pressed();
    }
}

static void build_tab_reboot(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    btn_reboot = lv_button_create(tab);
    lv_obj_set_pos(btn_reboot, 0, 184);
    lv_obj_set_size(btn_reboot, DISP_W, 80);
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(COL_DARK), 0);
    lv_obj_set_style_bg_opa(btn_reboot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_reboot, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(btn_reboot, 1, 0);
    lv_obj_set_style_shadow_width(btn_reboot, 0, 0);
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn_reboot);
    lv_label_set_text(lbl, "Reboot");
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_30, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

/**
 * Released callback on the tabview content -- wraps around at boundaries.
 *
 * LV_EVENT_GESTURE doesn't fire because the scroll container consumes swipes.
 * Instead we use LV_EVENT_RELEASED, which fires after every finger lift,
 * and check lv_indev_get_gesture_dir() for the swipe direction.
 *
 * At the time of RELEASED, the active tab hasn't animated yet, so
 * lv_tabview_get_tab_active() still returns the tab the user was ON
 * when they swiped -- exactly what we need for boundary detection.
 */
static void tabview_wrap_released_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    uint32_t current = lv_tabview_get_tab_active(tabview);

    if (dir == LV_DIR_RIGHT && current == TAB_COUNT - 1) {
        lv_tabview_set_active(tabview, 0, LV_ANIM_ON);
    } else if (dir == LV_DIR_LEFT && current == 0) {
        lv_tabview_set_active(tabview, TAB_COUNT - 1, LV_ANIM_ON);
    }
}

/**
 * Tabview value-changed event -- fires when user swipes or taps a tab.
 * Reports the new active tab index through the callback.
 */
static void tabview_changed_cb(lv_event_t *e) {
    lv_obj_t *tv = lv_event_get_target(e);
    uint32_t idx = lv_tabview_get_tab_active(tv);
    ESP_LOGI(TAG, "Tab changed to %lu", (unsigned long)idx);
    if (s_cb && s_cb->on_tab_changed) {
        s_cb->on_tab_changed((int)idx);
    }
}

/**
 * Build the complete 6-tab UI.
 *
 * LVGL 9 tabview API:
 *   lv_tabview_create(parent)       -- creates tabview
 *   lv_tabview_set_tab_bar_position(tv, LV_DIR_BOTTOM)
 *   lv_tabview_set_tab_bar_size(tv, size_px)
 *   lv_tabview_add_tab(tv, "name")  -- returns tab content obj
 *
 * YAML reference:
 *   position: BOTTOM, size: 10%
 *   border_color: 0xE10522, border_width: 1
 *   tab_style: bg_color 0x000000, text_color 0x6E0211
 *     checked: bg_color 0x6E0211, text_color 0xE10522
 */
void ui_tabs_create(lv_obj_t *parent, const ui_tabs_callbacks_t *cb) {
    s_cb = cb;
    init_styles();

    /* ── Create tabview ─────────────────────────────────────────── */
    tabview = lv_tabview_create(parent);
    lv_obj_set_pos(tabview, 0, 0);
    lv_obj_set_size(tabview, DISP_W, DISP_H);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(tabview, 0);  /* hidden -- nav via BOOT button + swipe */

    /* Tabview background */
    lv_obj_set_style_bg_color(tabview, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tabview, 0, 0);  /* no border -- full screen */

    /* Hide the tab bar completely */
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_add_flag(tab_bar, LV_OBJ_FLAG_HIDDEN);

    /* Remove default padding from content area so tabs fill full screen */
    lv_obj_t *tv_content = lv_tabview_get_content(tabview);
    lv_obj_set_style_pad_all(tv_content, 0, 0);

    /* ── Add the 6 tabs ─────────────────────────────────────────── */
    lv_obj_t *tab_home     = lv_tabview_add_tab(tabview, "Home");
    lv_obj_t *tab_gps      = lv_tabview_add_tab(tabview, "GPS");
    lv_obj_t *tab_speed    = lv_tabview_add_tab(tabview, "Speed");
    lv_obj_t *tab_network  = lv_tabview_add_tab(tabview, "Network");
    lv_obj_t *tab_settings = lv_tabview_add_tab(tabview, "Settings");
    lv_obj_t *tab_reboot   = lv_tabview_add_tab(tabview, "Reboot");

    /* ── Build each tab's contents ──────────────────────────────── */
    build_tab_home(tab_home);
    build_tab_gps(tab_gps);
    build_tab_speed(tab_speed);
    build_tab_network(tab_network);
    build_tab_settings(tab_settings);
    build_tab_reboot(tab_reboot);

    /* ── Listen for tab changes ─────────────────────────────────── */
    lv_obj_add_event_cb(tabview, tabview_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Wrap-around on swipe past boundary ─────────────────────── */
    lv_obj_add_event_cb(tv_content, tabview_wrap_released_cb,
                        LV_EVENT_RELEASED, NULL);

    ESP_LOGI(TAG, "UI tabs created (%dx%d, %d tabs)", DISP_W, DISP_H, TAB_COUNT);
}

/**
 * Push a full GPS data packet into all tab widgets.
 *
 * Field mapping matches the YAML on_receive lambda exactly:
 *   - lat/lon/alt -> Home tab labels (%.5f, %.5f, %.1f m)
 *   - local_time -> Home tab time label
 *   - fix_type -> status string on Home + GPS tabs
 *   - sats/sats_visible -> "N/V" on Home + GPS tabs, bar on GPS tab
 *   - hdop/vdop/pdop -> GPS tab (%.2f)
 *   - h_acc/v_acc -> GPS tab (%.1f m)
 *   - speed -> Speed tab (m/s * 2.23694 = mph, %.1f)
 *   - sender_ip -> Network tab (dotted quad)
 */
void ui_tabs_update_gps(const gps_data_t *data) {
    if (!data || !tabview) return;

    char buf[64];

    /* ── Home tab ───────────────────────────────────────────────── */
    lv_label_set_text(lbl_time, data->local_time);

    /* Fix status string */
    const char *fix_str = "No Fix";
    switch (data->fix_type) {
        case 0: fix_str = "No Fix";         break;
        case 1: fix_str = "Dead Reckoning"; break;
        case 2: fix_str = "2D Fix";         break;
        case 3: fix_str = "3D Fix";         break;
    }
    lv_label_set_text(lbl_fix_home, fix_str);
    lv_label_set_text(lbl_fix_gps, fix_str);

    /* Satellite count "used / visible" */
    snprintf(buf, sizeof(buf), "%d/%d", data->sats, data->sats_visible);
    lv_label_set_text(lbl_sats_home, buf);
    lv_label_set_text(lbl_sats_gps, buf);

    /* Lat / Lon / Alt */
    snprintf(buf, sizeof(buf), "%.5f", data->lat);
    lv_label_set_text(lbl_lat_val, buf);

    snprintf(buf, sizeof(buf), "%.5f", data->lon);
    lv_label_set_text(lbl_lon_val, buf);

    snprintf(buf, sizeof(buf), "%.1f m", data->alt);
    lv_label_set_text(lbl_alt_val, buf);

    /* ── GPS Accuracy tab ───────────────────────────────────────── */

    /* Satellite bar: range = 0..sats_visible (or max_sats), value = sats */
    int bar_max = data->sats_visible > 0 ? data->sats_visible : 24;
    lv_bar_set_range(bar_sats, 0, bar_max);
    lv_bar_set_value(bar_sats, data->sats, LV_ANIM_OFF);
    lv_obj_invalidate(bar_sats);

    /* DOP values */
    snprintf(buf, sizeof(buf), "%.2f", data->hdop);
    lv_label_set_text(lbl_hdop_val, buf);

    snprintf(buf, sizeof(buf), "%.2f", data->vdop);
    lv_label_set_text(lbl_vdop_val, buf);

    snprintf(buf, sizeof(buf), "%.2f", data->pdop);
    lv_label_set_text(lbl_pdop_val, buf);

    /* Accuracy values */
    snprintf(buf, sizeof(buf), "%.1f m", data->h_acc);
    lv_label_set_text(lbl_hacc_val, buf);

    snprintf(buf, sizeof(buf), "%.1f m", data->v_acc);
    lv_label_set_text(lbl_vacc_val, buf);

    /* ── Speed tab ──────────────────────────────────────────────── */
    float speed_mph = data->speed * 2.23694f;
    snprintf(buf, sizeof(buf), "%.1f", speed_mph);
    lv_label_set_text(lbl_speed_val, buf);

    /* ── Network tab (sender IP) ────────────────────────────────── */
    uint8_t ip[4];
    ip[0] = (data->station_ip >>  0) & 0xFF;
    ip[1] = (data->station_ip >>  8) & 0xFF;
    ip[2] = (data->station_ip >> 16) & 0xFF;
    ip[3] = (data->station_ip >> 24) & 0xFF;
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    lv_label_set_text(lbl_sender_ip, buf);
}

void ui_tabs_set_device_ip(const char *ip_str) {
    if (lbl_device_ip && ip_str) {
        lv_label_set_text(lbl_device_ip, ip_str);
    }
}

void ui_tabs_led_on(void) {
    if (led_indicator) lv_led_on(led_indicator);
}

void ui_tabs_led_off(void) {
    if (led_indicator) lv_led_off(led_indicator);
}

void ui_tabs_set_active(int tab_index, bool animate) {
    if (tabview && tab_index >= 0 && tab_index < TAB_COUNT) {
        lv_tabview_set_active(tabview, (uint32_t)tab_index,
                                  animate ? LV_ANIM_ON : LV_ANIM_OFF);
    }
}

int ui_tabs_get_active(void) {
    if (!tabview) return 0;
    return (int)lv_tabview_get_tab_active(tabview);
}

int ui_tabs_get_tab_count(void) {
    return TAB_COUNT;
}

/**
 * Restore persisted settings into the UI widgets.
 * Call once after ui_tabs_create(), from the main init sequence.
 *
 * This mirrors the ESPHome on_boot lambda that reads NVS values
 * and pushes them into sliders/switches.
 */
void ui_tabs_restore_settings(float brightness_pct,
                              int dim_timeout_s, int sleep_timeout_s,
                              bool dim_enabled, bool sleep_enabled) {

    /* ── Brightness slider ──────────────────────────────────────── */
    if (slider_brightness && lbl_brightness_val) {
        int slider_val = (int)((brightness_pct - 10.0f) / 0.9f);
        if (slider_val < 0) slider_val = 0;
        if (slider_val > 100) slider_val = 100;
        lv_slider_set_value(slider_brightness, slider_val, LV_ANIM_OFF);

        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f%%", brightness_pct);
        lv_label_set_text(lbl_brightness_val, buf);
    }

    /* ── Dim timeout slider ─────────────────────────────────────── */
    if (slider_dim && lbl_dim_timeout_val) {
        int idx = 3;  /* default 30s */
        for (int i = 0; i < TIMEOUT_STEP_COUNT; i++) {
            if (dim_timeout_s <= timeout_steps[i]) { idx = i; break; }
        }
        lv_slider_set_value(slider_dim, idx, LV_ANIM_OFF);

        char buf[32];
        if (timeout_steps[idx] < 60) {
            snprintf(buf, sizeof(buf), "%ds", timeout_steps[idx]);
        } else {
            snprintf(buf, sizeof(buf), "%dm", timeout_steps[idx] / 60);
        }
        lv_label_set_text(lbl_dim_timeout_val, buf);
    }

    /* ── Sleep timeout slider ───────────────────────────────────── */
    if (slider_sleep && lbl_sleep_timeout_val) {
        int idx = 4;  /* default 60s */
        for (int i = 0; i < TIMEOUT_STEP_COUNT; i++) {
            if (sleep_timeout_s <= timeout_steps[i]) { idx = i; break; }
        }
        lv_slider_set_value(slider_sleep, idx, LV_ANIM_OFF);

        char buf[32];
        if (timeout_steps[idx] < 60) {
            snprintf(buf, sizeof(buf), "%ds", timeout_steps[idx]);
        } else {
            snprintf(buf, sizeof(buf), "%dm", timeout_steps[idx] / 60);
        }
        lv_label_set_text(lbl_sleep_timeout_val, buf);
    }

    /* ── Dim enable switch ──────────────────────────────────────── */
    if (sw_dim_enable) {
        if (dim_enabled) {
            lv_obj_add_state(sw_dim_enable, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw_dim_enable, LV_STATE_CHECKED);
        }
    }

    /* ── Sleep enable switch ────────────────────────────────────── */
    if (sw_sleep_enable) {
        if (sleep_enabled) {
            lv_obj_add_state(sw_sleep_enable, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw_sleep_enable, LV_STATE_CHECKED);
        }
    }

    ESP_LOGI(TAG, "Settings restored: bright=%.0f%%, dim=%ds, sleep=%ds, "
             "dim_en=%d, sleep_en=%d",
             brightness_pct, dim_timeout_s, sleep_timeout_s,
             dim_enabled, sleep_enabled);
}
