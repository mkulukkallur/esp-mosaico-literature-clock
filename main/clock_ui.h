#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Literature Clock screen (LVGL 9, 480x480).
 *
 * Shows the quote for the current minute with its time phrase highlighted and
 * "— Title, Author" underneath. Tap: next quote for this minute. Long-press:
 * switch between the dark and paper themes (remembered in NVS).
 *
 * clock_ui_create() must be called with the LVGL lock held; afterwards all
 * work runs in LVGL timers. clock_ui_set_status() may be called from any task.
 */
void clock_ui_create(void);

typedef enum {
    CLOCK_STATUS_CONNECTING, /**< waiting for Wi-Fi / the time */
    CLOCK_STATUS_NO_WIFI,    /**< no Wi-Fi configured: cannot get the time */
} clock_status_t;

void clock_ui_set_status(clock_status_t status);

#ifdef __cplusplus
}
#endif
