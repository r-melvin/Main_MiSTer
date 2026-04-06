/*
 * launcher.cpp
 * Main state machine for the MiSTer launcher.
 * Handles: splash, system carousel, game grid, launch overlay, error, in-game menu.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <cctype>
#include <linux/input.h>  /* KEY_* codes */

#include "launcher.h"
#include "launcher_draw.h"
#include "launcher_io.h"
#include "lib/imlib2/Imlib2.h"
#include "cfg.h"
#include "video.h"
#include "osd.h"
#include "input.h"
#include "frame_timer.h"
#include <sys/stat.h>
#include "cheats.h"
#include "menu.h"
#include "user_io.h"
#include "file_io.h"

/* from menu.cpp — made non-static */
extern uint32_t menu_key_get(void);
extern uint8_t  menu_ascii_key(uint32_t keycode);

/* defined in input.cpp, declared extern in launcher.h */

/* ─── globals ────────────────────────────────────────────────────────────── */

static LauncherMode   g_mode = LMODE_SPLASH;
static LauncherSystem *g_real_sys  = NULL;
static int             g_real_cnt  = 0;
static LauncherSystem *g_all_sys   = NULL;
static int             g_all_cnt   = 0;
static LauncherState   g_state = {};
static char            g_base_dir[512];
static char            g_state_path[600];

/* framebuffer */
static int           g_fb_w = 0, g_fb_h = 0;
static int           g_back_page = 1;  /* 1 or 2 */
static Imlib_Image   g_fb_img[3] = {}; /* wraps fb_base pages 0..2 */

/* background image (loaded from theme; NULL = solid colour fill) */
static Imlib_Image   g_bg_img = NULL;

/* performance tracking */
static uint32_t      g_frame_times[60] = {}; /* ring buffer of last 60 frame times in ms */
static int           g_frame_time_idx = 0;
static uint32_t      g_last_frame_ms = 0;

/* fade */
static FadeDir   g_fade_dir   = FADE_IDLE;
static int       g_fade_alpha = 255;      /* start fully black */
static void     (*g_post_fade)(void) = NULL;

/* system carousel */
static int    g_sys_sel    = 0;
static float  g_sys_scroll = 0.0f;
static float  g_sys_target = 0.0f;
static float  g_sys_scale  = 1.0f;
static float  g_sys_float_y= 0.0f;

/* game grid */
static int    g_game_sel    = 0;
static int    g_game_scroll = 0;
static int    g_game_target = 0;
static char   g_search[128] = {};
static bool   g_search_mode = false;
static LauncherGame *g_filtered = NULL;
static int    g_filtered_cnt = 0;
static bool   g_filtered_owned = false;
static bool   g_fav_filter = false;

/* spinners / timers */
static float  g_spinner_angle = 0.0f;
static float  g_splash_dots   = 0.0f;
static float  g_splash_bar    = 0.0f;
static uint32_t g_splash_timer = 0;
static bool   g_splash_min_done = false;

/* error message */
static char g_error_msg[256] = {};

/* library stats cache */
static int g_stats_total_systems = 0;
static int g_stats_total_games = 0;
static uint64_t g_stats_total_bytes = 0;
static int g_stats_fav_count = 0;
static int g_stats_total_plays = 0;
static uint32_t g_stats_total_play_time = 0;
static char g_stats_most_played[128] = {};
static int g_stats_most_played_cnt = 0;

/* settings menu */
static int g_settings_sel = 0;
static LauncherMode g_settings_prev_mode = LMODE_SYSTEMS;  /* remember which mode we came from */
static const char *g_theme_names[] = { "dark", "light", "retro", "purple" };
static const int g_theme_count = 4;

/* cover batch download modal */
static int g_cdl_total = 0;        /* games queued for this batch */
static int g_cdl_done = 0;         /* covers confirmed on disk so far */
static int g_cdl_check_idx = 0;    /* sliding-window cursor for stat() scan */
static LauncherSystem *g_cdl_sys = NULL;  /* pointer to system being scanned */

/* bulk select */
static bool g_multi_mode = false;
static bool g_sel_bits[LAUNCHER_MAX_GAMES] = {};

/* dirty flag — set true whenever a redraw is needed */
static bool g_dirty = true;
static int  g_sel_count = 0;

/* version selector */
static int g_variant_count[LAUNCHER_MAX_GAMES] = {};
static int g_ver_indices[LAUNCHER_MAX_GAMES] = {};
static int g_ver_count = 0;
static int g_ver_sel = 0;

/* description overlay */
static char g_desc_game_name[128] = {};
static char g_desc_system[64] = {};
static int  g_desc_scroll = 0;

/* rating selector */
static int g_rating_sel = 0;   /* 1–5 star selection in picker, 0 = cancel */

/* last successfully launched game — used by the IGM for cheats and save states */
static LauncherGame g_active_game = {};

/* input repeat */
static uint32_t g_repeat_timer = 0;
static uint32_t g_repeat_key   = 0;
static bool     g_first_repeat = false;

/* library loading thread */
static pthread_t  g_lib_thread;
static volatile bool g_lib_done = false;
static volatile bool g_lib_ok   = false;

/* ─── helpers ─────────────────────────────────────────────────────────────── */

/* lerp for float */
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* absolute time in ms (monotonic) */
static inline uint32_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ─── background drawing ────────────────────────────────────────────────── */

static void draw_background(Imlib_Image fb)
{
    if (g_bg_img) {
        launcher_blit_image(fb, g_bg_img, 0, 0, g_fb_w, g_fb_h);
    } else {
        launcher_clear(fb, LC_BG);
    }
}

/* ─── framebuffer setup ──────────────────────────────────────────────────── */

static void fb_setup(void)
{
    video_get_fb_dims(&g_fb_w, &g_fb_h);
    if (!g_fb_w || !g_fb_h) { g_fb_w = 1920; g_fb_h = 1080; }

    for (int p = 1; p <= 2; p++) {
        if (g_fb_img[p]) {
            imlib_context_set_image(g_fb_img[p]);
            imlib_free_image_and_decache();
            g_fb_img[p] = NULL;
        }
        volatile uint32_t *ptr = video_get_fb_ptr(p);
        if (ptr)
            g_fb_img[p] = imlib_create_image_using_data(
                g_fb_w, g_fb_h, (uint32_t*)ptr);
    }
}

static Imlib_Image cur_fb(void) { return g_fb_img[g_back_page]; }
static void fb_flip(void)
{
    video_fb_enable(1, g_back_page);
    g_back_page = (g_back_page == 1) ? 2 : 1;
}

/* ─── fade ───────────────────────────────────────────────────────────────── */

static void fade_begin_out(void (*callback)(void))
{
    g_post_fade  = callback;
    g_fade_dir   = FADE_OUT;
}

static void fade_begin_in(void)
{
    g_fade_dir   = FADE_IN;
    g_fade_alpha = 255;
}

static void fade_tick(void)
{
    if (g_fade_dir == FADE_OUT) {
        g_fade_alpha += LAUNCHER_FADE_SPEED;
        if (g_fade_alpha >= 255) {
            g_fade_alpha = 255;
            g_fade_dir   = FADE_IDLE;
            if (g_post_fade) { auto cb = g_post_fade; g_post_fade = NULL; cb(); }
        }
    } else if (g_fade_dir == FADE_IN) {
        g_fade_alpha -= LAUNCHER_FADE_SPEED;
        if (g_fade_alpha <= 0) { g_fade_alpha = 0; g_fade_dir = FADE_IDLE; }
    }
}

/* ─── input ──────────────────────────────────────────────────────────────── */

static uint32_t g_input_key = 0;

static void input_tick(void)
{
    uint32_t raw = menu_key_get();

    if (raw && !(raw & UPSTROKE)) {
        if (raw == g_repeat_key) {
            uint32_t delay = g_first_repeat ? LAUNCHER_REPEAT_DELAY : LAUNCHER_REPEAT_RATE;
            if ((ms_now() - g_repeat_timer) >= delay) {
                g_repeat_timer   = ms_now();
                g_first_repeat   = false;
                g_input_key      = raw;
            } else {
                g_input_key = 0;
            }
        } else {
            g_repeat_key    = raw;
            g_repeat_timer  = ms_now();
            g_first_repeat  = true;
            g_input_key     = raw;
        }
    } else {
        if (raw & UPSTROKE) g_repeat_key = 0;
        g_input_key = raw;
    }
}

static inline bool key_down(uint32_t k)  { return g_input_key == k; }
static inline bool key_up_pressed(uint32_t k) { return g_input_key == (k | UPSTROKE); }

/* ─── backfill play data ─────────────────────────────────────────────────── */

static void backfill_play_data(LauncherSystem *systems, int sys_count,
                                const LauncherState *st)
{
    for (int h = 0; h < st->history_count; h++) {
        const HistoryEntry *he = &st->history[h];
        for (int s = 0; s < sys_count; s++) {
            if (strcmp(systems[s].name, he->system) != 0) continue;
            for (int g = 0; g < systems[s].game_count; g++) {
                if (strcmp(systems[s].games[g].path, he->path) == 0) {
                    systems[s].games[g].play_count  = (uint16_t)he->count;
                    systems[s].games[g].last_played = he->ts;
                    systems[s].games[g].play_time   = he->play_time;
                    systems[s].games[g].user_rating = he->user_rating;
                }
            }
        }
    }
}

/* ─── library loading thread ─────────────────────────────────────────────── */

static void *lib_loader_thread(void *)
{
    /* If SFTP is configured, scan the remote server and generate/refresh CSVs
       before loading the library.  Fresh CSVs (<24h) are skipped automatically. */
    if (cfg.sftp_host[0])
        launcher_scan_remote(g_base_dir);

    bool ok = launcher_load_library(g_base_dir, &g_real_sys, &g_real_cnt);
    launcher_load_state(g_state_path, &g_state);
    launcher_state_apply_play_time(&g_state, g_state_path);
    backfill_play_data(g_real_sys, g_real_cnt, &g_state);
    g_lib_ok   = ok;
    g_lib_done = true;
    return NULL;
}

/* ─── rebuild virtual systems ────────────────────────────────────────────── */

static void rebuild_all_systems(void)
{
    if (g_all_sys) {
        int extra = g_all_cnt - g_real_cnt;
        launcher_free_virtual_systems(g_all_sys, g_all_cnt, g_real_cnt);
    }
    g_all_sys = launcher_build_all_systems(g_real_sys, g_real_cnt, &g_state, &g_all_cnt);
}

/* ─── filtered game list ──────────────────────────────────────────────────── */

/* ─── version selector helpers ──────────────────────────────────────────── */

static void strip_tags(const char *src, char *out, int maxlen)
{
    strncpy(out, src, maxlen - 1);
    out[maxlen-1] = '\0';
    while (true) {
        int len = (int)strlen(out);
        if (len == 0) break;
        if (out[len-1] != ')' && out[len-1] != ']') break;
        char open = (out[len-1] == ')') ? '(' : '[';
        int i = len - 2;
        while (i >= 0 && out[i] != open) i--;
        if (i < 0) break;
        out[i] = '\0';
        while (i > 0 && out[i-1] == ' ') out[--i] = '\0';
    }
}

