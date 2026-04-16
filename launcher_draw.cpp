/*
 * launcher_draw.cpp
 * Imlib2-based rendering primitives for the MiSTer launcher.
 * All functions that take an Imlib_Image operate on that image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include "launcher.h"
#include "lib/imlib2/Imlib2.h"

/* fonts extracted from embedded binary (font_launcher.ttf.o) */
extern uint8_t _binary_font_launcher_ttf_start[];
extern uint8_t _binary_font_launcher_ttf_end[];

static Imlib_Font g_font[3] = {};  /* FONT_TITLE, FONT_BIG, FONT_SM */
static bool g_draw_ready = false;

/* ─── colour helpers ──────────────────────────────────────────────────────── */

static inline void set_col(uint32_t argb)
{
    imlib_context_set_color(
        (argb >> 16) & 0xFF,
        (argb >> 8)  & 0xFF,
         argb        & 0xFF,
        (argb >> 24) & 0xFF);
}

/* alpha-blend a single ARGB8888 pixel over a destination pixel */
static inline uint32_t blend_pixel(uint32_t dst, uint32_t src)
{
    int sa = (src >> 24) & 0xFF;
    if (sa == 255) return src;
    if (sa == 0)   return dst;
    int dr = (dst >> 16) & 0xFF;
    int dg = (dst >>  8) & 0xFF;
    int db =  dst        & 0xFF;
    int sr = (src >> 16) & 0xFF;
    int sg = (src >>  8) & 0xFF;
    int sb =  src        & 0xFF;
    int r  = sr + ((dr * (255 - sa)) >> 8);
    int g  = sg + ((dg * (255 - sa)) >> 8);
    int b  = sb + ((db * (255 - sa)) >> 8);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ─── init / shutdown ────────────────────────────────────────────────────── */

void launcher_draw_init(void)
{
    if (g_draw_ready) return;

    /* extract embedded TTF to /tmp */
    const char *font_dir  = "/tmp/launcher_fonts";
    const char *font_path = "/tmp/launcher_fonts/font_launcher.ttf";
    mkdir(font_dir, 0755);

    size_t font_sz = (size_t)(_binary_font_launcher_ttf_end - _binary_font_launcher_ttf_start);
    FILE *fp = fopen(font_path, "wb");
    if (fp) {
        fwrite(_binary_font_launcher_ttf_start, 1, font_sz, fp);
        fclose(fp);
    } else {
        printf("launcher_draw: failed to extract font\n");
    }

    imlib_add_path_to_font_path(font_dir);

    for (int i = 0; i < 3; i++) {
        char spec[64];
        snprintf(spec, sizeof(spec), "font_launcher/%d", g_theme.font_sizes[i]);
        g_font[i] = imlib_load_font(spec);
        if (!g_font[i])
            printf("launcher_draw: failed to load font size %d\n", g_theme.font_sizes[i]);
    }

    g_draw_ready = true;
}

void launcher_draw_shutdown(void)
{
    for (int i = 0; i < 3; i++) {
        if (g_font[i]) {
            imlib_context_set_font(g_font[i]);
            imlib_free_font();
            g_font[i] = nullptr;
        }
    }
    g_draw_ready = false;
}

/* ─── clear ──────────────────────────────────────────────────────────────── */

void launcher_clear(Imlib_Image img, uint32_t argb)
{
    imlib_context_set_image(img);
    set_col(argb);
    int w = imlib_image_get_width();
    int h = imlib_image_get_height();
    imlib_image_fill_rectangle(0, 0, w, h);
}

/* ─── solid rect ─────────────────────────────────────────────────────────── */

void launcher_fill_rect(Imlib_Image img, int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0) return;
    imlib_context_set_image(img);
    set_col(argb);
    imlib_image_fill_rectangle(x, y, w, h);
}

/* ─── rounded rect using filled ellipses at corners ─────────────────────── */

void launcher_fill_rect_rounded(Imlib_Image img, int x, int y, int w, int h,
                                 int r, uint32_t argb)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) { launcher_fill_rect(img, x, y, w, h, argb); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    imlib_context_set_image(img);
    set_col(argb);

    /* centre strips */
    imlib_image_fill_rectangle(x + r, y,     w - 2*r, h);
    imlib_image_fill_rectangle(x,     y + r, r,       h - 2*r);
    imlib_image_fill_rectangle(x + w - r, y + r, r,   h - 2*r);

    /* four corner ellipses */
    imlib_image_fill_ellipse(x + r,         y + r,         r-1, r-1);
    imlib_image_fill_ellipse(x + w - r - 1, y + r,         r-1, r-1);
    imlib_image_fill_ellipse(x + r,         y + h - r - 1, r-1, r-1);
    imlib_image_fill_ellipse(x + w - r - 1, y + h - r - 1, r-1, r-1);
}

