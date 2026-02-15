#include "noto.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ── Font handles ─────────────────────────────────────────────────────────── */
static vita2d_font *g_latin = NULL;   /* NotoSans-Regular.ttf          */
static vita2d_font *g_jp    = NULL;   /* NotoSansJP-Regular.otf        */
static vita2d_font *g_kr    = NULL;   /* NotoSansKR-Regular.otf        */
static vita2d_font *g_sc    = NULL;   /* NotoSansSC-Regular.otf        */

/* ── UTF-8 decoder ────────────────────────────────────────────────────────── */
/* Decode one codepoint from *p, advance *p past it.
 * Returns 0xFFFD on invalid sequences. */
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int extra;

    if (*s < 0x80) {
        cp = *s; extra = 0;
    } else if (*s < 0xC0) {
        /* continuation byte at start — skip */
        *p += 1; return 0xFFFD;
    } else if (*s < 0xE0) {
        cp = *s & 0x1F; extra = 1;
    } else if (*s < 0xF0) {
        cp = *s & 0x0F; extra = 2;
    } else {
        cp = *s & 0x07; extra = 3;
    }

    s++;
    for (int i = 0; i < extra; i++) {
        if ((*s & 0xC0) != 0x80) { *p = (const char *)s; return 0xFFFD; }
        cp = (cp << 6) | (*s & 0x3F);
        s++;
    }
    *p = (const char *)s;
    return cp;
}

/* Peek at the codepoint at *p without advancing. */
static uint32_t utf8_peek(const char *p)
{
    return utf8_next(&p);
}

/* ── Script classification ────────────────────────────────────────────────── */
static vita2d_font *font_for_cp(uint32_t cp)
{
    /* Hangul syllables + jamo */
    if ((cp >= 0x1100 && cp <= 0x11FF) ||
        (cp >= 0x302E && cp <= 0x302F) ||
        (cp >= 0x3131 && cp <= 0x318E) ||
        (cp >= 0xA960 && cp <= 0xA97C) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xD7B0 && cp <= 0xD7C6) ||
        (cp >= 0xD7CB && cp <= 0xD7FB))
        return g_kr ? g_kr : g_latin;

    /* CJK unified ideographs + extensions + compatibility */
    if ((cp >= 0x3000 && cp <= 0x303F) ||   /* CJK symbols/punctuation */
        (cp >= 0x3040 && cp <= 0x309F) ||   /* Hiragana                */
        (cp >= 0x30A0 && cp <= 0x30FF) ||   /* Katakana                */
        (cp >= 0x3400 && cp <= 0x4DBF) ||   /* CJK Extension A         */
        (cp >= 0x4E00 && cp <= 0x9FFF) ||   /* CJK Unified Ideographs  */
        (cp >= 0xF900 && cp <= 0xFAFF) ||   /* CJK Compatibility       */
        (cp >= 0xFF00 && cp <= 0xFFEF) ||   /* Fullwidth forms          */
        (cp >= 0x20000 && cp <= 0x2A6DF) || /* CJK Extension B         */
        (cp >= 0x2A700 && cp <= 0x2CEAF))   /* CJK Extensions C–F      */
        return g_jp ? g_jp : (g_sc ? g_sc : g_latin);

    /* Chinese-specific: if we have SC loaded use it for CJK blocks above
     * when JP is unavailable — already handled by fallback chain above.
     * Explicitly route bopomofo / Chinese-only blocks to SC. */
    if ((cp >= 0x02EA && cp <= 0x02EB) ||   /* Bopomofo tone marks     */
        (cp >= 0x3100 && cp <= 0x312F) ||   /* Bopomofo                */
        (cp >= 0x31A0 && cp <= 0x31BF))     /* Bopomofo extended       */
        return g_sc ? g_sc : g_latin;

    return g_latin;
}

/* ── Byte-length of a single UTF-8 codepoint starting at p ───────────────── */
static int utf8_char_len(const char *p)
{
    const char *q = p;
    utf8_next(&q);
    return (int)(q - p);
}

/* ── Init / destroy ───────────────────────────────────────────────────────── */
int noto_init(void)
{
    g_latin = vita2d_load_font_file("app0:/fonts/NotoSans-Regular.ttf");
    g_jp    = vita2d_load_font_file("app0:/fonts/NotoSansJP-Regular.ttf");
    g_kr    = vita2d_load_font_file("app0:/fonts/NotoSansKR-Regular.ttf");
    g_sc    = vita2d_load_font_file("app0:/fonts/NotoSansSC-Regular.ttf");

    return g_latin ? 0 : -1;   /* Latin is mandatory; CJK fonts are optional */
}

void noto_destroy(void)
{
    if (g_latin) { vita2d_free_font(g_latin); g_latin = NULL; }
    if (g_jp)    { vita2d_free_font(g_jp);    g_jp    = NULL; }
    if (g_kr)    { vita2d_free_font(g_kr);    g_kr    = NULL; }
    if (g_sc)    { vita2d_free_font(g_sc);    g_sc    = NULL; }
}

/* ── Core draw: iterate UTF-8 in font runs ────────────────────────────────── */
int noto_draw_text(int x, int y, unsigned int color,
                   unsigned int size, const char *text)
{
    if (!text || !*text || !g_latin) return 0;

    int cx = x;
    const char *p = text;

    while (*p) {
        /* Determine which font this run uses */
        vita2d_font *run_font = font_for_cp(utf8_peek(p));

        /* Collect all consecutive chars that map to the same font */
        const char *run_start = p;
        while (*p) {
            if (font_for_cp(utf8_peek(p)) != run_font)
                break;
            p += utf8_char_len(p);
        }

        /* Build a NUL-terminated copy of the run */
        int run_bytes = (int)(p - run_start);
        char run_buf[1024];
        if (run_bytes >= (int)sizeof(run_buf))
            run_bytes = (int)sizeof(run_buf) - 1;
        memcpy(run_buf, run_start, run_bytes);
        run_buf[run_bytes] = '\0';

        vita2d_font_draw_text(run_font, cx, y, color, size, run_buf);
        cx += vita2d_font_text_width(run_font, size, run_buf);
    }

    return cx - x;
}

int noto_draw_textf(int x, int y, unsigned int color,
                    unsigned int size, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return noto_draw_text(x, y, color, size, buf);
}

int noto_text_width(unsigned int size, const char *text)
{
    if (!text || !*text || !g_latin) return 0;

    int total = 0;
    const char *p = text;

    while (*p) {
        vita2d_font *run_font = font_for_cp(utf8_peek(p));
        const char *run_start = p;
        while (*p) {
            if (font_for_cp(utf8_peek(p)) != run_font) break;
            p += utf8_char_len(p);
        }
        int run_bytes = (int)(p - run_start);
        char run_buf[1024];
        if (run_bytes >= (int)sizeof(run_buf))
            run_bytes = (int)sizeof(run_buf) - 1;
        memcpy(run_buf, run_start, run_bytes);
        run_buf[run_bytes] = '\0';
        total += vita2d_font_text_width(run_font, size, run_buf);
    }

    return total;
}

int noto_text_height(unsigned int size, const char *text)
{
    if (!g_latin) return (int)size;
    /* Height is font/size dependent, not text dependent — use latin font */
    (void)text;
    return vita2d_font_text_height(g_latin, size, "Ag");
}
