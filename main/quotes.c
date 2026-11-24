#include "quotes.h"

quote_set_t quotes_for_minute(int minute)
{
    quote_set_t s = {.minute = minute, .first = 0, .count = 0, .exact = true};
    minute = ((minute % 1440) + 1440) % 1440;
    for (int back = 0; back < 1440; back++) {
        int m = (minute - back + 1440) % 1440;
        int n = lc_minute_first[m + 1] - lc_minute_first[m];
        if (n > 0) {
            s.minute = m;
            s.first = lc_minute_first[m];
            s.count = n;
            s.exact = back == 0;
            return s;
        }
    }
    return s; /* empty table: count == 0 */
}