/* ─── text ───────────────────────────────────────────────────────────────── */

void launcher_draw_text(Imlib_Image img, int x, int y,
                         const char *text, int font_id, uint32_t argb)
{
    if (!text || !text[0]) return;
    if (font_id < 0 || font_id > 2 || !g_font[font_id]) return;
    imlib_context_set_image(img);
    imlib_context_set_font(g_font[font_id]);
    set_col(argb);
    imlib_text_draw(x, y, text);
}

/* draw centred text, returns drawn width */
int launcher_draw_text_centred(Imlib_Image img, int cx, int y,
                                const char *text, int font_id, uint32_t argb)
{
    if (!text || !text[0] || font_id < 0 || font_id > 2 || !g_font[font_id])
        return 0;
    imlib_context_set_font(g_font[font_id]);
    int tw = 0, th = 0;
    imlib_get_text_size(text, &tw, &th);
    launcher_draw_text(img, cx - tw / 2, y, text, font_id, argb);
    return tw;
}

/* measure text width */
int launcher_text_width(const char *text, int font_id)
{
    if (!text || !text[0] || font_id < 0 || font_id > 2 || !g_font[font_id])
        return 0;
    imlib_context_set_font(g_font[font_id]);
    int tw = 0, th = 0;
    imlib_get_text_size(text, &tw, &th);
    return tw;
}

int launcher_text_height(int font_id)
{
    if (font_id < 0 || font_id > 2 || !g_font[font_id]) return 16;
    imlib_context_set_font(g_font[font_id]);
    return imlib_get_font_ascent() + imlib_get_font_descent();
}

/* draw text clipped with "…" if wider than max_w */
void launcher_draw_text_clipped(Imlib_Image img, int x, int y,
                                  const char *text, int font_id,
                                  uint32_t argb, int max_w)
{
    if (!text || !text[0] || max_w <= 0) return;
    int tw = launcher_text_width(text, font_id);
    if (tw <= max_w) {
        launcher_draw_text(img, x, y, text, font_id, argb);
        return;
    }
    /* truncate and append ellipsis */
    char buf[256];
    strncpy(buf, text, sizeof(buf) - 4);
    buf[sizeof(buf) - 4] = '\0';
    int len = (int)strlen(buf);
    while (len > 0) {
        buf[len - 1] = '\0';
        buf[len - 0] = '.'; buf[len + 1] = '.'; buf[len + 2] = '.';
        buf[len + 3] = '\0';
        if (launcher_text_width(buf, font_id) <= max_w) break;
        buf[len] = '\0';   /* hide ellipsis and shorten further */
        len--;
    }
    launcher_draw_text(img, x, y, buf, font_id, argb);
}

/* ─── blit / scale image ──────────────────────────────────────────────────── */

void launcher_blit_image(Imlib_Image dst, Imlib_Image src,
                          int dx, int dy, int dw, int dh)
{
    if (!src || !dst || dw <= 0 || dh <= 0) return;
    imlib_context_set_image(src);
    int sw = imlib_image_get_width();
    int sh = imlib_image_get_height();
    imlib_context_set_image(dst);
    imlib_context_set_blend(0);
    imlib_blend_image_onto_image(src, 0,
        0, 0, sw, sh,
        dx, dy, dw, dh);
}

/* ─── selection glow ─────────────────────────────────────────────────────── */

void launcher_draw_glow(Imlib_Image img, int x, int y, int w, int h,
                         uint32_t argb, int rings)
{
    imlib_context_set_image(img);
    int r = (argb >> 16) & 0xFF;
    int g = (argb >>  8) & 0xFF;
    int b =  argb        & 0xFF;

    for (int i = rings; i >= 1; i--) {
        int alpha = (int)(200 * i / rings);
        imlib_context_set_color(r, g, b, alpha);
        imlib_image_draw_rectangle(x - i*2,     y - i*2,
                                   w + i*4,     h + i*4);
    }
}

/* ─── full-screen fade overlay ───────────────────────────────────────────── */