static void compute_has_variants(void)
{
    memset(g_variant_count, 0, sizeof(int) * g_filtered_cnt);
    for (int i = 0; i < g_filtered_cnt; i++) {
        char bi[128];
        strip_tags(g_filtered[i].name, bi, sizeof(bi));
        int group_size = 1;
        /* look ahead in the sorted list (±50 window) */
        for (int j = i+1; j < g_filtered_cnt && j < i+50; j++) {
            char bj[128];
            strip_tags(g_filtered[j].name, bj, sizeof(bj));
            if (strcasecmp(bi, bj) == 0) group_size++;
        }
        if (group_size > 1) {
            /* mark all members with the group size */
            for (int j = i; j < g_filtered_cnt && j < i+50; j++) {
                char bj[128];
                strip_tags(g_filtered[j].name, bj, sizeof(bj));
                if (strcasecmp(bi, bj) == 0) g_variant_count[j] = group_size;
            }
        }
    }
}

static void find_variants(int game_idx)
{
    char base[128];
    strip_tags(g_filtered[game_idx].name, base, sizeof(base));
    g_ver_count = 0;
    for (int i = 0; i < g_filtered_cnt && g_ver_count < LAUNCHER_MAX_GAMES; i++) {
        char bi[128];
        strip_tags(g_filtered[i].name, bi, sizeof(bi));
        if (strcasecmp(base, bi) == 0) {
            g_ver_indices[g_ver_count++] = i;
        }
    }
}

/* ─── game filter ────────────────────────────────────────────────────────── */

static void apply_game_filter(void)
{
    if (!g_all_sys || g_sys_sel < 0 || g_sys_sel >= g_all_cnt) return;
    LauncherSystem *sys = &g_all_sys[g_sys_sel];

    if (g_filtered_owned) { free(g_filtered); }
    g_filtered = NULL;
    g_filtered_cnt = 0;
    g_filtered_owned = false;

    /* clear multi-select state when filter changes */
    memset(g_sel_bits, 0, sizeof(g_sel_bits));
    g_sel_count = 0;
    g_multi_mode = false;

    bool has_search = g_search_mode && g_search[0];
    bool has_fav_filter = g_fav_filter;

    /* no search, no fav_filter: pass-through */
    if (!has_search && !has_fav_filter) {
        g_filtered_cnt = sys->game_count;
        g_filtered     = sys->games;
        g_filtered_owned = false;
        if (g_filtered_cnt > 0) compute_has_variants();
        return;
    }

    /* need to malloc: either search, fav_filter, or both */
    g_filtered = (LauncherGame*)malloc(sys->game_count * sizeof(LauncherGame));
    g_filtered_owned = true;

    for (int i = 0; i < sys->game_count; i++) {
        bool matches_search = true;
        bool matches_fav = true;

        /* check search filter */
        if (has_search) {
            const char *hay = sys->games[i].name;
            const char *ndl = g_search;
            bool found = false;
            for (int h = 0; hay[h] && !found; h++) {
                bool match = true;
                for (int n = 0; ndl[n] && match; n++)
                    if (tolower(hay[h+n]) != tolower(ndl[n])) match = false;
                if (match && ndl[0]) found = true;
            }
            matches_search = found;
        }

        /* check favorites filter */
        if (has_fav_filter) {
            matches_fav = launcher_state_is_favourite(&g_state, &sys->games[i]);
        }

        /* include game only if it passes both filters */
        if (matches_search && matches_fav) {
            g_filtered[g_filtered_cnt++] = sys->games[i];
        }
    }
    if (g_filtered_cnt > 0) compute_has_variants();
}

/* ─── library stats ──────────────────────────────────────────────────────── */

static void compute_stats(void)
{
    /* sum up all games/bytes from g_real_sys */
    g_stats_total_systems = g_real_cnt;
    g_stats_total_games = 0;
    g_stats_total_bytes = 0;
    for (int s = 0; s < g_real_cnt; s++) {
        g_stats_total_games += g_real_sys[s].game_count;
        for (int g = 0; g < g_real_sys[s].game_count; g++) {
            g_stats_total_bytes += (uint64_t)g_real_sys[s].games[g].file_size;
        }
    }

    /* sum history plays and play_time */
    g_stats_fav_count = g_state.fav_count;
    g_stats_total_plays = 0;
    g_stats_total_play_time = 0;
    memset(g_stats_most_played, 0, sizeof(g_stats_most_played));
    g_stats_most_played_cnt = 0;

    for (int h = 0; h < g_state.history_count; h++) {
        g_stats_total_plays += g_state.history[h].count;
        g_stats_total_play_time += g_state.history[h].play_time;
        if (g_state.history[h].count > g_stats_most_played_cnt) {
            g_stats_most_played_cnt = g_state.history[h].count;
            strncpy(g_stats_most_played, g_state.history[h].name,
                    sizeof(g_stats_most_played) - 1);
        }
    }
}

static void reset_game_view(void)
{
    g_game_sel    = 0;
    g_game_scroll = 0;
    g_game_target = 0;
    g_search[0]   = '\0';
    g_search_mode = false;
    g_fav_filter  = false;
    apply_game_filter();
}

static int game_cols(void)
{
    int body_w = g_fb_w - LAUNCHER_PAD * 2;
    int c = body_w / LAUNCHER_CELL_W;
    return c < 1 ? 1 : c;
}

static void clamp_game_scroll(void)
{
    int cols   = game_cols();
    int body_h = g_fb_h - LAUNCHER_HEADER_H - LAUNCHER_FOOTER_H;
    int row    = g_game_sel / cols;
    int row_y  = row * LAUNCHER_CELL_H;
    /* ensure selected row is visible */
    if (row_y < g_game_target)
        g_game_target = row_y;
    else if (row_y + LAUNCHER_CELL_H > g_game_target + body_h)
        g_game_target = row_y + LAUNCHER_CELL_H - body_h;
    if (g_game_target < 0) g_game_target = 0;
}

/* ─── transition callbacks ───────────────────────────────────────────────── */

static void on_enter_systems(void)
{
    g_mode     = LMODE_SYSTEMS;
    g_sys_sel  = 0;
    g_sys_scroll = 0.0f;
    fade_begin_in();
}

static void on_enter_games(void)
{
    g_mode = LMODE_GAMES;
    g_fav_filter = false;
    reset_game_view();
    /* restore scroll position for this system */
    if (g_sys_sel >= 0 && g_sys_sel < LAUNCHER_MAX_SYSTEMS) {
        g_game_sel = g_state.per_system[g_sys_sel].selected_game;
        g_game_scroll = g_state.per_system[g_sys_sel].scroll_offset;
        g_game_target = g_game_scroll;
        clamp_game_scroll();
    }
    fade_begin_in();
}

/* ─── drawing: splash ────────────────────────────────────────────────────── */

static void draw_splash(void)
{
    Imlib_Image fb = cur_fb();
    draw_background(fb);

    int cx = g_fb_w / 2;
    int cy = g_fb_h / 2;

    /* title */
    launcher_draw_text_centred(fb, cx, cy - 70, "MiSTer Launcher", FONT_TITLE, LC_HI);
    launcher_draw_text_centred(fb, cx, cy - 24, "Game Library", FONT_BIG, LC_TEXT);

    /* animated dots */
    uint32_t elapsed = ms_now() - g_splash_timer;
    int dots = (elapsed / 450) % 4;
    char loading[32];
    strcpy(loading, "Loading");
    for (int i = 0; i < dots; i++) strcat(loading, ".");
    launcher_draw_text_centred(fb, cx, cy + 32, loading, FONT_SM, LC_DIM);

    /* pulsing bar */
    float bar_t = sinf(g_splash_bar) * 0.5f + 0.5f;
    int track_w = 300, track_h = 8;
    int tx = cx - track_w / 2;
    int ty = g_fb_h - 55;
    launcher_fill_rect_rounded(fb, tx, ty, track_w, track_h, 4, LC_CARD);
    int hi_w = 80;
    int hi_x = tx + (int)((track_w - hi_w) * bar_t);
    launcher_fill_rect_rounded(fb, hi_x, ty, hi_w, track_h, 4, LC_HI);

    /* version */
    launcher_draw_text_centred(fb, cx, g_fb_h - 32, "v2 | MiSTer FPGA", FONT_SM, LC_DIM);
}

/* ─── drawing: system carousel ───────────────────────────────────────────── */

static void draw_systems(void)
{
    Imlib_Image fb = cur_fb();
    draw_background(fb);
    if (cfg.launcher_particles) launcher_particles_draw(fb);

    int cx = g_fb_w / 2;
    int card_y = g_fb_h / 2 - LAUNCHER_SYS_H / 2;

    /* title */
    launcher_draw_text_centred(fb, cx, 22, "Choose a System", FONT_TITLE, LC_HI);

    for (int i = 0; i < g_all_cnt; i++) {
        float x_f = (float)((i - g_sys_sel) * (LAUNCHER_SYS_W + 20)) - g_sys_scroll
                    + (g_fb_w - LAUNCHER_SYS_W) / 2.0f;
        int   x   = (int)x_f;
        int   y   = card_y;
        bool  sel = (i == g_sys_sel);

        if (x + LAUNCHER_SYS_W < 0 || x > g_fb_w) continue;

        int w = LAUNCHER_SYS_W, h = LAUNCHER_SYS_H;
        if (sel) {
            float s = g_sys_scale;
            int sw = (int)(w * s), sh = (int)(h * s);
            x  = x  - (sw - w) / 2;
            y  = y  + (int)g_sys_float_y - (sh - h) / 2;
            w  = sw; h = sh;
            launcher_draw_glow(fb, x, y, w, h, LC_HI, 4);
            /* derive selected-card colour from theme: brighten LC_CARD by 0x18 per channel */
            uint32_t base = LC_CARD;
            uint32_t cr = ((base >> 16) & 0xFF) + 0x18;
            uint32_t cg = ((base >>  8) & 0xFF) + 0x18;
            uint32_t cb = ( base        & 0xFF) + 0x18;
            if (cr > 255) cr = 255;
            if (cg > 255) cg = 255;
            if (cb > 255) cb = 255;
            uint32_t sel_col = 0xFF000000u | (cr << 16) | (cg << 8) | cb;
            launcher_fill_rect_rounded(fb, x, y, w, h, 8, sel_col);
        } else {
            launcher_fill_rect_rounded(fb, x, y, w, h, 8, LC_CARD);
        }

        /* only draw text on cards fully within screen bounds — prevents
           centred text from overflowing the screen edge on partially-visible
           cards in the carousel */
        if (x >= 0 && x + w <= g_fb_w) {
            uint32_t name_col = sel ? LC_HI : LC_TEXT;
            int txt_x = x + w / 2;
            launcher_draw_text_centred(fb, txt_x, y + h / 2 - 14, g_all_sys[i].name, FONT_BIG, name_col);

            char count_str[32];
            snprintf(count_str, sizeof(count_str), "%d games", g_all_sys[i].game_count);
            launcher_draw_text_centred(fb, txt_x, y + h / 2 + 8, count_str, FONT_SM, LC_DIM);
        }
    }

    /* footer hints */
    launcher_fill_rect(fb, 0, g_fb_h - LAUNCHER_FOOTER_H, g_fb_w, LAUNCHER_FOOTER_H, LC_BAR);
    launcher_draw_text_centred(fb, cx, g_fb_h - LAUNCHER_FOOTER_H + 16,
        "Left/Right: Navigate   A/Enter: Select   B/Esc: Exit Launcher   S: Stats",
        FONT_SM, LC_DIM);
}

