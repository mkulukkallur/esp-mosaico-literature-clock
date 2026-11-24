#pragma once
#include <stdbool.h>
#include "quotes_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The quotes to show for one minute of the day. */
typedef struct {
    int minute;   /**< minute the quotes describe (may be earlier than asked) */
    int first;    /**< index of the first quote in lc_quotes */
    int count;    /**< number of quotes for that minute, >= 1 */
    bool exact;   /**< false: no quote for the asked minute; these are the latest earlier ones */
} quote_set_t;

/**
 * Quotes for minute 0..1439. Minutes without a quote fall back to the most
 * recent earlier minute that has one (wrapping past midnight). Pure function,
 * any task.
 */
quote_set_t quotes_for_minute(int minute);

#ifdef __cplusplus
}
#endif
