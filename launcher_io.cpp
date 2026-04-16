/*
 * launcher_io.cpp
 * Data loading, state persistence, ROM download, MGL launch, cover cache.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <glob.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include "launcher.h"
#include "launcher_draw.h"
#include "lib/imlib2/Imlib2.h"
#include "cfg.h"
#include "file_io.h"
#include "str_util.h"

/* ─── core map ───────────────────────────────────────────────────────────── */

const CoreMapEntry launcher_core_map[] = {
    { "SNES",          "_Console",  "SNES",          0 },
    { "NES",           "_Console",  "NES",           0 },
    { "GBA",           "_Console",  "GBA",           0 },
    { "GB",            "_Console",  "Gameboy",       0 },
    { "GBC",           "_Console",  "Gameboy",       0 },
    { "Genesis",       "_Console",  "Genesis",       0 },
    { "MegaDrive",     "_Console",  "Genesis",       0 },
    { "Sega32X",       "_Console",  "S32X",          0 },
    { "MasterSystem",  "_Console",  "SMS",           0 },
    { "GameGear",      "_Console",  "SMS",           0 },
    { "PCEngine",      "_Console",  "TurboGrafx16",  0 },
    { "TurboGrafx16",  "_Console",  "TurboGrafx16",  0 },
    { "NeoGeo",        "_Console",  "NeoGeo",        1 },
    { "Arcade",        "_Arcade",   NULL,            0 },  /* uses MRA */
    { "Atari2600",     "_Console",  "Atari7800",     0 },
    { "Atari7800",     "_Console",  "Atari7800",     0 },
    { "AtariLynx",     "_Console",  "AtariLynx",     0 },
    { "ColecoVision",  "_Console",  "ColecoVision",  0 },
    { "Intellivision", "_Console",  "Intellivision", 0 },
    { "PSX",           "_Console",  "PSX",           1 },
    { "N64",           "_Console",  "N64",           0 },
    { "C64",           "_Computer", "C64",           0 },
    { "AmigaOCS",      "_Computer", "Minimig",       0 },
};
const int launcher_core_map_count = (int)(sizeof(launcher_core_map) / sizeof(launcher_core_map[0]));

const CoreMapEntry *launcher_find_core(const char *system)
{
    for (int i = 0; i < launcher_core_map_count; i++)
        if (strcasecmp(launcher_core_map[i].system, system) == 0)
            return &launcher_core_map[i];
    return NULL;
}

/* ─── CSV parsing ────────────────────────────────────────────────────────── */

static int cmp_game_name(const void *a, const void *b)
{
    return strcasecmp(((const LauncherGame*)a)->name,
                      ((const LauncherGame*)b)->name);
}

static int cmp_game_name_desc(const void *a, const void *b)
{
    return -strcasecmp(((const LauncherGame*)a)->name,
                       ((const LauncherGame*)b)->name);
}

static int cmp_game_recent(const void *a, const void *b)
{
    uint32_t ta = ((const LauncherGame*)a)->last_played;
    uint32_t tb = ((const LauncherGame*)b)->last_played;
    if (ta != tb) return (ta > tb) ? -1 : 1;
    uint16_t pa = ((const LauncherGame*)a)->play_count;
    uint16_t pb = ((const LauncherGame*)b)->play_count;
    return (pa > pb) ? -1 : (pa < pb) ? 1 : 0;
}

static int cmp_game_most_played(const void *a, const void *b)
{
    uint16_t pa = ((const LauncherGame*)a)->play_count;
    uint16_t pb = ((const LauncherGame*)b)->play_count;
    if (pa != pb) return (pa > pb) ? -1 : 1;
    uint32_t ta = ((const LauncherGame*)a)->last_played;
    uint32_t tb = ((const LauncherGame*)b)->last_played;
    return (ta > tb) ? -1 : (ta < tb) ? 1 : 0;
}

