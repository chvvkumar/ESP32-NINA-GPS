#include "web_server.h"
#include "types.h"
#include "wifi.h"
#include "display.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "lvgl.h"

static const char *TAG = "webserver";

// ---------------------------------------------------------------------------
// GET / -- Minimal status page
// ---------------------------------------------------------------------------
static esp_err_t root_handler(httpd_req_t *req)
{
    char *buf = malloc(2048);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int len = snprintf(buf, 2048,
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>AMOLED GPS Receiver</title>"
        "<style>"
        "body{font-family:monospace;background:#111;color:#eee;padding:1em}"
        "h1{color:#0ef}a{color:#0ef}"
        "button{padding:8px 16px;background:#0ef;color:#111;border:none;"
        "cursor:pointer;font-family:monospace;font-weight:bold}"
        "button:disabled{opacity:.5;cursor:wait}"
        "#snap-img img{max-width:100%%;border:1px solid #333;margin-top:8px}"
        "</style></head><body>"
        "<h1>AMOLED GPS Receiver</h1>"
        "<p>Status: %s</p>"
        "<p>Fix: %s | Sats: %u/%u</p>"
        "<p>Lat: %.7f | Lon: %.7f</p>"
        "<p>Alt: %.1f m | Speed: %.1f mph</p>"
        "<p>Sender IP: %s</p>"
        "<p>Receiver IP: %s | MAC: %s</p>"
        "<p><a href='/status'>JSON Status</a></p>"
        "<hr style='border-color:#333'>"
        "<h2 style='color:#0ef'>Screenshot</h2>"
        "<button id='sb' onclick='ss()'>Take Screenshot</button>"
        "<div id='snap-img'></div>"
        "<script>"
        "function ss(){"
        "var b=document.getElementById('sb'),d=document.getElementById('snap-img');"
        "b.disabled=true;b.textContent='Capturing...';"
        "fetch('/screenshot').then(function(r){"
        "if(!r.ok)throw new Error(r.statusText);return r.blob();"
        "}).then(function(bl){"
        "var u=URL.createObjectURL(bl);"
        "d.innerHTML='<img src=\"'+u+'\">"
        "<br><a href=\"'+u+'\" download=\"screenshot.bmp\" style=\"color:#0ef\">Save</a>';"
        "b.disabled=false;b.textContent='Take Screenshot';"
        "}).catch(function(e){"
        "d.innerHTML='<p style=\"color:red\">Error: '+e.message+'</p>';"
        "b.disabled=false;b.textContent='Take Screenshot';"
        "});}"
        "</script>"
        "</body></html>",
        g_gps.connected ? "Connected" : "No Signal",
        g_gps.fix_status, g_gps.sats, g_gps.sats_visible,
        g_gps.lat, g_gps.lon,
        g_gps.alt, g_gps.speed_mph,
        g_gps.station_ip_str,
        wifi_get_ip_str(), wifi_get_mac_str());

    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, buf, len);
    free(buf);
    return ret;
}

// ---------------------------------------------------------------------------
// GET /status -- JSON API
// ---------------------------------------------------------------------------
static esp_err_t status_handler(httpd_req_t *req)
{
    char buf[768];
    int len = snprintf(buf, sizeof(buf),
        "{"
        "\"connected\":%s,"
        "\"fix_type\":%u,"
        "\"fix_status\":\"%s\","
        "\"lat\":%.7f,"
        "\"lon\":%.7f,"
        "\"alt\":%.1f,"
        "\"speed\":%.2f,"
        "\"speed_mph\":%.2f,"
        "\"heading\":%.1f,"
        "\"sats\":%u,"
        "\"sats_visible\":%u,"
        "\"local_time\":\"%s\","
        "\"pdop\":%.2f,"
        "\"hdop\":%.2f,"
        "\"vdop\":%.2f,"
        "\"h_acc\":%.2f,"
        "\"v_acc\":%.2f,"
        "\"station_ip\":\"%s\","
        "\"receiver_ip\":\"%s\","
        "\"receiver_mac\":\"%s\","
        "\"ping_counter\":%u,"
        "\"time_synced\":%s"
        "}",
        g_gps.connected ? "true" : "false",
        g_gps.fix_type,
        g_gps.fix_status,
        g_gps.lat, g_gps.lon, g_gps.alt,
        g_gps.speed, g_gps.speed_mph, g_gps.heading,
        g_gps.sats, g_gps.sats_visible,
        g_gps.local_time,
        g_gps.pdop, g_gps.hdop, g_gps.vdop,
        g_gps.h_acc, g_gps.v_acc,
        g_gps.station_ip_str,
        wifi_get_ip_str(), wifi_get_mac_str(),
        (unsigned)g_gps.ping_counter,
        g_gps.time_synced ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, len);
}

// ---------------------------------------------------------------------------
// POST /ota -- Firmware upload (chunked receive)
// ---------------------------------------------------------------------------
static esp_err_t ota_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update started, content length: %d", req->content_len);

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                                  &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    int received;

    while (remaining > 0) {
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA receive error");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Receive error");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Set boot partition failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OTA success, rebooting...");
    ESP_LOGI(TAG, "OTA complete, rebooting in 1s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;  // unreachable
}

