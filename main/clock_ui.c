/*
 * Literature Clock screen for the 480x480 ESP-Mosaico display (LVGL 9).
 *
 *   +------------------------------------------+
 *   |                  12:13                    |  digital time (only when no exact quote,
 *   |                                           |  by default)
 *   |   "It was a quarter past twelve when      |  quote: Literata, largest size that fits;
 *   |    the bell rang, and ..."                |  the time phrase bold + accent colour
 *   |                                           |
 *   |                 — The Title, Author       |  attribution, right-aligned
 *   |                 . o .                     |  one dot per quote for this minute
 *   +------------------------------------------+
 *
 * Text is rendered at run time from embedded TrueType fonts (LVGL Tiny TTF),
 * so the quote can be sized freely to fill the screen.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "clock_time.h"
#include "clock_ui.h"
#include "quotes.h"

static const char *TAG = "clock_ui";

/* ---------- layout ---------- */
/* The panel has strongly rounded corners: everything stays inside a ~40 px inset. */
#define QUOTE_X 40
#define QUOTE_W 400
#define QUOTE_TOP 50
#define ATTR_RIGHT 416       /* right edge of the attribution */
#define ATTR_W 340
#define ATTR_BOTTOM 432
#define ATTR_GAP 16          /* space between quote and attribution */
#define ATTR_SIZE 20
#define SMALL_SIZE 20
#define TIME_Y 16            /* digital time, top centre */
#define DOTS_Y 448
#define DOT_D 6
#define DOT_GAP 7
#define MAX_DOTS 30
#define TEXT_BUF 1024        /* longest quote is ~720 bytes */
#define TICK_MS 250

/* Quote font sizes tried, largest first (px). */
static const int k_sizes[] = {46, 42, 38, 35, 32, 30, 28, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16};
#define N_SIZES ((int)(sizeof(k_sizes) / sizeof(k_sizes[0])))

typedef struct {
    uint32_t bg, text, accent, meta, dot_on, dot_off;
} theme_t;

static const theme_t k_themes[2] = {
    /* dark: warm grey on black, amber time phrase */
    {0x000000, 0xB9B2A7, 0xFFC86B, 0x7C766D, 0xC9C2B6, 0x3A3732},
    /* paper: ink on cream, oxblood time phrase */
    {0xF3EEE3, 0x3B3731, 0x9A2A1A, 0x6E675D, 0x3B3731, 0xCFC7B8},
};

extern const uint8_t k_ttf_regular_start[] asm("_binary_literata_regular_ttf_start");
extern const uint8_t k_ttf_regular_end[] asm("_binary_literata_regular_ttf_end");
extern const uint8_t k_ttf_bold_start[] asm("_binary_literata_bold_ttf_start");
extern const uint8_t k_ttf_bold_end[] asm("_binary_literata_bold_ttf_end");
extern const uint8_t k_ttf_italic_start[] asm("_binary_literata_italic_ttf_start");
extern const uint8_t k_ttf_italic_end[] asm("_binary_literata_italic_ttf_end");

static struct {
    /* fonts */
    lv_font_t *q_reg, *q_bold;      /* quote, resized per quote */
    lv_font_t *a_it, *a_reg;        /* attribution, digital time, splash status */
    lv_font_t *splash;              /* splash title */
    /* widgets */
    lv_obj_t *quote, *attr, *time_lbl, *splash_title, *splash_status;
    lv_span_t *sp_before, *sp_time, *sp_after;
    lv_span_t *sp_dash, *sp_title, *sp_sep, *sp_author;
    lv_obj_t *dots[MAX_DOTS];
    /* state */
    int theme;
    int shown_minute;               /* minute of day being displayed, -1 = none */
    quote_set_t set;
    int index;                      /* 0..set.count-1 */
    int brightness;                 /* last value sent, -1 = never */
    volatile clock_status_t status;
    bool showing_clock;
    char buf[TEXT_BUF];
} s_ui = {.shown_minute = -1, .brightness = -1};

/* ---------- preferences ---------- */

#define PREF_NS "litclock"
#define PREF_THEME "theme"

static int load_theme(void)
{
#if CONFIG_LC_THEME_PAPER
    int theme = 1;
#else
    int theme = 0;
#endif
    nvs_handle_t h;
    if (nvs_open(PREF_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, PREF_THEME, &v) == ESP_OK && v < 2) {
            theme = v;
        }
        nvs_close(h);
    }
    return theme;
}