static int cmp_game_rated(const void *a, const void *b)
{
    uint8_t ra = ((const LauncherGame*)a)->user_rating;
    uint8_t rb = ((const LauncherGame*)b)->user_rating;
    if (ra != rb) return (ra > rb) ? -1 : 1;
    /* tiebreak: more played first */
    uint16_t pa = ((const LauncherGame*)a)->play_count;
    uint16_t pb = ((const LauncherGame*)b)->play_count;
    return (pa > pb) ? -1 : (pa < pb) ? 1 : 0;
}

/* Public sort function for game lists */
void launcher_sort_games(LauncherGame *games, int count, int sort_order)
{
    if (count <= 0) return;

    int (*cmp_func)(const void*, const void*) = cmp_game_name;  /* default */
    switch (sort_order) {
        case SORT_NAME_DESC:    cmp_func = cmp_game_name_desc; break;
        case SORT_RECENT:       cmp_func = cmp_game_recent; break;
        case SORT_MOST_PLAYED:  cmp_func = cmp_game_most_played; break;
        case SORT_HIGHEST_RATED: cmp_func = cmp_game_rated; break;
        case SORT_NAME_ASC:
        default:                cmp_func = cmp_game_name; break;
    }
    qsort(games, count, sizeof(LauncherGame), cmp_func);
}

/* Coarse progress of the library loader, read by draw_splash. */
static volatile int g_load_done  = 0;
static volatile int g_load_total = 0;

void launcher_load_progress(int *done_out, int *total_out)
{
    if (done_out)  *done_out  = g_load_done;
    if (total_out) *total_out = g_load_total;
}

/* Setter used by the new scanner-driven loader. Same-thread writes; no lock. */
void launcher_load_progress_set(int done, int total)
{
    g_load_done  = done;
    g_load_total = total;
}

void launcher_free_library(LauncherSystem *systems, int count)
{
    for (int i = 0; i < count; i++)
        free(systems[i].games);
    free(systems);
}

/* ─── state persistence ──────────────────────────────────────────────────── */

bool launcher_load_state(const char *path, LauncherState *st)
{
    memset(st, 0, sizeof(*st));

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[1024];
    int section = 0; /* 0=none, 1=favs, 2=history, 3=positions */

    while (fgets(line, sizeof(line), fp)) {
        int ln = (int)strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r'))
            line[--ln] = '\0';
        if (!ln) continue;

        if (strcmp(line, "[favourites]") == 0) { section = 1; continue; }
        if (strcmp(line, "[history]") == 0)    { section = 2; continue; }
        if (strcmp(line, "[positions]") == 0)  { section = 3; continue; }

        if (section == 1 && st->fav_count < LAUNCHER_MAX_FAVS) {
            /* format: SYSTEM=path */
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                FavEntry *f = &st->favs[st->fav_count++];
                strncpy(f->system, line, sizeof(f->system) - 1);
                strncpy(f->path,   eq+1, sizeof(f->path)   - 1);
            }
        } else if (section == 2 && st->history_count < LAUNCHER_MAX_HISTORY) {
            /* format: ts|system|path|name|cover_path|count[|play_time][|user_rating] */
            char *p = line;
            char *fields[8]; int fi = 0;
            fields[fi++] = p;
            while (*p && fi < 8) {
                if (*p == '|') { *p = '\0'; fields[fi++] = p + 1; }
                p++;
            }
            if (fi >= 6) {
                HistoryEntry *h = &st->history[st->history_count++];
                h->ts    = (uint32_t)atol(fields[0]);
                strncpy(h->system,     fields[1], sizeof(h->system) - 1);
                strncpy(h->path,       fields[2], sizeof(h->path)   - 1);
                strncpy(h->name,       fields[3], sizeof(h->name)   - 1);
                strncpy(h->cover_path, fields[4], sizeof(h->cover_path) - 1);
                h->count     = atoi(fields[5]);
                h->play_time = (fi >= 7) ? (uint32_t)atol(fields[6]) : 0;  /* backward compat */
                h->user_rating = (fi >= 8) ? (uint8_t)atoi(fields[7]) : 0;  /* backward compat */
            }
        } else if (section == 3) {
            /* format: SYS_INDEX=selected|scroll */
            char *eq = strchr(line, '=');
            if (eq) {
                int sys_idx = atoi(line);
                if (sys_idx >= 0 && sys_idx < LAUNCHER_MAX_SYSTEMS) {
                    char *pipe = strchr(eq + 1, '|');
                    if (pipe) {
                        *pipe = '\0';
                        st->per_system[sys_idx].selected_game = atoi(eq + 1);
                        st->per_system[sys_idx].scroll_offset = atoi(pipe + 1);
                    }
                }
            }
        }
    }
    fclose(fp);
    return true;
}