// ---------------------------------------------------------------------------
// POST /reboot
// ---------------------------------------------------------------------------
static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Rebooting...");
    ESP_LOGI(TAG, "Reboot requested via HTTP");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;  // unreachable
}

// ---------------------------------------------------------------------------
// GET /screenshot -- Capture display as BMP
// ---------------------------------------------------------------------------

/* Snapshot must run in a task with enough stack for LVGL's render pipeline.
 * The httpd task only has 4 KB which overflows during lv_snapshot_take(). */
typedef struct {
    lv_draw_buf_t *result;
    TaskHandle_t    caller;
} snap_req_t;

static void snapshot_task(void *arg)
{
    snap_req_t *r = (snap_req_t *)arg;
    r->result = NULL;
    if (display_lock(LVGL_LOCK_TIMEOUT_MS * 2)) {
        r->result = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
        display_unlock();
    }
    xTaskNotifyGive(r->caller);
    vTaskDelete(NULL);
}

static esp_err_t screenshot_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Screenshot requested");

    /* Run snapshot in a dedicated task with 10 KB stack */
    snap_req_t sr = { .result = NULL, .caller = xTaskGetCurrentTaskHandle() };
    BaseType_t ok = xTaskCreate(snapshot_task, "snap", 10240, &sr, 2, NULL);
    if (ok != pdPASS) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task create failed");
        return ESP_FAIL;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));

    lv_draw_buf_t *snap = sr.result;
    if (!snap || !snap->data) {
        ESP_LOGE(TAG, "Snapshot failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot failed");
        return ESP_FAIL;
    }

    int w = snap->header.w;
    int h = snap->header.h;
    int src_stride = snap->header.stride;
    ESP_LOGI(TAG, "Snapshot: %dx%d, stride=%d", w, h, src_stride);

    /* BMP row: 24-bit RGB, padded to 4-byte boundary */
    int row_rgb = w * 3;
    int row_pad = (4 - (row_rgb % 4)) % 4;
    int row_stride = row_rgb + row_pad;
    uint32_t pixel_size = (uint32_t)row_stride * h;
    uint32_t file_size = 54 + pixel_size;

    /* Build BMP file header (14) + DIB header (40) = 54 bytes */
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));

    /* File header */
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  =  file_size        & 0xFF;
    hdr[3]  = (file_size >> 8)  & 0xFF;
    hdr[4]  = (file_size >> 16) & 0xFF;
    hdr[5]  = (file_size >> 24) & 0xFF;
    hdr[10] = 54;  /* pixel data offset */

    /* DIB header (BITMAPINFOHEADER) */
    hdr[14] = 40;  /* DIB header size */
    hdr[18] =  w       & 0xFF;
    hdr[19] = (w >> 8) & 0xFF;
    hdr[22] =  h       & 0xFF;
    hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1;   /* color planes */
    hdr[28] = 24;  /* bits per pixel */
    hdr[34] =  pixel_size        & 0xFF;
    hdr[35] = (pixel_size >> 8)  & 0xFF;
    hdr[36] = (pixel_size >> 16) & 0xFF;
    hdr[37] = (pixel_size >> 24) & 0xFF;

    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    /* Send BMP header */
    httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr));

    /* Allocate one-row conversion buffer */
    uint8_t *row_buf = malloc(row_stride);
    if (!row_buf) {
        ESP_LOGE(TAG, "Row buffer alloc failed");
        lv_draw_buf_destroy(snap);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }

    /* Stream rows bottom-to-top (BMP stores bottom row first) */
    const uint8_t *src = snap->data;
    for (int y = h - 1; y >= 0; y--) {
        const uint16_t *row = (const uint16_t *)(src + y * src_stride);
        for (int x = 0; x < w; x++) {
            uint16_t px = row[x];
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5)  & 0x3F;
            uint8_t b5 =  px        & 0x1F;
            /* BMP pixel order is BGR */
            row_buf[x * 3 + 0] = (b5 << 3) | (b5 >> 2);
            row_buf[x * 3 + 1] = (g6 << 2) | (g6 >> 4);
            row_buf[x * 3 + 2] = (r5 << 3) | (r5 >> 2);
        }
        memset(row_buf + row_rgb, 0, row_pad);
        httpd_resp_send_chunk(req, (const char *)row_buf, row_stride);
    }

    free(row_buf);
    lv_draw_buf_destroy(snap);

    /* End chunked response */
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "Screenshot sent (%lu bytes)", (unsigned long)file_size);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// URI registrations
// ---------------------------------------------------------------------------
static const httpd_uri_t uri_root = {
    .uri     = "/",
    .method  = HTTP_GET,
    .handler = root_handler,
};

static const httpd_uri_t uri_status = {
    .uri     = "/status",
    .method  = HTTP_GET,
    .handler = status_handler,
};

static const httpd_uri_t uri_ota = {
    .uri     = "/ota",
    .method  = HTTP_POST,
    .handler = ota_handler,
};

static const httpd_uri_t uri_reboot = {
    .uri     = "/reboot",
    .method  = HTTP_POST,
    .handler = reboot_handler,
};

static const httpd_uri_t uri_screenshot = {
    .uri     = "/screenshot",
    .method  = HTTP_GET,
    .handler = screenshot_handler,
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port    = 80;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_ota);
    httpd_register_uri_handler(server, &uri_reboot);
    httpd_register_uri_handler(server, &uri_screenshot);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}