/* ─── drawing: library stats ─────────────────────────────────────────────── */

static void draw_stats(void)
{
    Imlib_Image fb = cur_fb();
    draw_background(fb);

    int cx = g_fb_w / 2;
    int cy = g_fb_h / 2;

    /* title */
    launcher_draw_text_centred(fb, cx, 40, "LIBRARY STATISTICS", FONT_TITLE, LC_HI);

    /* two-column card layout */
    int card_w = 280;
    int card_h = 200;
    int card_x1 = cx - card_w - 20;
    int card_x2 = cx + 20;
    int card_y = 110;

    /* left column background */
    launcher_fill_rect_rounded(fb, card_x1, card_y, card_w, card_h, 8, LC_CARD);

    /* right column background */
    launcher_fill_rect_rounded(fb, card_x2, card_y, card_w, card_h, 8, LC_CARD);

    /* left column: systems, games, size */
    int y_offset = 10;
    char buf[128];

    snprintf(buf, sizeof(buf), "Systems: %d", g_stats_total_systems);
    launcher_draw_text_centred(fb, card_x1 + card_w/2, card_y + y_offset, buf, FONT_SM, LC_TEXT);

    snprintf(buf, sizeof(buf), "Games: %d", g_stats_total_games);
    launcher_draw_text_centred(fb, card_x1 + card_w/2, card_y + y_offset + 30, buf, FONT_SM, LC_TEXT);

    if (g_stats_total_bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "Size: %.1f GB", g_stats_total_bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "Size: %.0f MB", g_stats_total_bytes / (1024.0 * 1024.0));
    }
    launcher_draw_text_centred(fb, card_x1 + card_w/2, card_y + y_offset + 60, buf, FONT_SM, LC_TEXT);

    snprintf(buf, sizeof(buf), "Favourites: %d", g_stats_fav_count);
    launcher_draw_text_centred(fb, card_x1 + card_w/2, card_y + y_offset + 90, buf, FONT_SM, LC_TEXT);

    snprintf(buf, sizeof(buf), "Plays: %d", g_stats_total_plays);
    launcher_draw_text_centred(fb, card_x1 + card_w/2, card_y + y_offset + 120, buf, FONT_SM, LC_TEXT);

    /* right column: play time, most played */
    uint32_t hours = g_stats_total_play_time / 3600;
    uint32_t minutes = (g_stats_total_play_time % 3600) / 60;
    snprintf(buf, sizeof(buf), "Play Time: %uh %um", hours, minutes);
    launcher_draw_text_centred(fb, card_x2 + card_w/2, card_y + y_offset, buf, FONT_SM, LC_TEXT);

    if (g_stats_most_played[0]) {
        snprintf(buf, sizeof(buf), "Most Played:");
        launcher_draw_text_centred(fb, card_x2 + card_w/2, card_y + y_offset + 35, buf, FONT_SM, LC_DIM);
        snprintf(buf, sizeof(buf), "%s", g_stats_most_played);
        launcher_draw_text_centred(fb, card_x2 + card_w/2, card_y + y_offset + 60, buf, FONT_SM, LC_TEXT);
        snprintf(buf, sizeof(buf), "(%d plays)", g_stats_most_played_cnt);
        launcher_draw_text_centred(fb, card_x2 + card_w/2, card_y + y_offset + 80, buf, FONT_SM, LC_DIM);
    }

    /* footer bar */
    launcher_fill_rect(fb, 0, g_fb_h - LAUNCHER_FOOTER_H, g_fb_w, LAUNCHER_FOOTER_H, LC_BAR);
    launcher_draw_text_centred(fb, cx, g_fb_h - LAUNCHER_FOOTER_H + 16,
        "B/Esc: Back to Systems",
        FONT_SM, LC_DIM);
}

/* ─── drawing: game grid ─────────────────────────────────────────────────── */

static void draw_games(void)
{
    Imlib_Image fb = cur_fb();
    draw_background(fb);
    if (cfg.launcher_particles) launcher_particles_draw(fb);

    int cols    = game_cols();
    int body_y0 = LAUNCHER_HEADER_H;
    int body_h  = g_fb_h - LAUNCHER_HEADER_H - LAUNCHER_FOOTER_H;
    int scroll  = g_game_scroll;

    float pulse = fmodf((float)ms_now() / 1000.0f, 1.0f);

    /* narrow iteration to visible rows only — avoids O(N) scan over full library */
    int row_first = (scroll >= LAUNCHER_CELL_H) ? (scroll / LAUNCHER_CELL_H - 1) : 0;
    int row_last  = (scroll + body_h) / LAUNCHER_CELL_H + 1;
    int idx_first = row_first * cols;
    int idx_last  = (row_last + 1) * cols;
    if (idx_first < 0)              idx_first = 0;
    if (idx_last  > g_filtered_cnt) idx_last  = g_filtered_cnt;

    /* request visible covers */
    for (int i = idx_first; i < idx_last; i++) {
        int y = body_y0 + (i / cols) * LAUNCHER_CELL_H - scroll;
        if (y + LAUNCHER_CELL_H < body_y0 || y > g_fb_h) continue;
        launcher_cover_request(&g_filtered[i]);
    }

    /* one placeholder image for the whole frame, reused for every cache-miss */
    Imlib_Image ph = launcher_make_placeholder(pulse);

    /* draw covers */
    for (int i = idx_first; i < idx_last; i++) {
        int col = i % cols;
        int row = i / cols;
        int x   = LAUNCHER_PAD + col * LAUNCHER_CELL_W;
        int y   = body_y0 + row * LAUNCHER_CELL_H - scroll;

        if (y + LAUNCHER_CELL_H < body_y0 || y > g_fb_h) continue;

        bool sel = (i == g_game_sel);
        if (sel) launcher_draw_glow(fb, x, y, LAUNCHER_COVER_W, LAUNCHER_COVER_H, LC_HI, 4);

        uint32_t fa = 0;
        Imlib_Image cover = launcher_cover_get_ex(g_filtered[i].cover_path, &fa);
        if (cover) {
            launcher_blit_image(fb, cover, x, y, LAUNCHER_COVER_W, LAUNCHER_COVER_H);
            if (fa < 255) {
                imlib_context_set_image(fb);
                imlib_context_set_color(0, 0, 0, 255 - fa);
                imlib_image_fill_rectangle(x, y, LAUNCHER_COVER_W, LAUNCHER_COVER_H);
            }
        } else if (ph) {
            launcher_blit_image(fb, ph, x, y, LAUNCHER_COVER_W, LAUNCHER_COVER_H);
        }

        /* selection sheen */
        if (sel) {
            imlib_context_set_image(fb);
            imlib_context_set_color(255, 255, 255, 28);
            imlib_image_fill_rectangle(x, y, LAUNCHER_COVER_W, LAUNCHER_COVER_H);
        }

        /* favourite badge */
        if (launcher_state_is_favourite(&g_state, &g_filtered[i]))
            launcher_draw_fav_badge(fb, x + LAUNCHER_COVER_W - 10, y + 10);

        /* selection badge (bulk select mode) */
        if (g_multi_mode && i < LAUNCHER_MAX_GAMES && g_sel_bits[i])
            launcher_draw_sel_badge(fb, x + LAUNCHER_COVER_W - 24, y + 4);

        /* variant badge (indicates multiple ROM versions available) */
        if (i < LAUNCHER_MAX_GAMES && g_variant_count[i] > 1) {
            char vbuf[8];
            snprintf(vbuf, sizeof(vbuf), "v%d", g_variant_count[i]);
            launcher_draw_variant_badge(fb, x + 4, y + 4, vbuf);
        }

        /* game name label */
        launcher_draw_text_clipped(fb, x, y + LAUNCHER_COVER_H + 4,
            g_filtered[i].name, FONT_SM, LC_TEXT, LAUNCHER_CELL_W - 4);
    }

    if (ph) { imlib_context_set_image(ph); imlib_free_image(); }

    /* header bar */
    launcher_fill_rect(fb, 0, 0, g_fb_w, LAUNCHER_HEADER_H, LC_BAR);
    if (g_sys_sel < g_all_cnt) {
        if (g_fav_filter) {
            launcher_draw_text(fb, LAUNCHER_PAD, 16, "[★ Favourites]", FONT_TITLE, LC_FAV);
            char sys_name[80];
            snprintf(sys_name, sizeof(sys_name), "%s", g_all_sys[g_sys_sel].name);
            int tw = launcher_text_width(sys_name, FONT_TITLE);
            launcher_draw_text(fb, LAUNCHER_PAD + launcher_text_width("[★ Favourites] ", FONT_TITLE), 16,
                             sys_name, FONT_TITLE, LC_HI);
        } else {
            launcher_draw_text(fb, LAUNCHER_PAD, 16, g_all_sys[g_sys_sel].name, FONT_TITLE, LC_HI);
        }
    }
    if (g_filtered_cnt > 0 && g_game_sel < g_filtered_cnt) {
        char sel_str[256];
        snprintf(sel_str, sizeof(sel_str), "%d / %d", g_game_sel + 1, g_filtered_cnt);
        if (g_search_mode && g_search[0]) {
            char tmp[512]; snprintf(tmp, sizeof(tmp), "%s  [%s]", sel_str, g_search);
            strncpy(sel_str, tmp, sizeof(sel_str) - 1);
            sel_str[sizeof(sel_str) - 1] = '\0';
        }
        launcher_draw_text_centred(fb, g_fb_w / 2, 26, sel_str, FONT_SM, LC_DIM);
        int tw = launcher_text_width(g_filtered[g_game_sel].name, FONT_SM);
        int mw = launcher_game_metadata_width(&g_filtered[g_game_sel]);
        int max_w = (tw > mw) ? tw : mw;
        int tx = g_fb_w - LAUNCHER_PAD - max_w;
        launcher_draw_text(fb, tx, 26, g_filtered[g_game_sel].name, FONT_SM, LC_TEXT);
        /* display metadata below selected game name */
        launcher_draw_game_metadata(fb, &g_filtered[g_game_sel], tx, 45);

        /* display sort mode indicator */
        const char *sort_labels[] = { "A-Z", "Z-A", "Recent", "Most Played", "Top Rated" };
        char sort_str[32];
        snprintf(sort_str, sizeof(sort_str), "Sort: %s",
                 g_state.sort_order < (int)(sizeof(sort_labels)/sizeof(sort_labels[0])) ?
                 sort_labels[g_state.sort_order] : "?");
        launcher_draw_text(fb, LAUNCHER_PAD, 45, sort_str, FONT_SM, LC_DIM);
    } else if (g_fav_filter && g_filtered_cnt == 0) {
        /* no results and favorites filter is on */
        launcher_draw_text_centred(fb, g_fb_w / 2, g_fb_h / 2 - 20,
            "No favourites in this system — press F to show all",
            FONT_BIG, LC_DIM);
    }

    /* footer bar */
    launcher_fill_rect(fb, 0, g_fb_h - LAUNCHER_FOOTER_H, g_fb_w, LAUNCHER_FOOTER_H, LC_BAR);
    if (g_search_mode) {
        char sbuf[256];
        int cursor = (ms_now() / 500) % 2 ? '|' : ' ';
        bool no_matches = (g_search[0] && g_filtered_cnt == 0);
        if (g_search[0] && g_sys_sel < g_all_cnt) {
            int total = g_all_sys[g_sys_sel].game_count;
            if (no_matches) {
                snprintf(sbuf, sizeof(sbuf), "Search: %s%c   No matches", g_search, cursor);
            } else {
                snprintf(sbuf, sizeof(sbuf), "Search: %s%c   (%d / %d)", g_search, cursor,
                         g_filtered_cnt, total);
            }
        } else {
            snprintf(sbuf, sizeof(sbuf), "Search: %s%c", g_search, cursor);
        }
        launcher_draw_text_centred(fb, g_fb_w / 2, g_fb_h - LAUNCHER_FOOTER_H + 16,
            sbuf, FONT_SM, no_matches ? LC_ERR : LC_HI);
    } else if (g_multi_mode) {
        char mbuf[200];
        snprintf(mbuf, sizeof(mbuf), "Space: Toggle   A: Select All   F: Fav All   Ins: Exit   [%d selected]",
                 g_sel_count);
        launcher_draw_text_centred(fb, g_fb_w / 2, g_fb_h - LAUNCHER_FOOTER_H + 16,
            mbuf, FONT_SM, LC_DIM);
    } else {
        launcher_draw_text_centred(fb, g_fb_w / 2, g_fb_h - LAUNCHER_FOOTER_H + 16,
            "A: Launch   B: Back   X/Y: Fav   F: Fav Filter   L1: Search   ?:Help   Tab: Settings",
            FONT_SM, LC_DIM);
    }
}

