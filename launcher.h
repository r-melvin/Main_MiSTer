#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <inttypes.h>
#include <pthread.h>
#include <linux/input.h>

/* ─── sub-states ─────────────────────────────────────────────────────────── */
enum LauncherMode
{
    LMODE_SPLASH,
    LMODE_SYSTEMS,
    LMODE_GAMES,
    LMODE_LAUNCHING,
    LMODE_ERROR,
    LMODE_COVER_DL,       /* cover batch download modal */
    LMODE_VERSION_SELECT, /* ROM version/region picker modal */
    LMODE_DESCRIPTION,    /* game description overlay */
    LMODE_SLEEPING,       /* game running – fb disabled, waiting for chord */
    LMODE_IGM,            /* in-game menu overlay */
    LMODE_SETTINGS,       /* launcher settings menu */
    LMODE_HELP,           /* keyboard shortcuts help */
    LMODE_STATS,          /* library statistics dashboard */
    LMODE_RATING          /* rating picker modal */
};

enum LauncherSortOrder
{
    SORT_NAME_ASC,
    SORT_NAME_DESC,
    SORT_RECENT,
    SORT_MOST_PLAYED,
    SORT_HIGHEST_RATED,
    SORT_COUNT
};

enum FadeDir { FADE_IDLE, FADE_IN, FADE_OUT };

enum IGMAction
{
    IGM_RETURN = 0,
    IGM_SAVE_STATE,
    IGM_CHEATS,
    IGM_VIDEO_SETTINGS,  /* native video/filter settings */
    IGM_CORE_SETTINGS,
    IGM_GAME_INFO,
    IGM_MAIN_MENU,
    IGM_COUNT
};

/* Sub-screens within the in-game menu */
enum IGMMode
{
    IGM_MODE_MENU,       /* main IGM item list */
    IGM_MODE_SAVESTATES, /* save-state slot picker */
    IGM_MODE_CHEATS,     /* cheat browser */
    IGM_MODE_VIDEO       /* video / filter settings */
};

/* ─── data structures ────────────────────────────────────────────────────── */
struct LauncherGame
{
    char name[128];
    char path[512];       /* remote SFTP or local absolute path */
    char cover_path[512]; /* local cover image absolute path */
    char system[64];
    uint32_t last_played; /* unix timestamp of last launch */
    uint16_t play_count;  /* number of times launched */
    uint32_t file_size;   /* file size in bytes (0 if unknown/remote) */
    uint32_t play_time;   /* cumulative play time in seconds */
    uint8_t  user_rating; /* backfilled from history */
};

#define LAUNCHER_MAX_SYSTEMS 64
#define LAUNCHER_MAX_GAMES   4096

struct LauncherSystem
{
    char name[64];
    LauncherGame *games;  /* heap-allocated array */
    int  game_count;
    int  is_virtual;      /* 1 = Recent / Favourites */
};

struct HistoryEntry
{
    char     system[64];
    char     path[512];
    char     name[128];
    char     cover_path[512];
    uint32_t ts;          /* unix timestamp */
    int      count;
    uint32_t play_time;   /* cumulative play time in seconds */
    uint8_t  user_rating; /* 0 = unrated, 1–5 = stars */
};

struct FavEntry
{
    char system[64];
    char path[512];
};

#define LAUNCHER_MAX_HISTORY  50
#define LAUNCHER_MAX_FAVS     256

struct SystemMemory
{
    int selected_game;   /* last selected game in this system */
    int scroll_offset;   /* last scroll position */
};

struct LauncherState
{
    HistoryEntry history[LAUNCHER_MAX_HISTORY];
    int          history_count;
    FavEntry     favs[LAUNCHER_MAX_FAVS];
    int          fav_count;
    SystemMemory per_system[LAUNCHER_MAX_SYSTEMS]; /* remember position in each system */
    int          sort_order;        /* LauncherSortOrder */
    uint8_t      show_performance;  /* 1 = display FPS counter */
};

/* ─── cover LRU cache ────────────────────────────────────────────────────── */
#define LAUNCHER_MAX_COVERS   250
/* forward-declare Imlib2 opaque type without including the full header */
typedef void *Imlib_Image;

struct CoverEntry
{
    char       path[512];
    Imlib_Image img;
    uint32_t   last_used; /* frame counter, 0 = empty slot */
    uint32_t   hash;      /* FNV-1a hash of path — fast rejection before strcmp */
};

/* ─── core map ───────────────────────────────────────────────────────────── */
struct CoreMapEntry
{
    const char *system;
    const char *dir;      /* e.g. "_Console" */
    const char *stem;     /* e.g. "SNES" → searches for SNES*.rbf */
    int         file_index;
};