/* blend a black (or colour) fade layer over the entire image */
void launcher_draw_fade(Imlib_Image img, int alpha)
{
    if (alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    imlib_context_set_image(img);
    imlib_context_set_color(0, 0, 0, alpha);
    int w = imlib_image_get_width();
    int h = imlib_image_get_height();
    imlib_image_fill_rectangle(0, 0, w, h);
}

/* ─── particles ──────────────────────────────────────────────────────────── */

#define PARTICLE_COUNT 30

struct Particle {
    float x, y;
    float speed;
    int   size;
    int   bright;
};

static Particle g_particles[PARTICLE_COUNT];
static bool     g_particles_init = false;

static uint32_t g_prng_state = 99;
static inline uint32_t prng(void)
{
    g_prng_state ^= g_prng_state << 13;
    g_prng_state ^= g_prng_state >> 17;
    g_prng_state ^= g_prng_state << 5;
    return g_prng_state;
}

void launcher_particles_init(int sw, int sh)
{
    g_prng_state = 99;
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        g_particles[i].x      = (float)(prng() % sw);
        g_particles[i].y      = (float)(prng() % sh);
        g_particles[i].speed  = 0.15f + (float)(prng() % 56) / 100.0f;
        g_particles[i].size   = ((prng() % 2) == 0) ? 1 : 2;
        g_particles[i].bright = 40 + (int)(prng() % 56);
    }
    g_particles_init = true;
}

void launcher_particles_update(int sh)
{
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        g_particles[i].y -= g_particles[i].speed;
        if (g_particles[i].y < -2.0f)
            g_particles[i].y += (float)(sh + 4);
    }
}

void launcher_particles_draw(Imlib_Image img)
{
    if (!g_particles_init) return;
    imlib_context_set_image(img);
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        int b  = g_particles[i].bright;
        int sz = g_particles[i].size;
        int px = (int)g_particles[i].x;
        int py = (int)g_particles[i].y;
        imlib_context_set_color(b, b, b + 18 < 255 ? b + 18 : 255, 255);
        if (sz == 1)
            imlib_image_fill_rectangle(px, py, 1, 1);
        else
            imlib_image_fill_ellipse(px, py, 1, 1);
    }
}

/* ─── loading spinner ────────────────────────────────────────────────────── */

void launcher_draw_spinner(Imlib_Image img, int cx, int cy,
                            int radius, float angle, uint32_t argb)
{
    /* draw a 72% arc as polyline with 32 segments */
    static const float ARC = 0.72f * 2.0f * 3.14159265f;
    static const int   SEG = 32;

    imlib_context_set_image(img);
    set_col(argb);

    int px0 = cx + (int)(radius * cosf(angle));
    int py0 = cy + (int)(radius * sinf(angle));

    for (int i = 1; i <= SEG; i++) {
        float a  = angle + ARC * i / SEG;
        int   px = cx + (int)(radius * cosf(a));
        int   py = cy + (int)(radius * sinf(a));
        imlib_image_draw_line(px0, py0, px, py, 0);
        px0 = px; py0 = py;
    }
}

/* ─── favourite badge ────────────────────────────────────────────────────── */

void launcher_draw_fav_badge(Imlib_Image img, int x, int y)
{
    /* outer ring in LC_FAV, inner dot in LC_BG */
    imlib_context_set_image(img);
    set_col(LC_FAV);
    imlib_image_fill_ellipse(x, y, 7, 7);
    set_col(LC_BG);
    imlib_image_fill_ellipse(x, y, 4, 4);
}

/* ─── placeholder cover ──────────────────────────────────────────────────── */

/* Create a solid placeholder image (caller must free with imlib_free_image) */
Imlib_Image launcher_make_placeholder(float pulse)
{
    /* pulse is time.fmod(1.0) 0..1 triangle wave brightness 50..70 */
    float t = pulse < 0.5f ? pulse * 2.0f : (1.0f - pulse) * 2.0f;
    int bri = 50 + (int)(t * 20.0f);
    Imlib_Image img = imlib_create_image(LAUNCHER_COVER_W, LAUNCHER_COVER_H);
    if (!img) return NULL;
    imlib_context_set_image(img);
    imlib_image_set_has_alpha(0);
    imlib_context_set_color(bri, bri, bri + 15, 255);
    imlib_image_fill_rectangle(0, 0, LAUNCHER_COVER_W, LAUNCHER_COVER_H);
    return img;
}

