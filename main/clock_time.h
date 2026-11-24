#pragma once
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Local time for the clock.
 *
 * Timezone, first match wins:
 *   1. "clock_timezone" in Vibe Mode > Settings > Shared information: an IANA
 *      name ("Europe/Paris") or a POSIX rule ("CET-1CEST,M3.5.0,M10.5.0/3").
 *   2. Automatic (CONFIG_LC_TZ_AUTO): the zone of the internet address, from
 *      one ipinfo.io request per start-up.
 *   3. CONFIG_LC_TIMEZONE.
 * Time comes from SNTP (pool.ntp.org) over Wi-Fi.
 */

/** Apply the starting timezone. Call once from app_main before the UI reads the time. */
void clock_time_init(void);

/** Start SNTP and, if needed, the automatic timezone lookup. Call after wifi_start(). */
void clock_time_start_network(void);

/**
 * Fill *out with the local time. Returns false until the time is set and the
 * timezone is settled (lookup finished, or 20 s passed). Call from one task
 * only (the LVGL task): it applies a timezone found by the lookup.
 */
bool clock_time_local(struct tm *out);

/** Short description of where the timezone came from, for logs/status. */
const char *clock_time_zone_desc(void);

#ifdef __cplusplus
}
#endif