/* ─── drawing: launching overlay ─────────────────────────────────────────── */

static void draw_launching_overlay(void)
{
    /* draw game grid underneath */
    draw_games();
    Imlib_Image fb = cur_fb();

    int cx = g_fb_w / 2, cy = g_fb_h / 2;

    /* semi-transparent overlay */
    launcher_fill_rect(fb, 0, 0, g_fb_w, g_fb_h, LC_OVERLAY);

    /* spinner + message */
    launcher_draw_spinner(fb, cx, cy - 40, 22, g_spinner_angle, LC_HI);
    launcher_draw_text_centred(fb, cx, cy + 10, "Downloading…", FONT_BIG, LC_TEXT);

    /* show bytes downloaded so far by checking the .part file size */
    if (g_active_game.path[0]) {
        char local[600], part[608];
        launcher_cache_path(&g_active_game, g_base_dir, local, sizeof(local));
        snprintf(part, sizeof(part), "%s.part", local);
        struct stat pst;
        if (stat(part, &pst) == 0 && pst.st_size > 0) {
            char prog[64];
            if (pst.st_size >= 1024 * 1024)
                snprintf(prog, sizeof(prog), "%.1f MB downloaded", pst.st_size / (1024.0 * 1024.0));
            else
                snprintf(prog, sizeof(prog), "%.0f KB downloaded", pst.st_size / 1024.0);
            launcher_draw_text_centred(fb, cx, cy + 36, prog, FONT_SM, LC_DIM);
        }
    }

    launcher_draw_text_centred(fb, cx, cy + 60, "B/Esc: Cancel", FONT_SM, LC_DIM);
}

/* ─── drawing: error overlay ─────────────────────────────────────────────── */

static void draw_error_overlay(void)
{
    draw_games();
    Imlib_Image fb = cur_fb();
    int cx = g_fb_w / 2, cy = g_fb_h / 2;

    launcher_fill_rect(fb, 0, 0, g_fb_w, g_fb_h, LC_OVERLAY);
    launcher_draw_text_centred(fb, cx, cy - 20, g_error_msg, FONT_BIG, LC_ERR);
    launcher_draw_text_centred(fb, cx, cy + 20, "Any key to dismiss", FONT_SM, LC_DIM);
}

/* ─── in-game menu (called from menu.cpp MENU_LAUNCHER_IGM) ──────────────── */

static int      g_igm_sel    = 0;
static IGMMode  g_igm_mode   = IGM_MODE_MENU;
static bool     g_igm_info   = false;
static char     g_igm_toast[128] = {};
static uint32_t g_igm_toast_end  = 0;
static bool     g_igm_active = false;
static char     g_igm_game_name[128] = {};
static char     g_igm_game_sys[64]   = {};

/* save-state sub-screen */
static int  g_ss_sel = 0;     /* 0-3 */

/* cheat browser sub-screen */
static int  g_cheat_scroll = 0;  /* index of first visible row */

/* video settings sub-screen: 0=H-Filter, 1=V-Filter, 2=Gamma, 3=Shadow Mask */
static int  g_video_sel = 0;

/* game info extras (populated in launcher_igm_begin) */
static char     g_igm_game_path[512]    = {};
static char     g_igm_game_filesize[32] = {};
static int      g_igm_game_launches     = 0;
static char     g_igm_game_lastplay[64] = {};

static const char *igm_labels[IGM_COUNT] = {
    "Return to Game",
    "Load Save State",
    "Cheats",
    "Video Settings",
    "Core Settings",
    "Game Info",
    "Back to Main Menu"
};

void launcher_igm_begin(void)
{
    g_igm_sel    = 0;
    g_igm_mode   = IGM_MODE_MENU;
    g_igm_info   = false;
    g_igm_active = true;
    g_igm_toast[0] = '\0';
    OsdPause(1);
    video_fb_enable(1, 1); /* make page 1 visible */

    /* populate game name/system from active game (set at launch time) */
    if (g_active_game.name[0]) {
        strncpy(g_igm_game_name, g_active_game.name,   sizeof(g_igm_game_name) - 1);
        strncpy(g_igm_game_sys,  g_active_game.system, sizeof(g_igm_game_sys)   - 1);
        strncpy(g_igm_game_path, g_active_game.path,   sizeof(g_igm_game_path) - 1);
    } else {
        g_igm_game_name[0] = '\0';
        g_igm_game_sys[0]  = '\0';
        g_igm_game_path[0] = '\0';
    }

    /* file size: check cache first, then remote path */
    g_igm_game_filesize[0] = '\0';
    {
        struct stat fst;
        off_t fsize = 0;
        char cache[600];
        launcher_cache_path(&g_active_game, g_base_dir, cache, sizeof(cache));
        if (stat(cache, &fst) == 0 && fst.st_size > 0)
            fsize = fst.st_size;
        else if (g_active_game.path[0] && stat(g_active_game.path, &fst) == 0)
            fsize = fst.st_size;
        if (fsize >= 1024 * 1024)
            snprintf(g_igm_game_filesize, sizeof(g_igm_game_filesize),
                     "%.1f MB", fsize / (1024.0 * 1024.0));
        else if (fsize >= 1024)
            snprintf(g_igm_game_filesize, sizeof(g_igm_game_filesize),
                     "%.0f KB", fsize / 1024.0);
        else if (fsize > 0)
            snprintf(g_igm_game_filesize, sizeof(g_igm_game_filesize),
                     "%lld B", (long long)fsize);
    }

    /* play count and last-played timestamp from history */
    g_igm_game_launches  = 0;
    g_igm_game_lastplay[0] = '\0';
    for (int i = 0; i < g_state.history_count; i++) {
        if (strcmp(g_state.history[i].path, g_active_game.path) == 0) {
            g_igm_game_launches = g_state.history[i].count;
            time_t t = (time_t)g_state.history[i].ts;
            struct tm *ltm = localtime(&t);
            if (ltm) strftime(g_igm_game_lastplay, sizeof(g_igm_game_lastplay),
                              "%Y-%m-%d  %H:%M", ltm);
            break;
        }
    }

    fb_setup();
}

/* ─── IGM: save-state slot picker ────────────────────────────────────────── */

static void draw_igm_savestates(Imlib_Image fb)
{
    int cx    = g_fb_w / 2;
    int panel_w = 560;
    int panel_x = cx - panel_w / 2;
    int panel_y = g_fb_h / 2 - 160;

    launcher_fill_rect(fb, 0, 0, g_fb_w, g_fb_h, LC_OVERLAY);

    launcher_draw_text_centred(fb, cx, panel_y, "SAVE STATES", FONT_TITLE, LC_HI);
    launcher_draw_text_centred(fb, cx, panel_y + 38, g_igm_game_name, FONT_SM, LC_DIM);

    int item_h = 52;
    int y = panel_y + 74;
    for (int i = 0; i < 4; i++) {
        char path[1200];
        FileGenerateSavestatePath(g_active_game.path, path, i + 1);

        struct stat st;
        bool exists = (stat(path, &st) == 0);

        bool sel = (i == g_ss_sel);
        uint32_t bg   = sel ? LC_HI   : LC_CARD;
        uint32_t fg   = sel ? LC_BLACK : LC_TEXT;
        uint32_t fg2  = sel ? LC_BLACK : LC_DIM;

        if (sel) launcher_draw_glow(fb, panel_x, y, panel_w, item_h - 4, LC_HI, 3);
        launcher_fill_rect_rounded(fb, panel_x, y, panel_w, item_h - 4, 6, bg);

        char slot_label[32];
        snprintf(slot_label, sizeof(slot_label), "Slot %d", i + 1);
        launcher_draw_text(fb, panel_x + 18, y + (item_h - 4) / 2 - 10, slot_label, FONT_BIG, fg);

        if (exists) {
            /* format the modification time */
            char ts[64];
            struct tm *tm = localtime(&st.st_mtime);
            strftime(ts, sizeof(ts), "%Y-%m-%d  %H:%M:%S", tm);
            int tw = launcher_text_width(ts, FONT_SM);
            launcher_draw_text(fb, panel_x + panel_w - tw - 18,
                               y + (item_h - 4) / 2 - 8, ts, FONT_SM, fg2);
        } else {
            int tw = launcher_text_width("empty", FONT_SM);
            launcher_draw_text(fb, panel_x + panel_w - tw - 18,
                               y + (item_h - 4) / 2 - 8, "empty", FONT_SM, fg2);
        }
        y += item_h;
    }

    launcher_draw_text_centred(fb, cx, g_fb_h - 30,
        "Up/Down: Select   A/Enter: Load   B/Esc: Back", FONT_SM, LC_DIM);
}

/* ─── IGM: cheat browser ─────────────────────────────────────────────────── */

#define IGM_CHEAT_ROWS  10   /* visible rows in the cheat list */