/* ─── performance overlay ────────────────────────────────────────────────── */

void launcher_draw_performance(Imlib_Image img, const uint32_t *frame_times, int frame_count, int screen_w, int screen_h)
{
    (void)screen_h;
    /* Calculate average frame time and FPS from last 60 frames */
    uint32_t total_ms = 0;
    for (int i = 0; i < frame_count && i < 60; i++)
        total_ms += frame_times[i];
    uint32_t avg_ms = (frame_count > 0) ? (total_ms / frame_count) : 16;
    int fps = (avg_ms > 0) ? (1000 / avg_ms) : 60;

    imlib_context_set_image(img);

    /* Draw semi-transparent background bar */
    imlib_context_set_color(0, 0, 0, 192);
    imlib_image_fill_rectangle(screen_w - 150, 10, 140, 40);

    /* Draw FPS text in top-right */
    set_col(LC_HI);
    char fps_str[64];
    snprintf(fps_str, sizeof(fps_str), "%d fps\n%u ms", fps, avg_ms);
    launcher_draw_text(img, screen_w - 140, 14, fps_str, FONT_SM, LC_HI);
}

/* ─── help screen ────────────────────────────────────────────────────────── */

void launcher_draw_help(Imlib_Image img, int screen_w, int screen_h)
{
    imlib_context_set_image(img);
    set_col(LC_BG);
    imlib_image_fill_rectangle(0, 0, screen_w, screen_h);

    int x = 40, y = 40;
    const char *title = "KEYBOARD SHORTCUTS";
    launcher_draw_text(img, x, y, title, FONT_TITLE, LC_HI);
    y += 50;

    /* Organized help text by feature */
    const char *col1[] = {
        "NAVIGATION:",
        "LEFT/RIGHT:  Switch systems",
        "UP/DOWN:     Move in grid/menu",
        "A/ENTER:     Launch game / Select",
        "B/ESC:       Back / Exit",
        "",
        "GAME BROWSER:",
        "L1:          Search by name",
        "X/Y:         Toggle favourite",
        "Insert:      Bulk select mode",
        "R:           Rate game (1–5 stars)",
        NULL
    };

    const char *col2[] = {
        "DISPLAY:",
        "~:           Toggle FPS counter",
        "?:           Show this help",
        "",
        "SETTINGS:",
        "Tab:         Open settings menu",
        "L1/R1:       Change sort order",
        "",
        "IN-GAME MENU:",
        "SELECT+START: Open overlay",
        "A:           Select item",
        "B:           Back to game",
        NULL
    };

    int col_x = x, col_y = y;
    for (int i = 0; col1[i]; i++) {
        /* section headers in accent color */
        uint32_t col = (col1[i][0] == '\0') ? LC_BG :
                       (col1[i][strlen(col1[i])-1] == ':') ? LC_HI : LC_TEXT;
        launcher_draw_text(img, col_x, col_y, col1[i], FONT_SM, col);
        col_y += 22;
    }

    col_x = screen_w / 2;
    col_y = y;
    for (int i = 0; col2[i]; i++) {
        /* section headers in accent color */
        uint32_t col = (col2[i][0] == '\0') ? LC_BG :
                       (col2[i][strlen(col2[i])-1] == ':') ? LC_HI : LC_TEXT;
        launcher_draw_text(img, col_x, col_y, col2[i], FONT_SM, col);
        col_y += 22;
    }

    launcher_draw_text_centred(img, screen_w / 2, screen_h - 40,
        "Press any button to close", FONT_SM, LC_DIM);
}

/* ─── game metadata display ──────────────────────────────────────────────── */