/* ─── layout / timing constants ──────────────────────────────────────────── */
#define LAUNCHER_COVER_W       160
#define LAUNCHER_COVER_H       200
#define LAUNCHER_PAD            20
#define LAUNCHER_LABEL_H        26
#define LAUNCHER_CELL_W        180  /* COVER_W + PAD */
#define LAUNCHER_CELL_H        246  /* COVER_H + LABEL_H + PAD */
#define LAUNCHER_HEADER_H       75
#define LAUNCHER_FOOTER_H       50
#define LAUNCHER_SYS_W         180
#define LAUNCHER_SYS_H         220
#define LAUNCHER_FADE_SPEED     40
#define LAUNCHER_REPEAT_DELAY  300  /* ms, initial delay before repeat starts */
#define LAUNCHER_REPEAT_RATE    50  /* ms, interval between repeats */
#define LAUNCHER_AXIS_DEADZONE  0.45f
#define LAUNCHER_SPLASH_MIN    1800 /* ms */

/* ─── theme ──────────────────────────────────────────────────────────────── */
struct LauncherTheme {
    /* colours — ARGB 8888 (0xAARRGGBB) */
    uint32_t bg;        /* background fill */
    uint32_t card;      /* game / system cards */
    uint32_t hi;        /* selection highlight */
    uint32_t fav;       /* favourite badge ring */
    uint32_t text;      /* primary text */
    uint32_t dim;       /* secondary / hint text */
    uint32_t bar;       /* header and footer bars */
    uint32_t overlay;   /* semi-transparent modal overlay */
    uint32_t err;       /* error text */
    uint32_t search;    /* search bar background */
    /* fonts — point sizes for FONT_TITLE / FONT_BIG / FONT_SM */
    int      font_sizes[3];
    /* optional background image (absolute path; empty string = solid colour) */
    char     bg_image[512];
    char     name[64];
};

extern LauncherTheme g_theme;

/* ─── colour macros — backed by runtime theme values ─────────────────────── */
#define LC_BG      (g_theme.bg)
#define LC_CARD    (g_theme.card)
#define LC_HI      (g_theme.hi)
#define LC_FAV     (g_theme.fav)
#define LC_TEXT    (g_theme.text)
#define LC_DIM     (g_theme.dim)
#define LC_BAR     (g_theme.bar)
#define LC_OVERLAY (g_theme.overlay)
#define LC_ERR     (g_theme.err)
#define LC_SEARCH  (g_theme.search)
#define LC_WHITE   0xFFFFFFFFu
#define LC_BLACK   0xFF000000u

/* ─── font IDs ───────────────────────────────────────────────────────────── */
#define FONT_TITLE   0
#define FONT_BIG     1
#define FONT_SM      2

/* ─── public API ─────────────────────────────────────────────────────────── */
void launcher_init(void);
int  launcher_tick(void);       /* returns 0 to exit to stock MiSTer menu */
void launcher_shutdown(void);

/* In-Game Menu: begin() captures game snapshot; tick() drives the overlay.
   Returns 0 when the overlay is done (caller should resume normal rendering). */
void launcher_igm_begin(void);
int  launcher_igm_tick(void);

/* called by input.cpp when Select+Start chord fires during game play */
extern volatile int launcher_igm_chord;

/* the core map table (defined in launcher_io.cpp) */
extern const CoreMapEntry launcher_core_map[];
extern const int          launcher_core_map_count;

/* ─── theme API ──────────────────────────────────────────────────────────── */
void launcher_theme_init(const char *launcher_path, const char *theme_name);
void launcher_theme_load(const char *path);  /* load a specific .cfg file */

/* ─── sorting API ────────────────────────────────────────────────────────── */
void launcher_sort_games(LauncherGame *games, int count, int sort_order);

/* ─── cover worker public API (used by launcher.cpp) ─────────────────────── */
void  launcher_cover_worker_start(void);
void  launcher_cover_worker_stop(void);
void  launcher_cover_request(const LauncherGame *game);
/* returns scaled Imlib_Image or NULL; caller must NOT free it (cache owns it) */
Imlib_Image launcher_cover_get(const char *path);
Imlib_Image launcher_cover_get_ex(const char *path, uint32_t *fade_out); /* single lookup for both */
int   launcher_cover_flush(int limit);  /* call once per frame; returns covers processed */
uint32_t    launcher_cover_fade_alpha(const char *path); /* 0-255 */

/* ─── description fetch public API ────────────────────────────────────────── */
#define LAUNCHER_DESC_IDLE    0
#define LAUNCHER_DESC_LOADING 1
#define LAUNCHER_DESC_READY   2
#define LAUNCHER_DESC_NODATA  3

void        launcher_desc_request(const LauncherGame *game, const char *base_dir);
int         launcher_desc_state(void);
const char *launcher_desc_text(void);
const char *launcher_desc_error(void);

/* play time tracking API */
void launcher_state_apply_play_time(LauncherState *st, const char *state_path);

#endif /* LAUNCHER_H */
