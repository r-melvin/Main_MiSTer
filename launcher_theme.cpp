/*
 * launcher_theme.cpp
 * Theme defaults and cfg file loader.
 */

#include "launcher.h"
#include "launcher_theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ─── globals ────────────────────────────────────────────────────────────── */

LauncherTheme g_theme;

static void theme_set_defaults(LauncherTheme *t)
{
    t->bg            = 0xFF141417u;  /* near-black charcoal */
    t->card          = 0xFF2A2A2Eu;  /* dark grey card */
    t->hi            = 0xFFE8A824u;  /* warm amber highlight */
    t->fav           = 0xFFE89820u;  /* amber favourite */
    t->text          = 0xFFD4D4D8u;  /* light grey text */
    t->dim           = 0xFF6E6E78u;  /* mid-grey secondary */
    t->bar           = 0xFF1E1E22u;  /* dark grey header/footer */
    t->overlay       = 0xD2000000u;
    t->err           = 0xFFDC3C3Cu;
    t->search        = 0xFF262629u;  /* dark grey search bar */
    t->perf_on       = 0xFF4BC07Au;  /* desaturated green */
    t->perf_off      = 0xFFC04B4Bu;  /* desaturated red */
    t->sel_ring      = 0xFF00C8C8u;  /* teal */
    t->sel_ring_inner= 0xFF00A8A8u;
    t->sel_check     = 0xFFFFFFFFu;
    t->font_sizes[0] = 28;
    t->font_sizes[1] = 22;
    t->font_sizes[2] = 17;
    t->bg_image[0]   = '\0';
    strncpy(t->name, "default", sizeof(t->name) - 1);
}

/* Parse a hex color string.
   6 chars (RRGGBB)  → 0xFFRRGGBB (fully opaque)
   8 chars (AARRGGBB)→ 0xAARRGGBB  */
static uint32_t parse_color(const char *s, uint32_t fallback)
{
    size_t len = strlen(s);
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (!end || end == s) return fallback;
    if (len == 6) return 0xFF000000u | (uint32_t)v;
    if (len == 8) return (uint32_t)v;
    return fallback;
}

static int parse_font_size(const char *s, int fallback)
{
    int v = atoi(s);
    return (v >= 8 && v <= 72) ? v : fallback;
}

static void trim(char *s)
{
    /* trim leading */
    char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    /* trim trailing */
    char *e = s + strlen(s) - 1;
    while (e >= s && isspace((unsigned char)*e)) *e-- = '\0';
}

void launcher_theme_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* strip inline comments */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (!key[0]) continue;

        if      (!strcasecmp(key, "name"))       strncpy(g_theme.name,     val, sizeof(g_theme.name)     - 1);
        else if (!strcasecmp(key, "bg_image"))   strncpy(g_theme.bg_image, val, sizeof(g_theme.bg_image) - 1);
        else if (!strcasecmp(key, "bg"))         g_theme.bg      = parse_color(val, g_theme.bg);
        else if (!strcasecmp(key, "card"))       g_theme.card    = parse_color(val, g_theme.card);
        else if (!strcasecmp(key, "hi"))         g_theme.hi      = parse_color(val, g_theme.hi);
        else if (!strcasecmp(key, "fav"))        g_theme.fav     = parse_color(val, g_theme.fav);
        else if (!strcasecmp(key, "text"))       g_theme.text    = parse_color(val, g_theme.text);
        else if (!strcasecmp(key, "dim"))        g_theme.dim     = parse_color(val, g_theme.dim);
        else if (!strcasecmp(key, "bar"))        g_theme.bar     = parse_color(val, g_theme.bar);
        else if (!strcasecmp(key, "overlay"))    g_theme.overlay = parse_color(val, g_theme.overlay);
        else if (!strcasecmp(key, "err"))        g_theme.err     = parse_color(val, g_theme.err);
        else if (!strcasecmp(key, "search"))     g_theme.search  = parse_color(val, g_theme.search);
        else if (!strcasecmp(key, "perf_on"))    g_theme.perf_on = parse_color(val, g_theme.perf_on);
        else if (!strcasecmp(key, "perf_off"))   g_theme.perf_off= parse_color(val, g_theme.perf_off);
        else if (!strcasecmp(key, "sel_ring"))   g_theme.sel_ring       = parse_color(val, g_theme.sel_ring);
        else if (!strcasecmp(key, "sel_ring_inner"))
                                                 g_theme.sel_ring_inner = parse_color(val, g_theme.sel_ring_inner);
        else if (!strcasecmp(key, "sel_check"))  g_theme.sel_check      = parse_color(val, g_theme.sel_check);
        else if (!strcasecmp(key, "font_title")) g_theme.font_sizes[0] = parse_font_size(val, g_theme.font_sizes[0]);
        else if (!strcasecmp(key, "font_big"))   g_theme.font_sizes[1] = parse_font_size(val, g_theme.font_sizes[1]);
        else if (!strcasecmp(key, "font_sm"))    g_theme.font_sizes[2] = parse_font_size(val, g_theme.font_sizes[2]);
    }
    fclose(fp);
    printf("launcher: theme '%s' loaded from %s\n", g_theme.name, path);
}

void launcher_theme_init(const char *launcher_path, const char *theme_name)
{
    theme_set_defaults(&g_theme);
    if (!theme_name || !theme_name[0] || !strcasecmp(theme_name, "default"))
        return;
    char path[600];
    snprintf(path, sizeof(path), "%s/themes/%s/theme.cfg", launcher_path, theme_name);
    launcher_theme_load(path);
}