static void draw_igm_cheats(Imlib_Image fb)
{
    int n  = cheats_available();
    int cx = g_fb_w / 2;
    int panel_w = 680;
    int panel_x = cx - panel_w / 2;
    int header_y = g_fb_h / 2 - (IGM_CHEAT_ROWS * 36) / 2 - 60;

    launcher_fill_rect(fb, 0, 0, g_fb_w, g_fb_h, LC_OVERLAY);

    launcher_draw_text_centred(fb, cx, header_y, "CHEATS", FONT_TITLE, LC_HI);
    launcher_draw_text_centred(fb, cx, header_y + 38, g_igm_game_name, FONT_SM, LC_DIM);

    if (n == 0) {
        launcher_draw_text_centred(fb, cx, g_fb_h / 2, "No cheats available for this game",
                                   FONT_BIG, LC_DIM);
        launcher_draw_text_centred(fb, cx, g_fb_h - 30, "B/Esc: Back", FONT_SM, LC_DIM);
        return;
    }

    int sel = cheats_get_selected();
    /* clamp scroll so the selected row is visible */
    if (sel < g_cheat_scroll) g_cheat_scroll = sel;
    if (sel >= g_cheat_scroll + IGM_CHEAT_ROWS) g_cheat_scroll = sel - IGM_CHEAT_ROWS + 1;
    if (g_cheat_scroll < 0) g_cheat_scroll = 0;

    int row_h = 36;
    int list_y = header_y + 68;

    /* scroll indicator */
    if (g_cheat_scroll > 0)
        launcher_draw_text_centred(fb, cx, list_y - 20, "▲", FONT_SM, LC_DIM);
    if (g_cheat_scroll + IGM_CHEAT_ROWS < n)
        launcher_draw_text_centred(fb, cx, list_y + IGM_CHEAT_ROWS * row_h + 4, "▼", FONT_SM, LC_DIM);

    for (int row = 0; row < IGM_CHEAT_ROWS; row++) {
        int idx = g_cheat_scroll + row;
        if (idx >= n) break;

        bool is_sel     = (idx == sel);
        bool is_enabled = cheats_is_enabled(idx);

        uint32_t bg   = is_sel ? LC_HI   : 0x00000000u;
        uint32_t fg   = is_sel ? LC_BLACK : (is_enabled ? LC_WHITE : LC_TEXT);
        uint32_t tick_col = is_sel ? LC_BLACK : (is_enabled ? LC_HI : LC_DIM);

        int ry = list_y + row * row_h;
        if (bg) launcher_fill_rect_rounded(fb, panel_x, ry - 2, panel_w, row_h - 2, 4, bg);

        /* checkbox: filled square = enabled, outline square = disabled */
        const char *checkbox = is_enabled ? "\x1a" : "\x1b"; /* OSD check/uncheck chars */
        launcher_draw_text(fb, panel_x + 12, ry + 4, checkbox, FONT_SM, tick_col);
        launcher_draw_text_clipped(fb, panel_x + 38, ry + 4,
                                   cheats_get_name(idx), FONT_SM, fg,
                                   panel_w - 50);
    }

    /* active count footer */
    char footer[64];
    int active = cheats_loaded();
    if (active > 0)
        snprintf(footer, sizeof(footer), "%d active   A/Enter: Toggle   B/Esc: Back", active);
    else
        snprintf(footer, sizeof(footer), "A/Enter: Toggle   B/Esc: Back");
    launcher_draw_text_centred(fb, cx, g_fb_h - 30, footer, FONT_SM, LC_DIM);
}

/* ─── IGM: video settings ────────────────────────────────────────────────── */

#define IGM_VIDEO_ITEMS 4

static const char *s_video_labels[IGM_VIDEO_ITEMS] = {
    "H-Filter",
    "V-Filter",
    "Gamma",
    "Shadow Mask"
};

static void draw_igm_video(Imlib_Image fb)
{
    int cx      = g_fb_w / 2;
    int panel_w = 560;
    int panel_x = cx - panel_w / 2;
    int row_h   = 58;
    int header_y = g_fb_h / 2 - (IGM_VIDEO_ITEMS * row_h) / 2 - 50;

    launcher_fill_rect(fb, 0, 0, g_fb_w, g_fb_h, LC_OVERLAY);
    launcher_draw_text_centred(fb, cx, header_y, "VIDEO SETTINGS", FONT_TITLE, LC_HI);

    int y = header_y + 68;
    for (int i = 0; i < IGM_VIDEO_ITEMS; i++) {
        bool sel = (i == g_video_sel);
        uint32_t label_col = sel ? LC_HI : LC_DIM;
        launcher_draw_text(fb, panel_x, y + 16, s_video_labels[i], FONT_SM, label_col);

        /* current value string */
        char val[128] = {};
        switch (i) {
        case 0: {
            char *name = video_get_scaler_coeff(VFILTER_HORZ);
            snprintf(val, sizeof(val), "%s", (name && name[0]) ? name : "None");
            break;
        }
        case 1: {
            char *name = video_get_scaler_coeff(VFILTER_VERT);
            snprintf(val, sizeof(val), "%s", (name && name[0]) ? name : "None");
            break;
        }
        case 2:
            snprintf(val, sizeof(val), "%s", video_get_gamma_en() ? "On" : "Off");
            break;
        case 3: {
            char *name = video_get_shadow_mask();
            snprintf(val, sizeof(val), "%s", (name && name[0]) ? name : "None");
            break;
        }
        }

        int vw = launcher_text_width(val, FONT_BIG);
        int vx = cx - vw / 2;

        if (sel) {
            launcher_draw_text(fb, cx - 80 - 10, y + 12, "<", FONT_BIG, LC_HI);
            launcher_draw_text(fb, vx,           y + 12, val, FONT_BIG, LC_TEXT);
            launcher_draw_text(fb, cx + 80 - 10, y + 12, ">", FONT_BIG, LC_HI);
        } else {
            launcher_draw_text(fb, vx, y + 12, val, FONT_BIG, LC_DIM);
        }
        y += row_h;
    }

    launcher_draw_text_centred(fb, cx, g_fb_h - 30,
        "Up/Down: Navigate   Left/Right: Adjust   B/Esc: Back",
        FONT_SM, LC_DIM);
}

/* ─── IGM: main tick ─────────────────────────────────────────────────────── */

/* returns 0 if IGM should close */
int launcher_igm_tick(void)
{
    if (!g_igm_active) return 0;

    input_tick();
    uint32_t k = g_input_key;

    /* ── Save-state sub-screen ── */
    if (g_igm_mode == IGM_MODE_SAVESTATES) {
        bool back    = key_up_pressed(KEY_ESC);
        bool confirm = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE);

        if (key_down(KEY_UP)   || key_up_pressed(KEY_UP))
            g_ss_sel = (g_ss_sel - 1 + 4) % 4;
        if (key_down(KEY_DOWN) || key_up_pressed(KEY_DOWN))
            g_ss_sel = (g_ss_sel + 1) % 4;

        if (back) {
            g_igm_mode = IGM_MODE_MENU;
        } else if (confirm) {
            /* load the selected slot */
            if (process_ss_load_slot(g_ss_sel)) {
                OsdPause(0);
                video_fb_enable(0, 0);
                g_igm_active = false;
                return 0;   /* unpause — core will restore the loaded state */
            } else {
                snprintf(g_igm_toast, sizeof(g_igm_toast),
                         "Slot %d is empty", g_ss_sel + 1);
                g_igm_toast_end = ms_now() + 2000;
            }
        }

        Imlib_Image fb = g_fb_img[1];
        if (!fb) { video_fb_enable(0, 0); g_igm_active = false; return 0; }
        draw_igm_savestates(fb);
        goto draw_toast;
    }

    /* ── Cheat browser sub-screen ── */
    if (g_igm_mode == IGM_MODE_CHEATS) {
        bool back    = key_up_pressed(KEY_ESC);
        bool confirm = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE);

        int n = cheats_available();
        if (key_down(KEY_UP) || key_up_pressed(KEY_UP)) {
            if (n > 0) cheats_set_selected((cheats_get_selected() - 1 + n) % n);
        }
        if (key_down(KEY_DOWN) || key_up_pressed(KEY_DOWN)) {
            if (n > 0) cheats_set_selected((cheats_get_selected() + 1) % n);
        }
        if (key_down(KEY_PAGEUP) || key_up_pressed(KEY_PAGEUP)) {
            if (n > 0) cheats_set_selected(
                (cheats_get_selected() - IGM_CHEAT_ROWS + n) % n);
        }
        if (key_down(KEY_PAGEDOWN) || key_up_pressed(KEY_PAGEDOWN)) {
            if (n > 0) cheats_set_selected(
                (cheats_get_selected() + IGM_CHEAT_ROWS) % n);
        }

        if (back) {
            g_igm_mode = IGM_MODE_MENU;
        } else if (confirm && n > 0) {
            cheats_toggle();
        }

        Imlib_Image fb = g_fb_img[1];
        if (!fb) { video_fb_enable(0, 0); g_igm_active = false; return 0; }
        draw_igm_cheats(fb);
        goto draw_toast;
    }

    /* ── Video settings sub-screen ── */
    if (g_igm_mode == IGM_MODE_VIDEO) {
        bool back  = key_up_pressed(KEY_ESC);
        bool left  = key_down(KEY_LEFT)  || key_up_pressed(KEY_LEFT);
        bool right = key_down(KEY_RIGHT) || key_up_pressed(KEY_RIGHT);

        if (key_down(KEY_UP) || key_up_pressed(KEY_UP))
            g_video_sel = (g_video_sel - 1 + IGM_VIDEO_ITEMS) % IGM_VIDEO_ITEMS;
        if (key_down(KEY_DOWN) || key_up_pressed(KEY_DOWN))
            g_video_sel = (g_video_sel + 1) % IGM_VIDEO_ITEMS;

        if (left || right) {
            int delta = right ? 1 : -1;
            switch (g_video_sel) {
            case 0: {  /* H-Filter */
                int n = video_get_scaler_flt(VFILTER_HORZ) + delta;
                if (n >= 0) video_set_scaler_flt(VFILTER_HORZ, n);
                break;
            }
            case 1: {  /* V-Filter */
                int n = video_get_scaler_flt(VFILTER_VERT) + delta;
                if (n >= 0) video_set_scaler_flt(VFILTER_VERT, n);
                break;
            }
            case 2:    /* Gamma */
                video_set_gamma_en(video_get_gamma_en() ? 0 : 1);
                break;
            case 3: {  /* Shadow Mask — 4 modes: 0..3 */
                int n = video_get_shadow_mask_mode() + delta;
                if (n >= 0 && n <= 3) video_set_shadow_mask_mode(n);
                break;
            }
            }
        }

        if (back) g_igm_mode = IGM_MODE_MENU;

        Imlib_Image fb = g_fb_img[1];
        if (!fb) { video_fb_enable(0, 0); g_igm_active = false; return 0; }
        draw_igm_video(fb);
        goto draw_toast;
    }

    /* ── Main IGM menu ── */
    if (!g_igm_info) {
        if (key_down(KEY_UP)   || key_up_pressed(KEY_UP))
            g_igm_sel = (g_igm_sel - 1 + IGM_COUNT) % IGM_COUNT;
        if (key_down(KEY_DOWN) || key_up_pressed(KEY_DOWN))
            g_igm_sel = (g_igm_sel + 1) % IGM_COUNT;

        bool confirm = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE);
        bool back    = key_up_pressed(KEY_ESC);

        /* chord again = return to game */
        if (launcher_igm_chord && !confirm) {
            launcher_igm_chord = 0; confirm = true; g_igm_sel = IGM_RETURN;
        }
        if (back) { g_igm_sel = IGM_RETURN; confirm = true; }

        if (confirm) {
            switch ((IGMAction)g_igm_sel) {
            case IGM_RETURN:
                OsdPause(0);
                video_fb_enable(0, 0);
                g_igm_active = false;
                return 0;

            case IGM_SAVE_STATE:
                g_ss_sel   = 0;
                g_igm_mode = IGM_MODE_SAVESTATES;
                break;

            case IGM_CHEATS:
                g_cheat_scroll = 0;
                cheats_set_selected(0);
                g_igm_mode = IGM_MODE_CHEATS;
                break;

            case IGM_VIDEO_SETTINGS:
                g_video_sel = 0;
                g_igm_mode  = IGM_MODE_VIDEO;
                break;

            case IGM_CORE_SETTINGS:
                OsdPause(0);
                video_fb_enable(0, 0);
                g_igm_active = false;
                menu_key_set(KEY_F12);
                return 0;

            case IGM_GAME_INFO:
                g_igm_info = true;
                break;

            case IGM_MAIN_MENU:
                OsdPause(0);
                video_fb_enable(0, 0);
                g_igm_active = false;
                launcher_load_core("menu.rbf");
                return 0;

            default:
                break;
            }
        }
    } else {
        /* info sub-screen: any key returns */
        if (k && !(k & UPSTROKE)) g_igm_info = false;
    }

    {   /* draw main menu or info screen */
        Imlib_Image fb = g_fb_img[1];
        if (!fb) { video_fb_enable(0, 0); g_igm_active = false; return 0; }

        launcher_fill_rect(fb, 0, 0, g_fb_w, g_fb_h, LC_OVERLAY);

        if (!g_igm_info) {
            int menu_w  = 360;
            int menu_x  = g_fb_w / 2 - menu_w / 2;
            int item_h  = 52;
            int total_h = IGM_COUNT * item_h + 80;
            int menu_y0 = g_fb_h / 2 - total_h / 2;

            launcher_draw_text_centred(fb, g_fb_w / 2, menu_y0, "PAUSED", FONT_TITLE, LC_HI);
            launcher_draw_text_centred(fb, g_fb_w / 2, menu_y0 + 38, g_igm_game_name, FONT_SM, LC_DIM);

            int y = menu_y0 + 70;
            for (int i = 0; i < IGM_COUNT; i++) {
                bool sel = (i == g_igm_sel);
                uint32_t bg_col   = sel ? LC_HI   : LC_CARD;
                uint32_t text_col = sel ? LC_BLACK : LC_TEXT;
                if (sel) launcher_draw_glow(fb, menu_x, y, menu_w, item_h - 4, LC_HI, 3);
                launcher_fill_rect_rounded(fb, menu_x, y, menu_w, item_h - 4, 6, bg_col);
                launcher_draw_text_centred(fb, g_fb_w / 2, y + (item_h - 4) / 2 - 10,
                    igm_labels[i], FONT_BIG, text_col);
                y += item_h;
            }
            launcher_draw_text_centred(fb, g_fb_w / 2, g_fb_h - 30,
                "Up/Down: Navigate   A/Enter: Select", FONT_SM, LC_DIM);
        } else {
            int cx     = g_fb_w / 2;
            int lx     = cx - 260;   /* label left edge */
            int vx     = cx - 60;    /* value left edge */
            int y      = g_fb_h / 2 - 130;

            launcher_draw_text_centred(fb, cx, y, "GAME INFO", FONT_TITLE, LC_HI);
            y += 54;

            /* row: label (right-aligned at lx) + value */
            auto info_row = [&](const char *label, const char *value, uint32_t vcol) {
                launcher_draw_text(fb, lx, y, label, FONT_SM, LC_DIM);
                launcher_draw_text_clipped(fb, vx, y, value, FONT_BIG, vcol,
                                           g_fb_w - vx - 40);
                y += 42;
            };

            info_row("Name",        g_igm_game_name[0]    ? g_igm_game_name    : "—", LC_TEXT);
            info_row("System",      g_igm_game_sys[0]     ? g_igm_game_sys     : "—", LC_TEXT);

            /* file: show only the basename of the path */
            const char *bn = strrchr(g_igm_game_path, '/');
            bn = bn ? bn + 1 : g_igm_game_path;
            info_row("File",        bn[0] ? bn : "—", LC_DIM);

            info_row("Size",        g_igm_game_filesize[0] ? g_igm_game_filesize : "—", LC_TEXT);

            char launches_str[32];
            if (g_igm_game_launches > 0)
                snprintf(launches_str, sizeof(launches_str), "%d", g_igm_game_launches);
            else
                strcpy(launches_str, "—");
            info_row("Launches",    launches_str, LC_TEXT);
            info_row("Last Played", g_igm_game_lastplay[0] ? g_igm_game_lastplay : "—", LC_TEXT);

            launcher_draw_text_centred(fb, cx, g_fb_h - 30,
                "Any button: Back", FONT_SM, LC_DIM);
        }
    }

