#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "theme.h"

UIColorPalette g_palette;

void theme_set_defaults(UIColorPalette *p)
{
    if (!p) return;
    p->bg          = 0xFF1C1C1E;
    p->accent      = 0xFF2C2C2E;
    p->highlight   = 0xFF3A3A3C;
    p->text        = 0xFFFFFFFF;
    p->text_dim    = 0xFF8E8E93;
    p->progress    = 0xFF443CFC; /* red #FC3C44 in ABGR */
    p->vis_bar     = 0xFF443CFC;
    p->album_art_bg = 0xFF2C2C2E;
}

/* convert "RRGGBB" hex to vita2d ABGR */
static unsigned int css_to_abgr(const char *hex)
{
    unsigned long rgb = strtoul(hex, NULL, 16);
    unsigned int r = (rgb >> 16) & 0xff;
    unsigned int g = (rgb >>  8) & 0xff;
    unsigned int b =  rgb        & 0xff;
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

int theme_load_ini(UIColorPalette *p, const char *path)
{
    if (!p || !path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing whitespace */
        char *end = line + strlen(line) - 1;
        while (end > line && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
        if (line[0] == '#' || line[0] == '[' || line[0] == '\0') continue;

        char key[64], val[64];
        if (sscanf(line, " %63[^=] = %63s", key, val) != 2) continue;

        /* trim key */
        char *k = key; while (*k == ' ') k++;
        char *v = val; while (*v == ' ') v++;

        if      (!strcmp(k, "bg"))          p->bg          = css_to_abgr(v);
        else if (!strcmp(k, "accent"))      p->accent      = css_to_abgr(v);
        else if (!strcmp(k, "highlight"))   p->highlight   = css_to_abgr(v);
        else if (!strcmp(k, "text"))        p->text        = css_to_abgr(v);
        else if (!strcmp(k, "text_dim"))    p->text_dim    = css_to_abgr(v);
        else if (!strcmp(k, "progress"))    p->progress    = css_to_abgr(v);
        else if (!strcmp(k, "vis_bar"))     p->vis_bar     = css_to_abgr(v);
        else if (!strcmp(k, "album_art_bg")) p->album_art_bg = css_to_abgr(v);
    }
    fclose(f);
    return 0;
}
