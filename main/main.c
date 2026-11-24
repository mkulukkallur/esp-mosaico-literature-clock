/*
 * ESP-Mosaico Literature Clock
 * Tells the time with a quote from a book for every minute of the day, the
 * time phrase highlighted. Inspired by the Kindle "Literary Clock" projects.
 *
 * Runs as a normal Mosaico application next to Vibe Mode: ESP-Iris and the
 * recovery support start first, so the board can always return to Vibe Mode
 * and accept updates even if the clock fails later.
 */
#include <string.h>
#include "bsp/esp_mosaico.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "clock_time.h"
#include "clock_ui.h"
#include "wifi.h"

static const char *TAG = "literature_clock";

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    /* Vibe Mode / update support must stay reachable whatever happens next. */
    iris_ota_support_start();

    clock_time_init(); /* timezone before the UI first reads the time */

    /* Larger LVGL task stack than the adapter default: Tiny TTF rasterises
     * glyphs and the quote is re-measured inside LVGL timers. */
    bsp_display_config_t disp_cfg = BSP_DISPLAY_DEFAULT_CONFIG();
    disp_cfg.task_stack_size = 16 * 1024;
    lv_display_t *display = bsp_display_start_with_config(&disp_cfg);
    if (!display) {
        ESP_LOGE(TAG, "Display unavailable; Vibe Mode RPC remains active");
        return;
    }
    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    clock_ui_create();
    bsp_display_unlock();

    /* Wi-Fi (for the time): menuconfig override if set, otherwise Vibe Mode's. */
    static char ssid[33], password[65];
    if (CONFIG_LC_WIFI_SSID[0]) {
        strlcpy(ssid, CONFIG_LC_WIFI_SSID, sizeof(ssid));
        strlcpy(password, CONFIG_LC_WIFI_PASSWORD, sizeof(password));
        ESP_LOGI(TAG, "Using Wi-Fi from menuconfig");
    } else if (wifi_load_vibe_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "Using Wi-Fi saved in Vibe Mode");
    } else {
        ESP_LOGE(TAG, "No Wi-Fi configured: set it up in Vibe Mode or in menuconfig");
        clock_ui_set_status(CLOCK_STATUS_NO_WIFI);
        return;
    }
    wifi_start(ssid, password);
    clock_time_start_network();
    ESP_LOGI(TAG, "Literature clock running (timezone: %s)", clock_time_zone_desc());
}