bool launcher_save_state(const char *path, const LauncherState *st)
{
    FILE *fp = fopen(path, "w");
    if (!fp) { perror("launcher: save state"); return false; }

    fprintf(fp, "[favourites]\n");
    for (int i = 0; i < st->fav_count; i++)
        fprintf(fp, "%s=%s\n", st->favs[i].system, st->favs[i].path);

    fprintf(fp, "[history]\n");
    for (int i = 0; i < st->history_count; i++) {
        const HistoryEntry *h = &st->history[i];
        fprintf(fp, "%u|%s|%s|%s|%s|%d|%u|%u\n",
                h->ts, h->system, h->path, h->name, h->cover_path, h->count, h->play_time, (unsigned)h->user_rating);
    }

    fprintf(fp, "[positions]\n");
    for (int i = 0; i < LAUNCHER_MAX_SYSTEMS; i++) {
        if (st->per_system[i].selected_game > 0 || st->per_system[i].scroll_offset > 0)
            fprintf(fp, "%d=%d|%d\n", i, st->per_system[i].selected_game, st->per_system[i].scroll_offset);
    }

    fclose(fp);
    return true;
}

void launcher_state_record_played(LauncherState *st, const LauncherGame *g,
                                   const char *state_path)
{
    /* update or insert into history */
    uint32_t now = (uint32_t)time(NULL);
    for (int i = 0; i < st->history_count; i++) {
        if (strcmp(st->history[i].path, g->path) == 0 &&
            strcmp(st->history[i].system, g->system) == 0) {
            st->history[i].ts = now;
            st->history[i].count++;
            /* move to front */
            HistoryEntry tmp = st->history[i];
            memmove(&st->history[1], &st->history[0], i * sizeof(HistoryEntry));
            st->history[0] = tmp;
            launcher_save_state(state_path, st);
            return;
        }
    }
    /* new entry */
    if (st->history_count == LAUNCHER_MAX_HISTORY)
        st->history_count--;
    memmove(&st->history[1], &st->history[0],
            st->history_count * sizeof(HistoryEntry));
    HistoryEntry *h = &st->history[0];
    h->ts    = now;
    h->count = 1;
    strncpy(h->system,     g->system,     sizeof(h->system) - 1);
    strncpy(h->path,       g->path,       sizeof(h->path)   - 1);
    strncpy(h->name,       g->name,       sizeof(h->name)   - 1);
    strncpy(h->cover_path, g->cover_path, sizeof(h->cover_path) - 1);
    st->history_count++;
    launcher_save_state(state_path, st);
}

void launcher_state_apply_play_time(LauncherState *st, const char *state_path)
{
    /* check if /tmp/launcher_play_start exists (from previous game launch) */
    FILE *fp = fopen("/tmp/launcher_play_start", "r");
    if (!fp) return;

    uint32_t start_ts = 0;
    fscanf(fp, "%u", &start_ts);
    fclose(fp);
    remove("/tmp/launcher_play_start");

    if (!start_ts || !st->history_count) return;

    /* calculate duration in seconds, cap at 12 hours for safety */
    uint32_t now = (uint32_t)time(NULL);
    uint32_t dur = (now > start_ts) ? (now - start_ts) : 0;
    const uint32_t MAX_DURATION = 12 * 3600;
    if (dur > MAX_DURATION) dur = MAX_DURATION;
    if (dur == 0) return;

    /* add to the most recent history entry (index 0) */
    st->history[0].play_time += dur;
    launcher_save_state(state_path, st);
}