static void save_theme(int theme)
{
    nvs_handle_t h;
    if (nvs_open(PREF_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u8(h, PREF_THEME, (uint8_t)theme) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

/* ---------- helpers ---------- */

static lv_font_t *mk_font(const uint8_t *start, const uint8_t *end, int size, size_t cache)
{
    lv_font_t *f = lv_tiny_ttf_create_data_ex(start, (size_t)(end - start), size,
                                              LV_FONT_KERNING_NORMAL, cache);
    if (!f) {
        ESP_LOGE(TAG, "Font creation failed");
    }
    return f;
}

static void span_style(lv_span_t *sp, const lv_font_t *font, uint32_t color)
{
    lv_style_t *st = lv_span_get_style(sp);
    lv_style_set_text_font(st, font);
    lv_style_set_text_color(st, lv_color_hex(color));
}

static lv_obj_t *mk_spangroup(int width, lv_text_align_t align, const lv_font_t *font)
{
    lv_obj_t *sg = lv_spangroup_create(lv_screen_active());
    lv_obj_set_width(sg, width);
    lv_spangroup_set_align(sg, align);
    lv_spangroup_set_overflow(sg, LV_SPAN_OVERFLOW_CLIP);
    /* Fixed width wraps lines; the height is set from the measured text. */
    lv_obj_set_style_text_font(sg, font, 0);
    lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return sg;
}

static lv_obj_t *mk_label(const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Copy bytes [from, from + n) of s into the scratch buffer as a C string. */
static const char *slice(const char *s, size_t from, size_t n)
{
    if (n >= sizeof(s_ui.buf)) {
        n = sizeof(s_ui.buf) - 1;
    }
    memcpy(s_ui.buf, s + from, n);
    s_ui.buf[n] = '\0';
    return s_ui.buf;
}

/* ---------- drawing ---------- */

static void apply_theme_colors(void)
{
    const theme_t *t = &k_themes[s_ui.theme];
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(t->bg), 0);
    span_style(s_ui.sp_before, s_ui.q_reg, t->text);
    span_style(s_ui.sp_time, s_ui.q_bold, t->accent);
    span_style(s_ui.sp_after, s_ui.q_reg, t->text);
    span_style(s_ui.sp_dash, s_ui.a_reg, t->meta);
    span_style(s_ui.sp_title, s_ui.a_it, t->meta);
    span_style(s_ui.sp_sep, s_ui.a_reg, t->meta);
    span_style(s_ui.sp_author, s_ui.a_reg, t->meta);
    lv_obj_set_style_text_color(s_ui.time_lbl, lv_color_hex(t->meta), 0);
    lv_obj_set_style_text_color(s_ui.splash_title, lv_color_hex(t->accent), 0);
    lv_obj_set_style_text_color(s_ui.splash_status, lv_color_hex(t->meta), 0);
}

static void draw_dots(void)
{
    const theme_t *t = &k_themes[s_ui.theme];
    int n = s_ui.set.count > 1 ? s_ui.set.count : 0;
    if (n > MAX_DOTS) {
        n = MAX_DOTS;
    }
    int x0 = 240 - (n * DOT_D + (n - 1) * DOT_GAP) / 2;
    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_t *d = s_ui.dots[i];
        set_hidden(d, i >= n);
        if (i < n) {
            lv_obj_set_pos(d, x0 + i * (DOT_D + DOT_GAP), DOTS_Y);
            lv_obj_set_style_bg_color(d, lv_color_hex(i == s_ui.index ? t->dot_on : t->dot_off), 0);
        }
    }
}

static void draw_quote(void)
{
    if (s_ui.set.count <= 0) {
        return;
    }
    const lc_quote_t *q = &lc_quotes[s_ui.set.first + s_ui.index];
    size_t len = strlen(q->text);
    size_t hs = q->hl_start, hl = q->hl_len;
    if (hs + hl > len) {
        hs = hl = 0;
    }

    /* Quote in three spans: before, time phrase, after (span text is copied). */
    lv_spangroup_set_span_text(s_ui.quote, s_ui.sp_before, slice(q->text, 0, hs));
    lv_spangroup_set_span_text(s_ui.quote, s_ui.sp_time, slice(q->text, hs, hl));
    lv_spangroup_set_span_text(s_ui.quote, s_ui.sp_after, slice(q->text, hs + hl, len - hs - hl));

    /* Attribution, bottom-aligned. */
    lv_spangroup_set_span_text(s_ui.attr, s_ui.sp_dash, "\xE2\x80\x94 ");   /* em dash */
    lv_spangroup_set_span_text(s_ui.attr, s_ui.sp_title, q->title);
    lv_spangroup_set_span_text(s_ui.attr, s_ui.sp_sep, q->author[0] ? ", " : "");
    lv_spangroup_set_span_text(s_ui.attr, s_ui.sp_author, q->author);
    lv_spangroup_refresh(s_ui.attr);
    int32_t ah = lv_spangroup_get_expand_height(s_ui.attr, ATTR_W);
    lv_obj_set_height(s_ui.attr, ah);
    lv_obj_set_pos(s_ui.attr, ATTR_RIGHT - ATTR_W, ATTR_BOTTOM - ah);

    /* Largest quote size that fits above the attribution. */
    int avail = ATTR_BOTTOM - ah - ATTR_GAP - QUOTE_TOP;
    int32_t h = 0;
    int size = k_sizes[N_SIZES - 1];
    for (int i = 0; i < N_SIZES; i++) {
        size = k_sizes[i];
        lv_tiny_ttf_set_size(s_ui.q_reg, size);
        lv_tiny_ttf_set_size(s_ui.q_bold, size);
        lv_obj_set_style_text_line_space(s_ui.quote, size / 5, 0);
        lv_spangroup_refresh(s_ui.quote);
        h = lv_spangroup_get_expand_height(s_ui.quote, QUOTE_W);
        if (h <= avail) {
            break;
        }
    }
    if (h > avail) {
        h = avail; /* smallest size still too tall: clip (no quote in the data needs this) */
    }
    lv_obj_set_height(s_ui.quote, h);
    lv_obj_set_pos(s_ui.quote, QUOTE_X, QUOTE_TOP + (avail - h) / 2);

    draw_dots();
    ESP_LOGI(TAG, "%02d:%02d quote %d/%d (%s) at %d px", s_ui.set.minute / 60, s_ui.set.minute % 60,
             s_ui.index + 1, s_ui.set.count, q->title, size);
}

static void draw_digital(const struct tm *t)
{
#if CONFIG_LC_SHOW_TIME_ALWAYS
    bool show = true;
#elif CONFIG_LC_SHOW_TIME_NEVER
    bool show = false;
#else
    bool show = !s_ui.set.exact;
#endif
    if (show) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%02d:%02d", t->tm_hour, t->tm_min);
        if (strcmp(lv_label_get_text(s_ui.time_lbl), txt) != 0) {
            lv_label_set_text(s_ui.time_lbl, txt);
        }
    }
    set_hidden(s_ui.time_lbl, !show);
}

static void show_clock(bool on)
{
    if (s_ui.showing_clock == on) {
        return;
    }
    s_ui.showing_clock = on;
    set_hidden(s_ui.splash_title, on);
    set_hidden(s_ui.splash_status, on);
    set_hidden(s_ui.quote, !on);
    set_hidden(s_ui.attr, !on);
    if (!on) {
        set_hidden(s_ui.time_lbl, true);
        for (int i = 0; i < MAX_DOTS; i++) {
            set_hidden(s_ui.dots[i], true);
        }
    }
}

/* Night dimming: lower the panel brightness between the configured hours. */
static void update_brightness(const struct tm *t)
{
#if CONFIG_LC_NIGHT_DIM
    int start = CONFIG_LC_NIGHT_START_HOUR, end = CONFIG_LC_NIGHT_END_HOUR, hr = t->tm_hour;
    bool night = start <= end ? (hr >= start && hr < end) : (hr >= start || hr < end);
    int want = night ? CONFIG_LC_NIGHT_BRIGHTNESS : CONFIG_LC_DAY_BRIGHTNESS;
    if (want != s_ui.brightness) {
        if (bsp_display_brightness_set(want) == ESP_OK) {
            s_ui.brightness = want;
        }
    }
#else
    (void)t;
#endif
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    struct tm t;
    if (!clock_time_local(&t)) {
        show_clock(false);
        const char *msg = s_ui.status == CLOCK_STATUS_NO_WIFI
                          ? "No Wi-Fi set up.\nAdd it in Vibe Mode, then restart."
                          : "Setting the clock\xE2\x80\xA6";
        if (strcmp(lv_label_get_text(s_ui.splash_status), msg) != 0) {
            lv_label_set_text(s_ui.splash_status, msg);
        }
        return;
    }
    int minute = t.tm_hour * 60 + t.tm_min;
    if (minute != s_ui.shown_minute || !s_ui.showing_clock) {
        s_ui.shown_minute = minute;
        s_ui.set = quotes_for_minute(minute);
#if CONFIG_LC_RANDOM_QUOTE
        s_ui.index = s_ui.set.count > 1 ? (int)(esp_random() % (uint32_t)s_ui.set.count) : 0;
#else
        s_ui.index = 0;
#endif
        show_clock(true);
        draw_quote();
    }
    draw_digital(&t);
    update_brightness(&t);
}

/* ---------- input ---------- */

static void on_tap(lv_event_t *e)
{
    (void)e;
    if (!s_ui.showing_clock || s_ui.set.count <= 1) {
        return;
    }
    s_ui.index = (s_ui.index + 1) % s_ui.set.count;
    draw_quote();
}

static void on_long_press(lv_event_t *e)
{
    (void)e;
    s_ui.theme ^= 1;
    save_theme(s_ui.theme);
    apply_theme_colors();
    draw_dots();
}

/* ---------- public ---------- */

void clock_ui_set_status(clock_status_t status)
{
    s_ui.status = status;
}

void clock_ui_create(void)
{
    s_ui.theme = load_theme();

    s_ui.q_reg = mk_font(k_ttf_regular_start, k_ttf_regular_end, 30, 256);
    s_ui.q_bold = mk_font(k_ttf_bold_start, k_ttf_bold_end, 30, 64);
    s_ui.a_it = mk_font(k_ttf_italic_start, k_ttf_italic_end, ATTR_SIZE, 128);
    s_ui.a_reg = mk_font(k_ttf_regular_start, k_ttf_regular_end, SMALL_SIZE, 128);
    s_ui.splash = mk_font(k_ttf_italic_start, k_ttf_italic_end, 44, 32);
    if (!s_ui.q_reg || !s_ui.q_bold || !s_ui.a_it || !s_ui.a_reg || !s_ui.splash) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_tap, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, on_long_press, LV_EVENT_LONG_PRESSED, NULL);

    s_ui.quote = mk_spangroup(QUOTE_W, LV_TEXT_ALIGN_LEFT, s_ui.q_reg);
    s_ui.sp_before = lv_spangroup_add_span(s_ui.quote);
    s_ui.sp_time = lv_spangroup_add_span(s_ui.quote);
    s_ui.sp_after = lv_spangroup_add_span(s_ui.quote);

    s_ui.attr = mk_spangroup(ATTR_W, LV_TEXT_ALIGN_RIGHT, s_ui.a_reg);
    lv_obj_set_style_text_line_space(s_ui.attr, 2, 0);
    s_ui.sp_dash = lv_spangroup_add_span(s_ui.attr);
    s_ui.sp_title = lv_spangroup_add_span(s_ui.attr);
    s_ui.sp_sep = lv_spangroup_add_span(s_ui.attr);
    s_ui.sp_author = lv_spangroup_add_span(s_ui.attr);

    s_ui.time_lbl = mk_label(s_ui.a_reg);
    lv_obj_align(s_ui.time_lbl, LV_ALIGN_TOP_MID, 0, TIME_Y);

    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_D, DOT_D);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_ui.dots[i] = d;
    }

    s_ui.splash_title = mk_label(s_ui.splash);
    lv_label_set_text(s_ui.splash_title, "Literature\nClock");
    lv_obj_align(s_ui.splash_title, LV_ALIGN_CENTER, 0, -40);
    s_ui.splash_status = mk_label(s_ui.a_reg);
    lv_obj_set_width(s_ui.splash_status, QUOTE_W);
    lv_obj_align(s_ui.splash_status, LV_ALIGN_CENTER, 0, 70);

    apply_theme_colors();
    s_ui.showing_clock = true; /* so show_clock(false) below takes effect */
    show_clock(false);
    lv_label_set_text(s_ui.splash_status, "Connecting\xE2\x80\xA6");
    lv_timer_create(tick_cb, TICK_MS, NULL);
}