static void build_metadata_string(const LauncherGame *game, char *metadata, size_t cap)
{
    int pos = 0;
    metadata[0] = '\0';

    /* play count */
    if (game->play_count > 0) {
        pos += snprintf(metadata + pos, cap - pos,
                       "Played: %d time%s", game->play_count,
                       game->play_count == 1 ? "" : "s");
    } else {
        pos += snprintf(metadata + pos, cap - pos, "Never played");
    }

    /* last played */
    if (game->last_played > 0) {
        uint32_t now = (uint32_t)time(NULL);
        uint32_t secs_ago = (now > game->last_played) ? (now - game->last_played) : 0;
        uint32_t days_ago = secs_ago / 86400;

        if (pos < (int)cap - 1) {
            pos += snprintf(metadata + pos, cap - pos, "  |  ");
            if (days_ago == 0) {
                pos += snprintf(metadata + pos, cap - pos, "Today");
            } else if (days_ago == 1) {
                pos += snprintf(metadata + pos, cap - pos, "Yesterday");
            } else if (days_ago < 30) {
                pos += snprintf(metadata + pos, cap - pos, "%u days ago", days_ago);
            } else {
                uint32_t weeks_ago = days_ago / 7;
                pos += snprintf(metadata + pos, cap - pos, "%u week%s ago",
                               weeks_ago, weeks_ago == 1 ? "" : "s");
            }
        }
    }

    /* play time */
    if (game->play_time > 0) {
        uint32_t hours = game->play_time / 3600;
        uint32_t minutes = (game->play_time % 3600) / 60;
        if (pos < (int)cap - 1) {
            pos += snprintf(metadata + pos, cap - pos, "  |  ");
            if (hours > 0) {
                pos += snprintf(metadata + pos, cap - pos, "%uh %um", hours, minutes);
            } else {
                pos += snprintf(metadata + pos, cap - pos, "%um", minutes);
            }
        }
    }

    /* user rating */
    if (game->user_rating > 0) {
        static const char *star_strs[] = { "", "*", "**", "***", "****", "*****" };
        pos += snprintf(metadata + pos, cap - pos,
                       "  |  %s", star_strs[game->user_rating]);
    }

    /* file size */
    if (game->file_size > 0) {
        if (pos < (int)cap - 1) {
            double size_mb = game->file_size / (1024.0 * 1024.0);
            pos += snprintf(metadata + pos, cap - pos,
                           "  |  %.1f MB", size_mb);
        }
    }
}

void launcher_draw_game_metadata(Imlib_Image img, const LauncherGame *game, int x, int y)
{
    if (!game) return;
    char metadata[512];
    build_metadata_string(game, metadata, sizeof(metadata));
    launcher_draw_text(img, x, y, metadata, FONT_SM, LC_DIM);
}

int launcher_game_metadata_width(const LauncherGame *game)
{
    if (!game) return 0;
    char metadata[512];
    build_metadata_string(game, metadata, sizeof(metadata));
    return launcher_text_width(metadata, FONT_SM);
}

/* ─── settings menu ──────────────────────────────────────────────────────── */

void launcher_draw_settings(Imlib_Image img, int screen_w, int screen_h, int selected,
                            uint8_t particles, const char *theme, uint8_t show_perf)
{
    imlib_context_set_image(img);
    set_col(LC_BG);
    imlib_image_fill_rectangle(0, 0, screen_w, screen_h);

    int cx = screen_w / 2;
    int y = 40;

    /* title */
    launcher_draw_text_centred(img, cx, y, "LAUNCHER SETTINGS", FONT_TITLE, LC_HI);
    y += 45;

    /* menu items */
    const char *items[] = {
        "Toggle Particles",
        "Select Theme",
        "Performance Display",
        "Back to Launcher"
    };
    const char *descs[] = {
        "Animated background effects",
        "Color scheme and appearance",
        "Show FPS counter and frame times",
        "Return to main launcher"
    };
    const int item_count = 4;
    const int item_h = 65;

    for (int i = 0; i < item_count; i++) {
        uint32_t fg = (i == selected) ? LC_HI : LC_TEXT;
        uint32_t desc_col = (i == selected) ? LC_TEXT : LC_DIM;
        uint32_t bg = (i == selected) ? LC_CARD : 0xFF000000;

        if (bg) {
            set_col(bg);
            imlib_image_fill_rectangle(50, y - 12, screen_w - 100, item_h);
        }

        launcher_draw_text(img, 80, y, items[i], FONT_BIG, fg);
        launcher_draw_text(img, 80, y + 24, descs[i], FONT_SM, desc_col);

        /* value display with visual indicator */
        int val_x = screen_w - 140;
        if (i == 0) {
            const char *status = particles ? "✓ ON" : "✗ OFF";
            uint32_t status_col = particles ? LC_PERF_ON : LC_PERF_OFF;
            launcher_draw_text(img, val_x, y + 6, status, FONT_BIG, status_col);
        } else if (i == 1) {
            launcher_draw_text(img, val_x, y + 6, theme ? theme : "default", FONT_BIG, LC_TEXT);
        } else if (i == 2) {
            const char *status = show_perf ? "✓ ON" : "✗ OFF";
            uint32_t status_col = show_perf ? LC_PERF_ON : LC_PERF_OFF;
            launcher_draw_text(img, val_x, y + 6, status, FONT_BIG, status_col);
        }

        y += item_h;
    }

    launcher_draw_text_centred(img, cx, screen_h - 40,
        "Up/Down: Navigate   A: Toggle/Select   B: Back", FONT_SM, LC_DIM);
}