bool launcher_state_is_favourite(const LauncherState *st, const LauncherGame *g)
{
    for (int i = 0; i < st->fav_count; i++)
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0)
            return true;
    return false;
}

void launcher_state_toggle_fav(LauncherState *st, const LauncherGame *g,
                                const char *state_path)
{
    for (int i = 0; i < st->fav_count; i++) {
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0) {
            /* remove */
            memmove(&st->favs[i], &st->favs[i+1],
                    (st->fav_count - i - 1) * sizeof(FavEntry));
            st->fav_count--;
            launcher_save_state(state_path, st);
            return;
        }
    }
    if (st->fav_count < LAUNCHER_MAX_FAVS) {
        FavEntry *f = &st->favs[st->fav_count++];
        strncpy(f->system, g->system, sizeof(f->system) - 1);
        strncpy(f->path,   g->path,   sizeof(f->path)   - 1);
        launcher_save_state(state_path, st);
    }
}

/* ─── virtual system builder ─────────────────────────────────────────────── */

/* Build virtual Recent and Favourites systems, prepend to real systems.
   Returns new array (caller frees virtual game arrays, NOT the real ones). */
LauncherSystem *launcher_build_all_systems(
    const LauncherSystem *real, int real_count,
    const LauncherState  *st,
    int *total_out)
{
    int extra = 0;
    if (st->history_count > 0) extra++;
    if (st->fav_count > 0) extra++;

    int total = extra + real_count;
    LauncherSystem *all = (LauncherSystem*)calloc(total, sizeof(LauncherSystem));
    int out = 0;

    /* Recent */
    if (st->history_count > 0) {
        LauncherSystem *sys = &all[out++];
        strncpy(sys->name, "Recent", sizeof(sys->name) - 1);
        sys->is_virtual = 1;
        sys->game_count = st->history_count;
        sys->games = (LauncherGame*)malloc(st->history_count * sizeof(LauncherGame));
        for (int i = 0; i < st->history_count; i++) {
            LauncherGame *g = &sys->games[i];
            strncpy(g->name,       st->history[i].name,       sizeof(g->name) - 1);
            strncpy(g->path,       st->history[i].path,       sizeof(g->path) - 1);
            strncpy(g->cover_path, st->history[i].cover_path, sizeof(g->cover_path) - 1);
            strncpy(g->system,     st->history[i].system,     sizeof(g->system) - 1);
        }
    }

    /* Favourites */
    if (st->fav_count > 0) {
        LauncherSystem *sys = &all[out++];
        strncpy(sys->name, "Favourites", sizeof(sys->name) - 1);
        sys->is_virtual = 1;
        /* collect matching games from real systems */
        int cap = st->fav_count, cnt = 0;
        LauncherGame *fgames = (LauncherGame*)malloc(cap * sizeof(LauncherGame));
        for (int f = 0; f < st->fav_count; f++) {
            for (int s = 0; s < real_count; s++) {
                for (int gi = 0; gi < real[s].game_count; gi++) {
                    if (strcmp(real[s].games[gi].path, st->favs[f].path) == 0) {
                        fgames[cnt++] = real[s].games[gi];
                        goto next_fav;
                    }
                }
            }
            next_fav:;
        }
        qsort(fgames, cnt, sizeof(LauncherGame), [](const void *a, const void *b) {
            return strcasecmp(((const LauncherGame*)a)->name,
                              ((const LauncherGame*)b)->name);
        });
        sys->games      = fgames;
        sys->game_count = cnt;
    }

    /* real systems */
    for (int i = 0; i < real_count; i++)
        all[out++] = real[i];

    *total_out = total;
    return all;
}

void launcher_free_virtual_systems(LauncherSystem *all, int total,
                                    int real_count)
{
    int extra = total - real_count;
    for (int i = 0; i < extra; i++)
        free(all[i].games); /* free virtual game arrays */
    free(all);              /* free the outer array */
}