draw_toast:
    {
        Imlib_Image fb = g_fb_img[1];
        if (fb && g_igm_toast[0]) {
            if (ms_now() < g_igm_toast_end) {
                int tw = launcher_text_width(g_igm_toast, FONT_SM);
                int tx = g_fb_w / 2 - tw / 2 - 16;
                int ty = g_fb_h - 80;
                launcher_fill_rect_rounded(fb, tx, ty, tw + 32, 34, 4, LC_BAR);
                launcher_draw_text_centred(fb, g_fb_w / 2, ty + 8, g_igm_toast, FONT_SM, LC_HI);
            } else {
                g_igm_toast[0] = '\0';
            }
        }
    }

    video_fb_enable(1, 1);
    return 1;
}

/* ─── init ───────────────────────────────────────────────────────────────── */

void launcher_init(void)
{
    /* base paths from INI config */
    const char *bd = (cfg.launcher_path[0]) ? cfg.launcher_path : "/media/fat/remote_ui";
    strncpy(g_base_dir, bd, sizeof(g_base_dir) - 1);
    snprintf(g_state_path, sizeof(g_state_path), "%s/state.dat", g_base_dir);

    /* framebuffer */
    fb_setup();

    /* theme system (must be before launcher_draw_init since it reads font sizes) */
    launcher_theme_init(g_base_dir, cfg.launcher_theme);

    /* drawing subsystem */
    launcher_draw_init();

    /* load background image if theme specifies one */
    if (g_theme.bg_image[0]) {
        Imlib_Load_Error err = IMLIB_LOAD_ERROR_NONE;
        Imlib_Image raw = imlib_load_image_with_error_return(g_theme.bg_image, &err);
        if (raw) {
            imlib_context_set_image(raw);
            int w = imlib_image_get_width();
            int h = imlib_image_get_height();
            g_bg_img = imlib_create_cropped_scaled_image(0, 0, w, h, g_fb_w, g_fb_h);
            imlib_free_image();
            if (g_bg_img)
                printf("launcher: loaded background image %s (%dx%d)\n", g_theme.bg_image, g_fb_w, g_fb_h);
        } else {
            printf("launcher: failed to load background image %s\n", g_theme.bg_image);
        }
    }

    /* particles */
    launcher_particles_init(g_fb_w, g_fb_h);

    /* cover worker */
    launcher_cover_worker_start();

    /* begin fade-in from black */
    g_fade_dir   = FADE_IN;
    g_fade_alpha = 255;
    g_mode       = LMODE_SPLASH;
    g_splash_timer = ms_now();

    /* fire library load in background */
    g_lib_done = false;
    pthread_create(&g_lib_thread, NULL, lib_loader_thread, NULL);

    /* enable framebuffer */
    video_fb_enable(1, g_back_page);
}

/* ─── shutdown ───────────────────────────────────────────────────────────── */

void launcher_shutdown(void)
{
    launcher_cancel_download();
    launcher_cover_worker_stop();
    launcher_draw_shutdown();

    if (g_bg_img) {
        imlib_context_set_image(g_bg_img);
        imlib_free_image();
        g_bg_img = NULL;
    }

    if (g_real_sys) {
        launcher_free_library(g_real_sys, g_real_cnt);
        g_real_sys = NULL; g_real_cnt = 0;
    }
    if (g_all_sys) {
        int extra = g_all_cnt - g_real_cnt;
        launcher_free_virtual_systems(g_all_sys, g_all_cnt, g_real_cnt);
        g_all_sys = NULL; g_all_cnt = 0;
    }
    free(g_filtered);
    g_filtered     = NULL;
    g_filtered_cnt = 0;

    video_fb_enable(0, 0);
}

/* ─── cover batch download ──────────────────────────────────────────────── */

static void enter_cover_dl(void)
{
    /* point to current system and count games with cover_path */
    g_cdl_sys = &g_all_sys[g_sys_sel];
    g_cdl_total = 0;
    for (int i = 0; i < g_cdl_sys->game_count; i++) {
        if (g_cdl_sys->games[i].cover_path[0] != '\0') {
            g_cdl_total++;
            /* queue the cover for loading */
            launcher_cover_request(&g_cdl_sys->games[i]);
        }
    }
    g_cdl_done = 0;
    g_cdl_check_idx = 0;
    g_mode = LMODE_COVER_DL;
}

/* ─── bulk select ────────────────────────────────────────────────────────── */

static void toggle_multi_mode(void)
{
    g_multi_mode = !g_multi_mode;
    if (!g_multi_mode) {
        /* clear selection when exiting multi-mode */
        memset(g_sel_bits, 0, sizeof(g_sel_bits));
        g_sel_count = 0;
    }
}

/* ─── main tick (called from HandleUI MENU_LAUNCHER2) ────────────────────── */

