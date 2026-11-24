#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_events;
#define CONNECTED_BIT BIT0

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        ESP_LOGW(TAG, "Disconnected, retrying...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

static void ok_or_already(esp_err_t err, const char *what)
{
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
}

#define SYSMETA_PARTITION "sysmeta"

static bool read_pair(const char *ns, const char *ssid_key, const char *pw_key,
                      char *ssid, size_t ssid_len, char *pw, size_t pw_len)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(SYSMETA_PARTITION, ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t a = ssid_len, b = pw_len;
    bool ok = nvs_get_str(h, ssid_key, ssid, &a) == ESP_OK && ssid[0] &&
              nvs_get_str(h, pw_key, pw, &b) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool wifi_load_vibe_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    ssid[0] = password[0] = '\0';
    /* Usually already initialised by iris_ota_support_start(). */
    (void)nvs_flash_init_partition(SYSMETA_PARTITION);
    if (read_pair("shared", "wifi_ssid", "wifi_password", ssid, ssid_len, password, password_len) ||
        read_pair("wifi", "ssid", "password", ssid, ssid_len, password, password_len)) {
        return true;
    }
    ssid[0] = password[0] = '\0';
    return false;
}

void wifi_start(const char *ssid, const char *password)
{
    s_events = xEventGroupCreate();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ok_or_already(esp_netif_init(), "esp_netif_init");
    ok_or_already(esp_event_loop_create_default(), "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, password, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Always-on display, as in Flight Radar: keep the radio awake. Modem sleep
     * re-initialises the PHY on every wake-up, which also needs internal RAM. */
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
}

bool wifi_wait_connected(TickType_t timeout)
{
    return xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE, timeout) & CONNECTED_BIT;
}

bool wifi_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & CONNECTED_BIT);
}
