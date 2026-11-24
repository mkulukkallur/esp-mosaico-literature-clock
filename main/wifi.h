#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load the Wi-Fi network saved by Mosaico Vibe Mode (sysmeta "shared" namespace,
 * falling back to its older "wifi" namespace). Returns false if none is saved.
 */
bool wifi_load_vibe_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len);

/** Init NVS + netif + Wi-Fi station and start connecting (auto-reconnects). */
void wifi_start(const char *ssid, const char *password);

/** Block until an IP address is obtained (or timeout). */
bool wifi_wait_connected(TickType_t timeout);

bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