/* ─── bulk select badge ──────────────────────────────────────────────────── */

void launcher_draw_sel_badge(Imlib_Image img, int x, int y)
{
    imlib_context_set_image(img);

    int radius = 12;
    const uint32_t ring_col  = LC_SEL_RING;
    const uint32_t inner_col = LC_SEL_RING_IN;
    const uint32_t check_col = LC_SEL_CHECK;

    /* outer ring */
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 <= radius*radius && d2 > (radius-2)*(radius-2)) {
                imlib_context_set_color((ring_col >> 16) & 0xFF, (ring_col >> 8) & 0xFF,
                                       ring_col & 0xFF, (ring_col >> 24) & 0xFF);
                imlib_image_fill_rectangle(x + dx, y + dy, 1, 1);
            }
        }
    }

    /* inner filled circle */
    for (int dy = -(radius-2); dy <= (radius-2); dy++) {
        for (int dx = -(radius-2); dx <= (radius-2); dx++) {
            if (dx*dx + dy*dy <= (radius-2)*(radius-2)) {
                imlib_context_set_color((inner_col >> 16) & 0xFF, (inner_col >> 8) & 0xFF,
                                       inner_col & 0xFF, (inner_col >> 24) & 0xFF);
                imlib_image_fill_rectangle(x + dx, y + dy, 1, 1);
            }
        }
    }

    /* checkmark */
    uint8_t cr = (check_col >> 16) & 0xFF;
    uint8_t cg = (check_col >> 8) & 0xFF;
    uint8_t cb = check_col & 0xFF;
    uint8_t ca = (check_col >> 24) & 0xFF;
    for (int i = -3; i <= 2; i++) {
        imlib_context_set_color(cr, cg, cb, ca);
        imlib_image_fill_rectangle(x + i - 2, y + i, 1, 1);
    }
    for (int i = -2; i <= 3; i++) {
        imlib_context_set_color(cr, cg, cb, ca);
        imlib_image_fill_rectangle(x + i + 2, y - i + 2, 1, 1);
    }
}

/* ─── variant badge ──────────────────────────────────────────────────────── */

void launcher_draw_variant_badge(Imlib_Image img, int x, int y, const char *label)
{
    imlib_context_set_image(img);

    /* badge background: small rounded rect, gold color */
    int bw = 32, bh = 18, r = 4;
    launcher_fill_rect_rounded(img, x, y, bw, bh, r, LC_HI);

    /* text: label (e.g. "v3") in black */
    int tw = launcher_text_width(label, FONT_SM);
    int tx = x + (bw - tw) / 2;
    int ty = y + (bh / 2) - 4;  /* vertically centered */
    launcher_draw_text(img, tx, ty, label, FONT_SM, LC_BLACK);
}

/* ─── version/region selector modal ──────────────────────────────────────── */

