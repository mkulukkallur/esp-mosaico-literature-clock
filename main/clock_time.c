#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "clock_time.h"
#include "mosaico_shared.h"
#include "tz_table.h"
#include "wifi.h"

static const char *TAG = "clock_time";

#define SHARED_KEY "clock_timezone"
#define USER_AGENT "esp-mosaico-literature-clock/1.0"
#define RESP_MAX 2048
#define TZ_SETTLE_TIMEOUT_US (20 * 1000000LL) /* show the time anyway after this */
#define VALID_EPOCH 1700000000                /* Nov 2023: anything earlier is "not set" */

static struct {
    char tz[72];             /* POSIX rule found by the lookup task */
    char desc[48];           /* where the zone came from */
    bool pending;            /* tz written, not yet applied by the reader */
    volatile bool settled;   /* no more timezone changes expected */
    int64_t start_us;
} s_ctx;

/* IANA name -> POSIX rule, ignoring case (names are typed on the on-screen keyboard). */
static const char *lookup_iana(const char *name)
{
    for (int i = 0; i < tz_table_count; i++) {
        if (strcasecmp(name, tz_table[i].name) == 0) {
            return tz_table[i].posix;
        }
    }
    return NULL;
}

static void apply_tz(const char *rule)
{
    setenv("TZ", rule, 1);
    tzset();
}

/* Shared setting: IANA name or POSIX rule. Returns true if one was applied. */
static bool tz_from_shared(void)
{
    char v[96];
    if (mosaico_shared_get(SHARED_KEY, v, sizeof(v)) != ESP_OK || !v[0]) {
        return false;
    }
    /* trim spaces */
    char *p = v;
    while (*p == ' ') {
        p++;
    }
    for (char *e = p + strlen(p); e > p && e[-1] == ' '; *--e = '\0') {
    }
    const char *rule = lookup_iana(p);
    /* POSIX rules always contain an offset digit; zone names (nearly) never do. */
    if (!rule && !strpbrk(p, "0123456789")) {
        ESP_LOGW(TAG, "%s \"%s\" is not a known zone name; ignoring it", SHARED_KEY, p);
        return false;
    }
    apply_tz(rule ? rule : p); /* not a zone name: treat as a POSIX rule */
    snprintf(s_ctx.desc, sizeof(s_ctx.desc), "%.30s (shared)", p);
    ESP_LOGI(TAG, "Timezone from %s: %s", SHARED_KEY, rule ? rule : p);
    return true;
}

void clock_time_init(void)
{
    s_ctx.start_us = esp_timer_get_time();
    if (tz_from_shared()) {
        s_ctx.settled = true;
        return;
    }
    apply_tz(CONFIG_LC_TIMEZONE);
    snprintf(s_ctx.desc, sizeof(s_ctx.desc), "default");
#if !CONFIG_LC_TZ_AUTO
    s_ctx.settled = true;
#endif
}

#if CONFIG_LC_TZ_AUTO
static int http_get(const char *url, char *resp, size_t resp_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = USER_AGENT,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return -1;
    }
    esp_http_client_set_header(c, "Accept", "application/json");
    int status = -2;
    resp[0] = '\0';
    if (esp_http_client_open(c, 0) == ESP_OK) {
        (void)esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        int total = 0, n;
        while (total < (int)resp_len - 1 &&
               (n = esp_http_client_read(c, resp + total, resp_len - 1 - total)) > 0) {
            total += n;
        }
        resp[total] = '\0';
        esp_http_client_close(c);
    }
    esp_http_client_cleanup(c);
    return status;
}

static void tz_task(void *arg)
{
    (void)arg;
    char *resp = malloc(RESP_MAX);
    for (int attempt = 0; resp && attempt < 3 && !s_ctx.settled; attempt++) {
        if (!wifi_wait_connected(pdMS_TO_TICKS(30000))) {
            continue;
        }
        int status = http_get(CONFIG_LC_IP_TZ_URL, resp, RESP_MAX);
        cJSON *root = status == 200 ? cJSON_Parse(resp) : NULL;
        const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "timezone");
        const char *rule = cJSON_IsString(tz) ? lookup_iana(tz->valuestring) : NULL;
        if (rule) {
            strlcpy(s_ctx.tz, rule, sizeof(s_ctx.tz));
            snprintf(s_ctx.desc, sizeof(s_ctx.desc), "%.36s (auto)", tz->valuestring);
            ESP_LOGI(TAG, "Timezone from IP: %s -> %s", tz->valuestring, rule);
            __atomic_store_n(&s_ctx.pending, true, __ATOMIC_RELEASE);
        } else {
            ESP_LOGW(TAG, "Timezone lookup failed (HTTP %d)", status);
        }
        cJSON_Delete(root);
        if (rule) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    free(resp);
    s_ctx.settled = true;
    vTaskDelete(NULL);
}
#endif

void clock_time_start_network(void)
{
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
#if CONFIG_LC_TZ_AUTO
    if (!s_ctx.settled) {
        xTaskCreate(tz_task, "tz_lookup", 6144, NULL, 4, NULL);
    }
#endif
}

bool clock_time_local(struct tm *out)
{
    if (__atomic_load_n(&s_ctx.pending, __ATOMIC_ACQUIRE)) {
        apply_tz(s_ctx.tz);
        s_ctx.pending = false;
    }
    time_t now = time(NULL);
    if (now < VALID_EPOCH) {
        return false;
    }
    if (!s_ctx.settled && esp_timer_get_time() - s_ctx.start_us < TZ_SETTLE_TIMEOUT_US) {
        return false;
    }
    localtime_r(&now, out);
    return true;
}

const char *clock_time_zone_desc(void)
{
    return s_ctx.desc;
}