int launcher_tick(void)  /* returns 0 to exit to stock MiSTer menu */
{
    input_tick();
    uint32_t k = g_input_key;

    /* ── frame timing ── */
    uint32_t now_ms = ms_now();
    uint32_t frame_time = (g_last_frame_ms > 0) ? (now_ms - g_last_frame_ms) : 16;
    g_frame_times[g_frame_time_idx++ % 60] = frame_time;
    g_last_frame_ms = now_ms;

    /* toggle performance display (press ~) */
    if (key_up_pressed(KEY_GRAVE)) {
        g_state.show_performance = !g_state.show_performance;
    }

    /* toggle help screen (press ?) — only in splash/systems/games modes */
    if (key_up_pressed(KEY_QUESTION) && (g_mode == LMODE_SPLASH || g_mode == LMODE_SYSTEMS || g_mode == LMODE_GAMES)) {
        g_mode = LMODE_HELP;
    }

    /* enter settings menu (press Tab) — only in systems/games modes */
    if (key_up_pressed(KEY_TAB) && (g_mode == LMODE_SYSTEMS || g_mode == LMODE_GAMES)) {
        g_settings_prev_mode = g_mode;
        g_mode = LMODE_SETTINGS;
        g_settings_sel = 0;
    }

    /* ── per-frame updates ── */
    if (cfg.launcher_particles) launcher_particles_update(g_fb_h);
    int covers_loaded = launcher_cover_flush(4);
    launcher_cover_cache_tick();
    g_spinner_angle += 0.10f;
    g_splash_bar    += 0.030f;
    fade_tick();

    /* smooth scroll */
    float prev_sys_scroll = g_sys_scroll;
    float prev_game_scroll_f = (float)g_game_scroll;
    g_sys_scroll  = lerpf(g_sys_scroll,  g_sys_target,  0.18f);
    g_sys_scale   = lerpf(g_sys_scale,   1.08f,         0.14f);
    g_sys_float_y = lerpf(g_sys_float_y, -10.0f,        0.14f);
    g_game_scroll = (int)lerpf((float)g_game_scroll, (float)g_game_target, 0.18f);

    /* ── dirty flag: mark redraw needed ── */
    if (k)                                             g_dirty = true;  /* any input */
    if (covers_loaded > 0)                             g_dirty = true;  /* cover loaded/fading in */
    if (g_fade_alpha > 0 || g_fade_dir != FADE_IDLE)  g_dirty = true;  /* fade active */
    if (fabsf(g_sys_scroll - prev_sys_scroll) > 0.1f) g_dirty = true;  /* carousel scroll */
    if (fabsf((float)g_game_scroll - prev_game_scroll_f) > 0.1f) g_dirty = true;  /* game scroll */
    if (g_mode == LMODE_SPLASH || g_mode == LMODE_LAUNCHING) g_dirty = true;  /* always animating */
    if (cfg.launcher_particles)                        g_dirty = true;  /* particles always move */

    if (!g_dirty) {
        fb_flip();   /* still flip so vsync is honoured */
        return 1;
    }
    g_dirty = false;

    /* ── state dispatch ── */
    switch (g_mode) {

    case LMODE_SPLASH: {
        /* check if library loaded AND minimum splash time elapsed */
        if (g_lib_done && !g_splash_min_done) {
            uint32_t el = ms_now() - g_splash_timer;
            if (el >= LAUNCHER_SPLASH_MIN) g_splash_min_done = true;
        }
        if (g_lib_done && g_splash_min_done && g_fade_dir == FADE_IDLE) {
            if (g_lib_ok) {
                rebuild_all_systems();
                backfill_play_data(g_all_sys, g_all_cnt, &g_state);
                g_splash_min_done = false;
                fade_begin_out(on_enter_systems);
            } else {
                snprintf(g_error_msg, sizeof(g_error_msg),
                         "Failed to load library from lists");
                g_mode = LMODE_ERROR;
            }
        }
        draw_splash();
        break;
    }

    case LMODE_SYSTEMS: {
        int n = g_all_cnt;

        if (key_down(KEY_LEFT) || key_down(KEY_RIGHT)) {
            int dx = key_down(KEY_RIGHT) ? 1 : -1;
            int new_sel = g_sys_sel + dx;
            if (new_sel >= 0 && new_sel < n) {
                g_sys_sel   = new_sel;
                g_sys_scale = 0.92f;
                g_sys_float_y = 5.0f;
                float step = (float)(LAUNCHER_SYS_W + 20);
                g_sys_target = (float)(g_sys_sel - (g_all_cnt - 1) / 2) * step;
            }
        }

        bool enter = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE);
        bool back  = key_up_pressed(KEY_ESC);

        if (key_up_pressed(KEY_S) && g_real_cnt > 0) {
            compute_stats();
            g_mode = LMODE_STATS;
        } else if (enter && n > 0) {
            fade_begin_out(on_enter_games);
        } else if (back) {
            launcher_shutdown();
            return 0;  /* exit to stock MiSTer menu */
        }

        draw_systems();
        break;
    }

    case LMODE_GAMES: {
        int cols = game_cols();
        int n    = g_filtered_cnt;

        /* L1/R1 (PAGEUP/PAGEDOWN): cycle through sort modes */
        if (key_up_pressed(KEY_PAGEUP) || key_up_pressed(KEY_PAGEDOWN)) {
            int next_sort = g_state.sort_order;
            if (key_up_pressed(KEY_PAGEDOWN)) {
                next_sort = (next_sort + 1) % SORT_COUNT;
            } else {
                next_sort = (next_sort - 1 + SORT_COUNT) % SORT_COUNT;
            }
            if (next_sort != g_state.sort_order) {
                g_state.sort_order = next_sort;
                /* re-sort the filtered games */
                if (g_filtered_cnt > 0) {
                    launcher_sort_games(g_filtered, g_filtered_cnt, g_state.sort_order);
                    g_game_sel = 0;  /* reset selection to top */
                    g_game_scroll = 0;
                    g_game_target = 0;
                }
            }
        }

        /* multi-select mode & cover download toggle & description (before search) */
        if (key_up_pressed(KEY_INSERT)) {
            toggle_multi_mode();
        }
        if (key_up_pressed(KEY_D) && !g_search_mode && !g_multi_mode) {
            enter_cover_dl();
        }
        if (key_up_pressed(KEY_I) && !g_search_mode && g_filtered_cnt > 0) {
            const LauncherGame *game = &g_filtered[g_game_sel];
            strncpy(g_desc_game_name, game->name,   sizeof(g_desc_game_name) - 1);
            strncpy(g_desc_system,    game->system, sizeof(g_desc_system) - 1);
            g_desc_scroll = 0;
            launcher_desc_request(game, g_base_dir);
            g_mode = LMODE_DESCRIPTION;
        }

        if (key_up_pressed(KEY_R) && !g_search_mode && g_filtered_cnt > 0) {
            /* pre-select current rating so cursor starts on it */
            int cur = g_filtered[g_game_sel].user_rating;
            g_rating_sel = (cur > 0) ? cur : 3;  /* default to 3 if unrated */
            g_mode = LMODE_RATING;
        }

        /* favorites filter toggle (F key) */
        if (key_up_pressed(KEY_F)) {
            g_fav_filter ^= 1;
            g_game_sel = 0;
            g_game_scroll = 0;
            apply_game_filter();
        }

        if (!g_search_mode) {
            if (key_down(KEY_LEFT))  { if (g_game_sel % cols > 0) { g_game_sel--; clamp_game_scroll(); } }
            if (key_down(KEY_RIGHT)) { if (g_game_sel % cols < cols - 1 && g_game_sel + 1 < n) { g_game_sel++; clamp_game_scroll(); } }
            if (key_down(KEY_UP))    { if (g_game_sel - cols >= 0) { g_game_sel -= cols; clamp_game_scroll(); } }
            if (key_down(KEY_DOWN))  { if (g_game_sel + cols < n)  { g_game_sel += cols; clamp_game_scroll(); } }

            bool enter = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE);
            bool back  = key_up_pressed(KEY_ESC);
            bool fav_key = (k == (KEY_X | UPSTROKE)) || (k == (KEY_Y | UPSTROKE));
            bool key_a = key_up_pressed(KEY_A);

            /* handle multi-select mode */
            if (g_multi_mode) {
                if (enter && n > 0 && g_game_sel < n) {
                    /* toggle selection of current game */
                    if (g_game_sel < LAUNCHER_MAX_GAMES) {
                        if (g_sel_bits[g_game_sel]) {
                            g_sel_bits[g_game_sel] = false;
                            g_sel_count--;
                        } else {
                            g_sel_bits[g_game_sel] = true;
                            g_sel_count++;
                        }
                    }
                } else if (key_a) {
                    /* select all or deselect all */
                    if (g_sel_count < n) {
                        /* select all visible */
                        for (int i = 0; i < n && i < LAUNCHER_MAX_GAMES; i++) {
                            g_sel_bits[i] = true;
                        }
                        g_sel_count = n < LAUNCHER_MAX_GAMES ? n : LAUNCHER_MAX_GAMES;
                    } else {
                        /* deselect all */
                        memset(g_sel_bits, 0, sizeof(g_sel_bits));
                        g_sel_count = 0;
                    }
                } else if (fav_key) {
                    /* bulk toggle favourite on all selected games */
                    for (int i = 0; i < n && i < LAUNCHER_MAX_GAMES; i++) {
                        if (g_sel_bits[i]) {
                            launcher_state_toggle_fav(&g_state, &g_filtered[i], g_state_path);
                        }
                    }
                    apply_game_filter(); /* refresh favourite badges */
                    memset(g_sel_bits, 0, sizeof(g_sel_bits));
                    g_sel_count = 0;
                } else if (back) {
                    /* exit multi-select mode */
                    g_multi_mode = false;
                    memset(g_sel_bits, 0, sizeof(g_sel_bits));
                    g_sel_count = 0;
                }
            } else {
                /* normal mode (not multi-select) */
                if (enter && n > 0 && g_game_sel < n) {
                    /* check for variants before launching */
                    if (g_game_sel < LAUNCHER_MAX_GAMES && g_variant_count[g_game_sel] > 1) {
                        find_variants(g_game_sel);
                        g_ver_sel = 0;
                        for (int i = 0; i < g_ver_count; i++) {
                            if (g_ver_indices[i] == g_game_sel) { g_ver_sel = i; break; }
                        }
                        g_mode = LMODE_VERSION_SELECT;
                    } else {
                        /* start download + launch */
                        g_mode = LMODE_LAUNCHING;
                        bool cached = launcher_start_download(&g_filtered[g_game_sel], g_base_dir);
                        (void)cached;
                    }
                } else if (back) {
                    /* save scroll position before leaving */
                    if (g_sys_sel >= 0 && g_sys_sel < LAUNCHER_MAX_SYSTEMS) {
                        g_state.per_system[g_sys_sel].selected_game = g_game_sel;
                        g_state.per_system[g_sys_sel].scroll_offset = g_game_scroll;
                        launcher_save_state(g_state_path, &g_state);
                    }
                    fade_begin_out([](){ g_mode = LMODE_SYSTEMS; fade_begin_in(); });
                } else if (fav_key && n > 0 && g_game_sel < n) {
                    launcher_state_toggle_fav(&g_state, &g_filtered[g_game_sel], g_state_path);
                    apply_game_filter(); /* refresh favourite badges */
                }
            }

            /* type-to-search */
            uint8_t ascii = menu_ascii_key(k);
            if (ascii > 1 && ascii < 128) {
                g_search_mode = true;
                int sl = (int)strlen(g_search);
                if (sl < (int)sizeof(g_search) - 2) {
                    g_search[sl]   = (char)ascii;
                    g_search[sl+1] = '\0';
                }
                apply_game_filter();
                g_game_sel = 0; g_game_target = 0;
            }
        } else {
            /* search mode — handle text input */
            if (key_up_pressed(KEY_ESC)) {
                /* clear search */
                g_search[0] = '\0'; g_search_mode = false;
                apply_game_filter();
                g_game_sel = 0; g_game_target = 0;
            } else if (key_up_pressed(KEY_BACKSPACE)) {
                int sl = (int)strlen(g_search);
                if (sl > 0) g_search[sl - 1] = '\0';
                if (g_search[0] == '\0') { g_search_mode = false; }
                apply_game_filter();
                g_game_sel = 0; g_game_target = 0;
            } else {
                uint8_t ascii = menu_ascii_key(k);
                if (ascii > 1 && ascii < 128) {
                    int sl = (int)strlen(g_search);
                    if (sl < (int)sizeof(g_search) - 2) {
                        g_search[sl]   = (char)ascii;
                        g_search[sl+1] = '\0';
                    }
                    apply_game_filter();
                    g_game_sel = 0; g_game_target = 0;
                }
                /* arrows still navigate in search mode */
                int cols2 = game_cols(); int n2 = g_filtered_cnt;
                if (key_down(KEY_LEFT))  { if (g_game_sel % cols2 > 0) { g_game_sel--; clamp_game_scroll(); } }
                if (key_down(KEY_RIGHT)) { if (g_game_sel % cols2 < cols2-1 && g_game_sel+1 < n2) { g_game_sel++; clamp_game_scroll(); } }
                if (key_down(KEY_UP))    { if (g_game_sel - cols2 >= 0) { g_game_sel -= cols2; clamp_game_scroll(); } }
                if (key_down(KEY_DOWN))  { if (g_game_sel + cols2 < n2)  { g_game_sel += cols2; clamp_game_scroll(); } }
                if (key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE)) {
                    if (n2 > 0 && g_game_sel < n2) {
                        /* check for variants before launching */
                        if (g_game_sel < LAUNCHER_MAX_GAMES && g_variant_count[g_game_sel] > 1) {
                            find_variants(g_game_sel);
                            g_ver_sel = 0;
                            for (int i = 0; i < g_ver_count; i++) {
                                if (g_ver_indices[i] == g_game_sel) { g_ver_sel = i; break; }
                            }
                            g_mode = LMODE_VERSION_SELECT;
                        } else {
                            g_mode = LMODE_LAUNCHING;
                            launcher_start_download(&g_filtered[g_game_sel], g_base_dir);
                        }
                    }
                }
            }
        }
        draw_games();
        break;
    }

    case LMODE_LAUNCHING: {
        int status = launcher_poll_download(&g_filtered[g_game_sel], g_base_dir);
        bool cancel = key_up_pressed(KEY_ESC);

        if (cancel) {
            launcher_cancel_download();
            g_mode = LMODE_GAMES;
        } else if (status == 1) {
            /* download complete — update local path and write MGL */
            char local[600];
            launcher_cache_path(&g_filtered[g_game_sel], g_base_dir, local, sizeof(local));

            LauncherGame tmp = g_filtered[g_game_sel];
            strncpy(tmp.path, local, sizeof(tmp.path) - 1);

            if (launcher_write_mgl(&tmp)) {
                launcher_state_record_played(&g_state, &g_filtered[g_game_sel], g_state_path);
                /* write play start timestamp for later accumulation */
                FILE *tsfp = fopen("/tmp/launcher_play_start", "w");
                if (tsfp) { fprintf(tsfp, "%u\n", (uint32_t)time(NULL)); fclose(tsfp); }
                /* store game for IGM (cheats, save states) */
                g_active_game = tmp;
                /* initialise cheat engine with the local cached ROM path (CRC=0:
                   filename-based lookup runs first, CRC is a fallback) */
                cheats_init(tmp.path, 0);
                launcher_load_core("/media/fat/launch.mgl");
                /* process will restart — we're done */
                return 0;
            } else {
                snprintf(g_error_msg, sizeof(g_error_msg), "Failed to launch game");
                g_mode = LMODE_ERROR;
            }
        } else if (status == -1) {
            snprintf(g_error_msg, sizeof(g_error_msg), "Download failed");
            g_mode = LMODE_ERROR;
        }
        draw_launching_overlay();
        break;
    }

    case LMODE_ERROR:
        if (k && !(k & UPSTROKE)) { g_mode = LMODE_GAMES; }
        draw_error_overlay();
        break;

    case LMODE_HELP:
        /* any button to close */
        if (k && !(k & UPSTROKE)) { g_mode = LMODE_GAMES; }
        launcher_draw_help(cur_fb(), g_fb_w, g_fb_h);
        break;

    case LMODE_COVER_DL: {
        /* sliding window: stat() 10 covers/frame, count successes */
        if (g_cdl_sys && g_cdl_total > 0) {
            int check_count = (g_cdl_total - g_cdl_done < 10) ? (g_cdl_total - g_cdl_done) : 10;
            for (int i = 0; i < check_count && g_cdl_check_idx < g_cdl_sys->game_count; i++, g_cdl_check_idx++) {
                if (g_cdl_sys->games[g_cdl_check_idx].cover_path[0] != '\0') {
                    struct stat st;
                    if (stat(g_cdl_sys->games[g_cdl_check_idx].cover_path, &st) == 0 && st.st_size > 0) {
                        g_cdl_done++;
                    }
                }
            }
        }

        /* auto-close when done or ESC */
        bool cancel = key_up_pressed(KEY_ESC);
        if (cancel || g_cdl_done >= g_cdl_total) {
            g_mode = LMODE_GAMES;
        }

        draw_games();  /* game grid as background layer */
        launcher_draw_cover_dl(cur_fb(), g_fb_w, g_fb_h, g_cdl_done, g_cdl_total);
        break;
    }

    case LMODE_VERSION_SELECT: {
        if (key_up_pressed(KEY_UP))   g_ver_sel = (g_ver_sel - 1 + g_ver_count) % g_ver_count;
        if (key_up_pressed(KEY_DOWN)) g_ver_sel = (g_ver_sel + 1) % g_ver_count;

        bool confirm = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE);
        bool cancel  = key_up_pressed(KEY_ESC);

        if (confirm && g_ver_sel >= 0 && g_ver_sel < g_ver_count) {
            g_game_sel = g_ver_indices[g_ver_sel];
            g_mode = LMODE_LAUNCHING;
            launcher_start_download(&g_filtered[g_game_sel], g_base_dir);
        } else if (cancel) {
            g_mode = LMODE_GAMES;
        }

        draw_games();
        launcher_draw_version_select(cur_fb(), g_fb_w, g_fb_h,
                                     g_filtered, g_ver_indices, g_ver_count, g_ver_sel);
        break;
    }

    case LMODE_DESCRIPTION: {
        int state = launcher_desc_state();

        if (key_up_pressed(KEY_ESC)) {
            g_mode = LMODE_GAMES;
        } else if (state == LAUNCHER_DESC_READY) {
            if (key_down(KEY_UP))   g_desc_scroll = (g_desc_scroll > 0) ? g_desc_scroll - 1 : 0;
            if (key_down(KEY_DOWN)) g_desc_scroll++;
        }

        launcher_draw_description(cur_fb(), g_fb_w, g_fb_h,
                                  g_desc_game_name, g_desc_system,
                                  launcher_desc_text(), state, g_desc_scroll, ms_now(),
                                  launcher_desc_error());
        break;
    }

    case LMODE_RATING: {
        bool cancel  = key_up_pressed(KEY_ESC) || key_up_pressed(KEY_B);
        bool confirm = key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_A);
        bool clear   = key_up_pressed(KEY_X);  /* X clears rating */

        if (key_down(KEY_UP)   || key_up_pressed(KEY_UP))
            g_rating_sel = (g_rating_sel > 1) ? g_rating_sel - 1 : 1;
        if (key_down(KEY_DOWN) || key_up_pressed(KEY_DOWN))
            g_rating_sel = (g_rating_sel < 5) ? g_rating_sel + 1 : 5;

        if (cancel) {
            g_mode = LMODE_GAMES;
        } else if (clear) {
            /* remove rating */
            LauncherGame *gm = &g_filtered[g_game_sel];
            gm->user_rating  = 0;
            /* update HistoryEntry if present */
            for (int i = 0; i < g_state.history_count; i++) {
                if (strcmp(g_state.history[i].path, gm->path) == 0) {
                    g_state.history[i].user_rating = 0;
                    launcher_save_state(g_state_path, &g_state);
                    break;
                }
            }
            g_mode = LMODE_GAMES;
        } else if (confirm) {
            LauncherGame *gm = &g_filtered[g_game_sel];
            gm->user_rating  = (uint8_t)g_rating_sel;
            /* upsert into history — add entry if not already present */
            bool found = false;
            for (int i = 0; i < g_state.history_count; i++) {
                if (strcmp(g_state.history[i].path, gm->path) == 0) {
                    g_state.history[i].user_rating = (uint8_t)g_rating_sel;
                    found = true;
                    launcher_save_state(g_state_path, &g_state);
                    break;
                }
            }
            if (!found) {
                /* game not yet in history — create minimal entry */
                if (g_state.history_count < LAUNCHER_MAX_HISTORY) {
                    HistoryEntry *h = &g_state.history[g_state.history_count];
                    memset(h, 0, sizeof(*h));
                    strncpy(h->system, gm->system, sizeof(h->system) - 1);
                    strncpy(h->path,   gm->path,   sizeof(h->path)   - 1);
                    strncpy(h->name,   gm->name,   sizeof(h->name)   - 1);
                    h->user_rating = (uint8_t)g_rating_sel;
                    g_state.history_count++;
                    launcher_save_state(g_state_path, &g_state);
                }
            }
            g_mode = LMODE_GAMES;
        }

        launcher_draw_rating_modal(cur_fb(), g_fb_w, g_fb_h,
                                    g_filtered[g_game_sel].name, g_rating_sel,
                                    g_filtered[g_game_sel].user_rating);
        break;
    }

    case LMODE_SETTINGS: {
        /* navigation */
        if (key_up_pressed(KEY_UP)) {
            g_settings_sel = (g_settings_sel - 1 + 4) % 4;  /* 4 menu items */
        } else if (key_up_pressed(KEY_DOWN)) {
            g_settings_sel = (g_settings_sel + 1) % 4;
        }

        /* selection / toggle */
        if (key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE)) {
            if (g_settings_sel == 0) {  /* Toggle Particles */
                cfg.launcher_particles = !cfg.launcher_particles;
            } else if (g_settings_sel == 1) {  /* Select Theme */
                /* cycle to next theme */
                int theme_idx = 0;
                for (int i = 0; i < g_theme_count; i++) {
                    if (strcmp(cfg.launcher_theme, g_theme_names[i]) == 0) {
                        theme_idx = i;
                        break;
                    }
                }
                theme_idx = (theme_idx + 1) % g_theme_count;
                strncpy(cfg.launcher_theme, g_theme_names[theme_idx], sizeof(cfg.launcher_theme) - 1);
                /* reload theme */
                launcher_theme_init(g_base_dir, cfg.launcher_theme);
            } else if (g_settings_sel == 2) {  /* Performance Display */
                g_state.show_performance = !g_state.show_performance;
            }
            /* item 3 is "Back to Launcher" which is handled by back key below */
        }

        /* back */
        bool back = key_up_pressed(KEY_ESC);
        if (back || (g_settings_sel == 3 && (key_up_pressed(KEY_ENTER) || key_up_pressed(KEY_SPACE)))) {
            g_mode = g_settings_prev_mode;
            /* don't save state here; theme changes persist in cfg struct and will be saved on exit */
        }

        /* draw settings menu */
        launcher_draw_settings(cur_fb(), g_fb_w, g_fb_h, g_settings_sel,
                               cfg.launcher_particles, cfg.launcher_theme, g_state.show_performance);
        break;
    }

    case LMODE_STATS: {
        if (key_up_pressed(KEY_ESC) || key_up_pressed(KEY_B)) g_mode = LMODE_SYSTEMS;
        draw_stats();
        break;
    }

    case LMODE_SLEEPING:
    case LMODE_IGM:
    default:
        break;
    }

    /* fade overlay on top */
    if (g_fade_alpha > 0)
        launcher_draw_fade(cur_fb(), g_fade_alpha);

    /* performance overlay */
    if (g_state.show_performance)
        launcher_draw_performance(cur_fb(), g_frame_times, g_frame_time_idx, g_fb_w, g_fb_h);

    fb_flip();

    /* ── 60fps frame-rate limiter ── */
    /* Sleep for any remaining time in the 16ms budget to cap CPU usage */
    uint32_t elapsed_ms = ms_now() - now_ms;
    if (elapsed_ms < 16) {
        struct timespec ts = { 0, (long)(16 - elapsed_ms) * 1000000L };
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }

    return 1;
}