void launcher_draw_version_select(Imlib_Image img, int sw, int sh,
                                   const LauncherGame *games,
                                   const int *indices, int count, int sel)
{
    imlib_context_set_image(img);

    /* overlay */
    set_col(LC_OVERLAY);
    imlib_image_fill_rectangle(0, 0, sw, sh);

    int cx = sw / 2;
    int cy = sh / 2;

    /* title */
    launcher_draw_text_centred(img, cx, cy - 100, "SELECT VERSION", FONT_TITLE, LC_HI);

    /* item list (max 8 rows visible) */
    int max_visible = 8;
    int item_h = 52;
    int panel_w = 600;
    int panel_x = cx - panel_w / 2;
    int panel_y = cy - 80;

    for (int i = 0; i < count && i < max_visible; i++) {
        int idx = indices[i];
        int y = panel_y + i * item_h;

        bool is_sel = (i == sel);
        uint32_t bg = is_sel ? LC_HI : LC_CARD;
        uint32_t fg = is_sel ? LC_BLACK : LC_TEXT;
        uint32_t fg2 = is_sel ? LC_BLACK : LC_DIM;

        if (is_sel) launcher_draw_glow(img, panel_x, y, panel_w, item_h - 4, LC_HI, 3);
        launcher_fill_rect_rounded(img, panel_x, y, panel_w, item_h - 4, 6, bg);

        /* game name (full, with region tag) */
        launcher_draw_text(img, panel_x + 18, y + (item_h - 4) / 2 - 10,
                           games[idx].name, FONT_BIG, fg);

        /* file size on the right */
        if (games[idx].file_size > 0) {
            char size_str[32];
            if (games[idx].file_size >= 1024 * 1024)
                snprintf(size_str, sizeof(size_str), "%.1f MB", games[idx].file_size / (1024.0 * 1024.0));
            else if (games[idx].file_size >= 1024)
                snprintf(size_str, sizeof(size_str), "%.0f KB", games[idx].file_size / 1024.0);
            else
                snprintf(size_str, sizeof(size_str), "%u B", games[idx].file_size);
            int tw = launcher_text_width(size_str, FONT_SM);
            launcher_draw_text(img, panel_x + panel_w - tw - 18,
                               y + (item_h - 4) / 2 - 8, size_str, FONT_SM, fg2);
        }
    }

    /* scroll indicators (▲/▼) for variants */
    if (sel > 0) {
        launcher_draw_text_centred(img, cx, panel_y - 20, "▲", FONT_SM, LC_DIM);
    }
    if (sel + max_visible < count) {
        launcher_draw_text_centred(img, cx, panel_y + (max_visible) * item_h + 10, "▼", FONT_SM, LC_DIM);
    }

    /* footer hint */
    launcher_draw_text_centred(img, cx, sh - 40,
        "Up/Down: Select   A/Enter: Launch   B/Esc: Back", FONT_SM, LC_DIM);
}

/* ─── rating modal ──────────────────────────────────────────────────────── */

void launcher_draw_rating_modal(Imlib_Image img, int sw, int sh,
                                 const char *game_name, int sel, int current)
{
    imlib_context_set_image(img);
    set_col(LC_OVERLAY);
    imlib_image_fill_rectangle(0, 0, sw, sh);

    int cx = sw / 2;
    int cy = sh / 2;

    launcher_draw_text_centred(img, cx, cy - 170, "RATE THIS GAME", FONT_TITLE, LC_HI);
    launcher_draw_text_centred(img, cx, cy - 125, game_name, FONT_SM, LC_DIM);

    /* 5 rows, one per star rating */
    static const char *labels[] = { "1  Poor",
                                     "2  Fair",
                                     "3  Good",
                                     "4  Great",
                                     "5  Excellent" };
    /* star strings — ASCII fallback using repeated asterisks if ★ missing */
    static const char *stars[]  = { "*", "**", "***", "****", "*****" };

    int item_h = 52, panel_w = 480;
    int panel_x = cx - panel_w / 2;
    int panel_y = cy - 100;

    for (int i = 0; i < 5; i++) {
        int y        = panel_y + i * item_h;
        bool is_sel  = ((i + 1) == sel);
        bool is_cur  = ((i + 1) == current);

        uint32_t bg  = is_sel  ? LC_HI   : LC_CARD;
        uint32_t fg  = is_sel  ? LC_BLACK : LC_TEXT;
        uint32_t sfg = is_sel  ? LC_BLACK : LC_FAV;   /* star colour */

        if (is_sel) launcher_draw_glow(img, panel_x, y, panel_w, item_h - 4, LC_HI, 3);
        launcher_fill_rect_rounded(img, panel_x, y, panel_w, item_h - 4, 6, bg);

        /* "current rating" indicator dot on left */
        if (is_cur) {
            set_col(LC_FAV);
            imlib_image_fill_ellipse(panel_x + 14, y + (item_h - 4) / 2, 5, 5);
        }

        launcher_draw_text(img, panel_x + 30, y + (item_h - 4) / 2 - 10,
                           labels[i], FONT_BIG, fg);

        /* star string right-aligned */
        int tw = launcher_text_width(stars[i], FONT_SM);
        launcher_draw_text(img, panel_x + panel_w - tw - 20,
                           y + (item_h - 4) / 2 - 8, stars[i], FONT_SM, sfg);
    }

    const char *hint = (current > 0)
        ? "A: Set Rating   X: Clear   B: Cancel"
        : "A: Set Rating   B: Cancel";
    launcher_draw_text_centred(img, cx, sh - 40, hint, FONT_SM, LC_DIM);
}
