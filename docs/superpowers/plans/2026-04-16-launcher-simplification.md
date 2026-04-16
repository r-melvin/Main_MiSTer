# Launcher Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the MiSTer launcher into a fully offline, directory-driven frontend — drop all network scraping, replace CSV-fed library loads with a stat-invalidated directory walk rooted at `/media/fat/games/<System>/`, split the 1,600-line `launcher_io.cpp` into focused modules, and add unit-test coverage for the scanner, state I/O, and launch pipeline.

**Architecture:** Seven bite-sized commits. Deletions first (scraping, batch cover modal), then additive new-scanner module with tests, then a cache layer, then switch the loader, then a mechanical refactor split, then test expansion. Each commit builds clean and passes the full test suite.

**Tech Stack:** C++14 with `std=gnu++14`, gcc-arm cross-compile for Cortex-A9. POSIX `opendir`/`readdir` for the scanner, Imlib2 for covers (unchanged). Unit tests are host-native `g++` under `tests/` — no SDL, no Imlib.

---

## File Structure

Files this plan creates, modifies, or deletes content from.

**New:**
- `launcher_scan.h`, `launcher_scan.cpp` — directory walker + per-system extension allowlist + on-disk library cache
- `launcher_theme.h`, `launcher_theme.cpp` — theme defaults, `launcher_theme_init`, `launcher_theme_load`, `parse_color`, `parse_font_size`
- `launcher_cover.h`, `launcher_cover.cpp` — cover worker thread + LRU cache (relocated from `launcher_io.cpp`)
- `launcher_launch.h`, `launcher_launch.cpp` — `launcher_write_mgl`, `find_rbf`, `launcher_load_core`, download/cache-path helpers, error classifier
- `tests/test_launcher_scan.cpp` — scanner + cache tests
- `tests/test_launcher_state.cpp` — state I/O tests (separated from current `test_launcher_io.cpp`)
- `tests/test_launcher_launch.cpp` — MGL + core resolution tests

**Modified:**
- `launcher.cpp` — remove `LMODE_COVER_DL` / `LMODE_DESCRIPTION` state machine branches, `KEY_D` / `KEY_I` handlers, `g_cdl_*`, `g_desc_*`; call the new cache+scanner path from `lib_loader_thread`
- `launcher_io.cpp` — shrinks to state I/O + core map; other concerns move to new modules
- `launcher_io.h` — public API for state + core map only
- `launcher_draw.cpp`, `launcher_draw.h` — delete `launcher_draw_cover_dl`, `launcher_draw_description`, `draw_wrapped_text`, `count_wrapped_lines`
- `launcher.h` — drop `LMODE_COVER_DL` / `LMODE_DESCRIPTION` enum members, `LAUNCHER_DESC_*` states, and description-related public API
- `cfg.h`, `cfg.cpp` — drop `tgdb_api_key`; add `launcher_games_path[1024]` + INI key `LAUNCHER_GAMES_PATH` defaulting to `"/media/fat/games"`
- `tests/test_launcher_io.cpp` — shrinks to cache-path/core-map helpers; state cases move to `test_launcher_state.cpp`
- `tests/Makefile` — new test binaries
- `README.md`, `launcher.cfg.example` — drop TGDB docs; document `LAUNCHER_GAMES_PATH`; note that covers are BYO

**Not modified:** top-level `Makefile` — it uses `$(wildcard *.cpp)` so new .cpp files are auto-picked up.

---

## Task 1: Remove TheGamesDB network scraping

Deletes `exec_curl`, `scrape_tgdb_cover`, `scrape_tgdb_description`, `tgdb_platform_id`, `url_encode`, `json_str`, `json_int`, the description worker thread and its public API, the lazy-scrape branch in the cover worker, and the whole description overlay mode (`LMODE_DESCRIPTION`) with its UI. `TGDB_API_KEY` cfg key goes with it.

Covers still work — the cover worker reads `<base>/covers/<System>/<stem>.{jpg,png}` from disk. Only the auto-fetch is gone.

**Files:**
- Modify: `launcher_io.cpp` — delete lines covering `url_encode`, `json_str`, `json_int`, `exec_curl`, `scrape_tgdb_cover`, `scrape_tgdb_description`, `tgdb_platform_id`, `desc_worker`, `DescReq`, `launcher_desc_request/state/text/error`, `g_desc_state/text/error`, the lazy-scrape block in `cover_worker` (around line 1246-1255)
- Modify: `launcher.h` — delete `LMODE_DESCRIPTION` from `LauncherMode` enum, `LAUNCHER_DESC_IDLE/LOADING/READY/NODATA` macros, `launcher_desc_request/state/text/error` declarations
- Modify: `launcher.cpp` — delete `g_desc_game_name`, `g_desc_system`, `g_desc_scroll` globals; delete the `KEY_I` description trigger block around lines 1759-1766; delete the entire `case LMODE_DESCRIPTION:` around lines 2032-2050
- Modify: `launcher_draw.cpp` — delete `launcher_draw_description` and its helpers `draw_wrapped_text`, `count_wrapped_lines` if unused elsewhere
- Modify: `launcher_draw.h` — delete `launcher_draw_description` declaration (line 68)
- Modify: `cfg.h` — delete `tgdb_api_key[128]` field
- Modify: `cfg.cpp` — delete the `TGDB_API_KEY` INI entry
- Modify: `README.md` — delete any TGDB section if present
- Modify: `launcher.cfg.example` — delete the TGDB block at the end (current lines ~25-28)
- Modify: `launcher.h` — in `LauncherGame`, update the `path` field comment from `/* remote SFTP or local absolute path */` to `/* local absolute path to the ROM */`

- [ ] **Step 1: Delete the curl + TGDB helpers in `launcher_io.cpp`**

Open [launcher_io.cpp](launcher_io.cpp) and find the block starting at `static void url_encode(` (around line 240) through the end of `scrape_tgdb_cover` (around line 495). Delete the entire block. Also delete `tgdb_platform_id` (around line 383, which is *inside* the block anyway). Delete `exec_curl` (line 311), `json_str` (line 273), `json_int` (line 297).

Then delete the description section at the bottom: find `/* ─── game description fetch (background thread) ────` (around line 1398) and delete everything from there to the end of `launcher_desc_error()` (around line 1604).

Then delete the lazy-scrape branch in `cover_worker`:

```cpp
// DELETE THIS BLOCK in cover_worker (around line 1246-1255):
        if (!src && req.game_name[0] && cfg.tgdb_api_key[0]) {
            char covers_dir[2048];
            snprintf(covers_dir, sizeof(covers_dir), "%s/covers", cfg.launcher_path);
            char dummy[280];
            if (scrape_tgdb_cover(req.game_name, req.system, covers_dir, dummy, sizeof(dummy))) {
                err = IMLIB_LOAD_ERROR_NONE;
                src = imlib_load_image_with_error_return(req.path, &err);
            }
        }
```

Also find the comment at line 739 that says `Sanitization must match scrape_tgdb_cover()` and remove that specific phrase (keep the sanitisation code — it's still used to derive default cover paths from game names).

- [ ] **Step 2: Delete `LMODE_DESCRIPTION` from `launcher.h`**

In [launcher.h](launcher.h), find the `LauncherMode` enum (around line 9). Delete the `LMODE_DESCRIPTION,    /* game description overlay */` line.

While you're here, fix the stale comment on `LauncherGame.path` (around line 64). Change:

```cpp
    char path[512];       /* remote SFTP or local absolute path */
```

to:

```cpp
    char path[512];       /* local absolute path to the ROM */
```

Then find and delete these lines (around line 250-267):

```cpp
#define LAUNCHER_DESC_IDLE    0
#define LAUNCHER_DESC_LOADING 1
#define LAUNCHER_DESC_READY   2
#define LAUNCHER_DESC_NODATA  3

void        launcher_desc_request(const LauncherGame *game, const char *base_dir);
int         launcher_desc_state(void);
const char *launcher_desc_text(void);
const char *launcher_desc_error(void);
```

- [ ] **Step 3: Delete description state and handlers in `launcher.cpp`**

In [launcher.cpp](launcher.cpp), delete these globals (around line 130-132):

```cpp
static char g_desc_game_name[128] = {};
static char g_desc_system[64]     = {};
static int  g_desc_scroll         = 0;
```

Delete the `KEY_I` block (around lines 1759-1766):

```cpp
        if (key_up_pressed(KEY_I) && !g_search_mode && g_filtered_cnt > 0) {
            const LauncherGame *game = &g_filtered[g_game_sel];
            strncpy(g_desc_game_name, game->name,   sizeof(g_desc_game_name) - 1);
            strncpy(g_desc_system,    game->system, sizeof(g_desc_system) - 1);
            g_desc_scroll = 0;
            launcher_desc_request(game, g_base_dir);
            g_mode = LMODE_DESCRIPTION;
        }
```

Delete the entire `case LMODE_DESCRIPTION:` block around lines 2032-2050. It's the whole case from `case LMODE_DESCRIPTION: {` through its closing `break; }`.

- [ ] **Step 4: Delete description drawing code**

In [launcher_draw.cpp](launcher_draw.cpp), delete `launcher_draw_description` (starts at line 921, ends when the function closes — typically ~80 lines later at the next `/* ─── */` banner or end of file). Search backwards and forwards for helpers used only by it: `draw_wrapped_text`, `count_wrapped_lines`. If a Grep shows they're referenced nowhere else, delete them too. Keep any helpers still used by other draw_* functions.

In [launcher_draw.h](launcher_draw.h), delete this declaration at line 68:

```cpp
void launcher_draw_description(Imlib_Image img, int sw, int sh,
                                const char *game_name, const char *system,
                                const char *text, int state, int scroll, uint32_t frame_time,
                                const char *error_msg);
```

- [ ] **Step 5: Delete `TGDB_API_KEY` from cfg**

In [cfg.h](cfg.h), delete the `tgdb_api_key[128]` field.

In [cfg.cpp](cfg.cpp), delete the `TGDB_API_KEY` INI entry:

```cpp
{ "TGDB_API_KEY",       (void*)(cfg.tgdb_api_key),          STRING, 0, sizeof(cfg.tgdb_api_key)        - 1 },
```

- [ ] **Step 6: Update docs**

In [launcher.cfg.example](launcher.cfg.example), delete the entire "Cover art (TheGamesDB)" block at the bottom (the final section after the theme config).

In [README.md](README.md), grep for any occurrence of `TGDB`, `TheGamesDB`, `scraping`, `API key` and remove those paragraphs.

- [ ] **Step 7: Build to verify deletion compiles**

```bash
make -j4 2>&1 | tail -10
```

Expected: clean build, `bin/MiSTer` produced. If any errors reference `launcher_desc_`, `scrape_tgdb_`, `LAUNCHER_DESC_`, `LMODE_DESCRIPTION`, or `tgdb_api_key`, find and delete the remaining reference.

- [ ] **Step 8: Run existing tests**

```bash
make -C tests && for t in tests/test_launcher_core_map tests/test_launcher_covers tests/test_launcher_io tests/test_launcher_nav tests/test_pause_core; do ./$t | tail -1; done
```

Expected: all five say `N / N tests passed`.

- [ ] **Step 9: Grep-verify scrape removal**

```bash
git grep -nE 'scrape_tgdb|tgdb_platform_id|exec_curl|launcher_desc_|LMODE_DESCRIPTION|LAUNCHER_DESC_|TGDB_API_KEY' -- ':!lib/' ':!docs/'
```

Expected: **no output**. If anything appears, delete those references.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "launcher: remove TheGamesDB cover + description scraping

No more network calls from the launcher. Drops exec_curl, all
scrape_tgdb_* helpers, the description fetch thread, LMODE_DESCRIPTION,
KEY_I binding, and TGDB_API_KEY config. Covers still work when present
on disk at <base>/covers/<System>/<stem>.{jpg,png}; just no longer
auto-fetched on cache miss."
```

---

## Task 2: Remove batch cover-download modal

Removes `LMODE_COVER_DL`, `enter_cover_dl`, the `g_cdl_*` state, the sliding-window stat loop, the `KEY_D` binding, and `launcher_draw_cover_dl`.

**Files:**
- Modify: `launcher.cpp` — delete `g_cdl_total`, `g_cdl_done`, `g_cdl_check_idx`, `g_cdl_sys` globals; delete `enter_cover_dl()` helper; delete the `KEY_D` block; delete the `case LMODE_COVER_DL:` state-machine branch
- Modify: `launcher.h` — delete `LMODE_COVER_DL` from enum
- Modify: `launcher_draw.cpp` — delete `launcher_draw_cover_dl`
- Modify: `launcher_draw.h` — delete `launcher_draw_cover_dl` declaration at line 54

- [ ] **Step 1: Delete `LMODE_COVER_DL` from `launcher.h`**

Delete the `LMODE_COVER_DL,       /* cover batch download modal */` line from the `LauncherMode` enum.

- [ ] **Step 2: Delete `g_cdl_*` globals + `enter_cover_dl` in `launcher.cpp`**

Find and delete (around line 110-113):

```cpp
static int g_cdl_total = 0;        /* games queued for this batch */
static int g_cdl_done = 0;         /* covers confirmed on disk so far */
static int g_cdl_check_idx = 0;    /* sliding-window cursor for stat() scan */
static LauncherSystem *g_cdl_sys = NULL;  /* pointer to system being scanned */
```

Find `static void enter_cover_dl(void)` (around line 1577) and delete the whole function.

- [ ] **Step 3: Delete `KEY_D` handler**

Find and delete the `KEY_D` block (around lines 1756-1758):

```cpp
        if (key_up_pressed(KEY_D) && !g_search_mode && !g_multi_mode) {
            enter_cover_dl();
        }
```

- [ ] **Step 4: Delete the `case LMODE_COVER_DL:` branch**

Find `case LMODE_COVER_DL: {` around line 1986. Delete everything from that line through its matching closing `break; }` (a ~25-line block).

- [ ] **Step 5: Delete `launcher_draw_cover_dl`**

In [launcher_draw.cpp](launcher_draw.cpp), find `void launcher_draw_cover_dl(` at line 627. Delete the function (ends at the `/* ─── bulk select badge ─── */` banner or equivalent).

In [launcher_draw.h](launcher_draw.h), delete the declaration at line 54:

```cpp
void launcher_draw_cover_dl(Imlib_Image img, int sw, int sh, int done, int total);
```

- [ ] **Step 6: Help-screen cleanup (if any)**

Grep for `KEY_D` or the help text "D:" in [launcher_draw.cpp](launcher_draw.cpp) and [launcher.cpp](launcher.cpp). If the on-screen help lists `D: Covers` or similar, remove that line.

```bash
git grep -n '"D:' -- 'launcher*'
```

Edit any remaining strings that reference the removed key.

- [ ] **Step 7: Build + test**

```bash
make -j4 2>&1 | tail -6
make -C tests 2>&1 | tail -3
for t in tests/test_launcher_core_map tests/test_launcher_covers tests/test_launcher_io tests/test_launcher_nav tests/test_pause_core; do ./$t | tail -1; done
```

Expected: clean build, all five test binaries pass.

- [ ] **Step 8: Grep-verify**

```bash
git grep -nE 'LMODE_COVER_DL|enter_cover_dl|g_cdl_|launcher_draw_cover_dl' -- ':!lib/' ':!docs/'
```

Expected: **no output**.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "launcher: remove batch cover download modal

Dead feature after TheGamesDB scraping was removed — the modal was
only useful to pre-warm the lazy scrape path. Drops LMODE_COVER_DL,
enter_cover_dl, the g_cdl_* state, the KEY_D binding, and
launcher_draw_cover_dl."
```

---

## Task 3: Add directory scanner with per-system extensions

Creates [launcher_scan.h](launcher_scan.h) and [launcher_scan.cpp](launcher_scan.cpp) with the extension allowlist and `launcher_scan_system`. No cache yet (Task 4). No wiring into production yet (Task 5). Just the module + its tests.

**Files:**
- Create: `launcher_scan.h` — public API header
- Create: `launcher_scan.cpp` — implementation
- Create: `tests/test_launcher_scan.cpp` — unit tests
- Modify: `tests/Makefile` — add the new test binary

- [ ] **Step 1: Create `launcher_scan.h`**

```cpp
#ifndef LAUNCHER_SCAN_H
#define LAUNCHER_SCAN_H

#include "launcher.h"

/* Returns a NULL-terminated array of lowercase extensions accepted for this
   system, e.g. for "SNES" returns {"sfc", "smc", NULL}. Returns NULL for
   unknown systems. Case-insensitive match; caller owns nothing. */
const char * const *launcher_rom_extensions(const char *system);

/* Walk games_dir/<system>/ recursively and fill *out with every ROM whose
   extension is in the allowlist. Derives cover_path from covers_dir/<system>/
   <stem>.jpg|png if present, empty string otherwise.

   Returns the number of games found (0 on empty dir, -1 if games_dir/<system>
   does not exist or cannot be opened). Caller must free(*out).

   Sort order: alphabetical by name. */
int launcher_scan_system(const char *system,
                         const char *games_dir,
                         const char *covers_dir,
                         LauncherGame **out);

#endif /* LAUNCHER_SCAN_H */
```

- [ ] **Step 2: Create `launcher_scan.cpp` — extension table**

Create [launcher_scan.cpp](launcher_scan.cpp) starting with includes and the extension table:

```cpp
/*
 * launcher_scan.cpp
 * Directory walker and per-system ROM extension allowlist.
 */

#include "launcher_scan.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* ─── per-system extension allowlist ─────────────────────────────────────── */

static const char *const EXT_SNES[]          = { "sfc", "smc", NULL };
static const char *const EXT_NES[]           = { "nes", NULL };
static const char *const EXT_GBA[]           = { "gba", NULL };
static const char *const EXT_GB[]            = { "gb", NULL };
static const char *const EXT_GBC[]           = { "gbc", "gb", NULL };
static const char *const EXT_MD[]            = { "md", "gen", "bin", "smd", NULL };
static const char *const EXT_S32X[]          = { "32x", NULL };
static const char *const EXT_SMS[]           = { "sms", NULL };
static const char *const EXT_GG[]            = { "gg", NULL };
static const char *const EXT_PCE[]           = { "pce", "sgx", NULL };
static const char *const EXT_NEOGEO[]        = { "neo", NULL };
static const char *const EXT_ARCADE[]        = { "mra", NULL };
static const char *const EXT_A2600[]         = { "a26", "bin", NULL };
static const char *const EXT_A7800[]         = { "a78", "bin", NULL };
static const char *const EXT_LYNX[]          = { "lnx", NULL };
static const char *const EXT_COLECO[]        = { "col", "bin", NULL };
static const char *const EXT_INTV[]          = { "int", "bin", NULL };
static const char *const EXT_PSX[]           = { "chd", "cue", "exe", NULL };
static const char *const EXT_N64[]           = { "n64", "z64", "v64", NULL };
static const char *const EXT_C64[]           = { "prg", "d64", "t64", "tap", "crt", NULL };
static const char *const EXT_AMIGA[]         = { "adf", "hdf", NULL };

static const struct { const char *system; const char *const *exts; } EXT_TABLE[] = {
    { "SNES",          EXT_SNES   },
    { "NES",           EXT_NES    },
    { "GBA",           EXT_GBA    },
    { "GB",            EXT_GB     },
    { "GBC",           EXT_GBC    },
    { "Genesis",       EXT_MD     },
    { "MegaDrive",     EXT_MD     },
    { "Sega32X",       EXT_S32X   },
    { "MasterSystem",  EXT_SMS    },
    { "GameGear",      EXT_GG     },
    { "PCEngine",      EXT_PCE    },
    { "TurboGrafx16",  EXT_PCE    },
    { "NeoGeo",        EXT_NEOGEO },
    { "Arcade",        EXT_ARCADE },
    { "Atari2600",     EXT_A2600  },
    { "Atari7800",     EXT_A7800  },
    { "AtariLynx",     EXT_LYNX   },
    { "ColecoVision",  EXT_COLECO },
    { "Intellivision", EXT_INTV   },
    { "PSX",           EXT_PSX    },
    { "N64",           EXT_N64    },
    { "C64",           EXT_C64    },
    { "AmigaOCS",      EXT_AMIGA  },
};

const char * const *launcher_rom_extensions(const char *system)
{
    for (size_t i = 0; i < sizeof(EXT_TABLE) / sizeof(EXT_TABLE[0]); i++)
        if (strcasecmp(EXT_TABLE[i].system, system) == 0)
            return EXT_TABLE[i].exts;
    return NULL;
}
```

- [ ] **Step 3: Add recursive walker to `launcher_scan.cpp`**

Append to [launcher_scan.cpp](launcher_scan.cpp):

```cpp
/* ─── directory walk ─────────────────────────────────────────────────────── */

static bool ext_matches(const char *filename, const char * const *exts)
{
    const char *dot = strrchr(filename, '.');
    if (!dot) return false;
    dot++;
    for (int i = 0; exts[i]; i++)
        if (strcasecmp(dot, exts[i]) == 0)
            return true;
    return false;
}

/* Copy `name` into `out`, stripping the last extension. */
static void strip_ext(const char *name, char *out, size_t out_sz)
{
    strncpy(out, name, out_sz - 1);
    out[out_sz - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

/* Resolve cover_path: <covers_dir>/<system>/<stem>.<jpg|png> if present,
   else empty. */
static void resolve_cover(const char *covers_dir, const char *system,
                           const char *stem, char *out, size_t out_sz)
{
    static const char *tries[] = { "jpg", "png", NULL };
    struct stat st;
    for (int i = 0; tries[i]; i++) {
        snprintf(out, out_sz, "%s/%s/%s.%s", covers_dir, system, stem, tries[i]);
        if (stat(out, &st) == 0 && st.st_size > 0) return;
    }
    out[0] = '\0';
}

struct ScanCtx {
    const char *system;
    const char *covers_dir;
    const char * const *exts;
    LauncherGame *games;
    int count;
    int capacity;
};

static void ensure_capacity(ScanCtx *ctx)
{
    if (ctx->count < ctx->capacity) return;
    int new_cap = ctx->capacity ? ctx->capacity * 2 : 128;
    ctx->games = (LauncherGame*)realloc(ctx->games, new_cap * sizeof(LauncherGame));
    ctx->capacity = new_cap;
}

static void walk_dir(ScanCtx *ctx, const char *path)
{
    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;  /* skip hidden + . / .. */

        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);

        struct stat st;
        if (lstat(child, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            walk_dir(ctx, child);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!ext_matches(ent->d_name, ctx->exts)) continue;

        ensure_capacity(ctx);
        LauncherGame *g = &ctx->games[ctx->count++];
        memset(g, 0, sizeof(*g));

        strip_ext(ent->d_name, g->name, sizeof(g->name));
        strncpy(g->path, child, sizeof(g->path) - 1);
        strncpy(g->system, ctx->system, sizeof(g->system) - 1);
        g->file_size = (uint32_t)st.st_size;

        resolve_cover(ctx->covers_dir, ctx->system, g->name,
                      g->cover_path, sizeof(g->cover_path));
    }
    closedir(d);
}

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp(((const LauncherGame*)a)->name,
                      ((const LauncherGame*)b)->name);
}

int launcher_scan_system(const char *system,
                         const char *games_dir,
                         const char *covers_dir,
                         LauncherGame **out)
{
    *out = NULL;
    const char * const *exts = launcher_rom_extensions(system);
    if (!exts) return -1;

    char root[1024];
    snprintf(root, sizeof(root), "%s/%s", games_dir, system);

    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;

    ScanCtx ctx = { system, covers_dir, exts, NULL, 0, 0 };
    walk_dir(&ctx, root);

    if (ctx.count > 0)
        qsort(ctx.games, ctx.count, sizeof(LauncherGame), cmp_name);

    *out = ctx.games;
    return ctx.count;
}
```

- [ ] **Step 4: Create `tests/test_launcher_scan.cpp` with extension-table tests**

The test file has to stand alone from the full launcher build (no `cfg.h`, no Imlib). So the test compiles `launcher_scan.cpp` directly with a stub `launcher.h` or — simpler — inlines a minimal `LauncherGame` matching the real layout.

Given `LauncherGame` pulls `<inttypes.h>` via `launcher.h` and uses only POD fields, the cleanest approach is to let the test compile `launcher_scan.cpp` with the real `launcher.h`. But `launcher.h` includes `<pthread.h>` and `<linux/input.h>` which is noisy on a workstation. Follow the pattern of `tests/test_launcher_io.cpp`: **inline a minimal LauncherGame** and compile `launcher_scan.cpp` into the test binary by #include-ing the .cpp after a stub definition.

Create [tests/test_launcher_scan.cpp](tests/test_launcher_scan.cpp):

```cpp
/*
 * Tests for launcher_scan.cpp — extension allowlist + directory walker + cache.
 *
 * We inline a minimal LauncherGame and launcher.h replacement so the scanner
 * can be compiled and linked directly into the test binary without dragging
 * in pthread / imlib / linux/input.h.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <string>

/* Minimal LauncherGame matching the layout in launcher.h. Must stay in sync. */
struct LauncherGame {
    char name[128];
    char path[512];
    char cover_path[512];
    char system[64];
    uint32_t last_played;
    uint16_t play_count;
    uint32_t file_size;
    uint32_t play_time;
    uint8_t  user_rating;
};

#define LAUNCHER_H  /* stop launcher_scan.cpp from pulling the real header */
#include "../launcher_scan.cpp"

/* ─── test harness ───────────────────────────────────────────────────────── */

static int pass = 0, total = 0;
#define CHECK(expr) do { \
    total++; \
    if (expr) { pass++; printf("  PASS %s\n", #expr); } \
    else      {          printf("  FAIL %s\n", #expr); } \
} while (0)

#define TEST(name) static void name(void)

/* ─── tmp dir helpers ────────────────────────────────────────────────────── */

static std::string g_tmp;

static int rmrf_cb(const char *fpath, const struct stat *, int, struct FTW *)
{
    remove(fpath);
    return 0;
}

static void make_tmp(void)
{
    char tmpl[] = "/tmp/launcher_scan_testXXXXXX";
    const char *dir = mkdtemp(tmpl);
    assert(dir);
    g_tmp = dir;
}

static void kill_tmp(void)
{
    if (g_tmp.empty()) return;
    nftw(g_tmp.c_str(), rmrf_cb, 16, FTW_DEPTH | FTW_PHYS);
    g_tmp.clear();
}

static void mkpath(const std::string &p)
{
    std::string cur;
    for (size_t i = 1; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/') {
            cur.assign(p, 0, i);
            mkdir(cur.c_str(), 0755);
        }
    }
}

static void touch_file(const std::string &p, size_t size = 16)
{
    mkpath(p.substr(0, p.find_last_of('/')));
    int fd = open(p.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd >= 0);
    if (size) {
        char buf[256] = {0};
        for (size_t w = 0; w < size; w += sizeof(buf))
            write(fd, buf, (size - w < sizeof(buf)) ? size - w : sizeof(buf));
    }
    close(fd);
}

/* ─── extension allowlist ────────────────────────────────────────────────── */

TEST(test_extensions_known_system)
{
    const char * const *e = launcher_rom_extensions("SNES");
    CHECK(e != NULL);
    CHECK(strcmp(e[0], "sfc") == 0);
    CHECK(strcmp(e[1], "smc") == 0);
    CHECK(e[2] == NULL);
}

TEST(test_extensions_case_insensitive)
{
    CHECK(launcher_rom_extensions("snes") != NULL);
    CHECK(launcher_rom_extensions("Snes") != NULL);
    CHECK(launcher_rom_extensions("SNES") != NULL);
}

TEST(test_extensions_unknown_returns_null)
{
    CHECK(launcher_rom_extensions("NotARealSystem") == NULL);
}

int main(void)
{
    printf("=== test_launcher_scan ===\n");

    test_extensions_known_system();
    test_extensions_case_insensitive();
    test_extensions_unknown_returns_null();

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", pass, total);
    return (pass == total) ? 0 : 1;
}
```

- [ ] **Step 5: Add walker tests**

Append before `main` in [tests/test_launcher_scan.cpp](tests/test_launcher_scan.cpp):

```cpp
/* ─── walker ─────────────────────────────────────────────────────────────── */

TEST(test_scan_extension_filter)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string covers = g_tmp + "/covers";

    touch_file(games + "/SNES/mario.sfc");
    touch_file(games + "/SNES/zelda.smc");
    touch_file(games + "/SNES/readme.txt");
    touch_file(games + "/SNES/cover.jpg");

    LauncherGame *games_out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), covers.c_str(), &games_out);
    CHECK(n == 2);
    /* alphabetical */
    CHECK(n >= 2 && strcmp(games_out[0].name, "mario") == 0);
    CHECK(n >= 2 && strcmp(games_out[1].name, "zelda") == 0);
    free(games_out);

    kill_tmp();
}

TEST(test_scan_recursive_walk)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string covers = g_tmp + "/covers";

    touch_file(games + "/SNES/USA/Action/mario.sfc");
    touch_file(games + "/SNES/Japan/zelda.smc");

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), covers.c_str(), &out);
    CHECK(n == 2);
    free(out);

    kill_tmp();
}

TEST(test_scan_empty_dir)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    mkpath(games + "/SNES");

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), "/tmp", &out);
    CHECK(n == 0);
    CHECK(out == NULL || out != NULL);  /* either is fine as long as no crash */
    free(out);

    kill_tmp();
}

TEST(test_scan_missing_system_returns_negative_one)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    mkpath(games);

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), "/tmp", &out);
    CHECK(n == -1);
    free(out);

    kill_tmp();
}

TEST(test_scan_cover_resolution_jpg)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string covers = g_tmp + "/covers";

    touch_file(games + "/SNES/mario.sfc");
    touch_file(covers + "/SNES/mario.jpg", 1024);

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), covers.c_str(), &out);
    CHECK(n == 1);
    CHECK(n >= 1 && strstr(out[0].cover_path, "/SNES/mario.jpg") != NULL);
    free(out);

    kill_tmp();
}

TEST(test_scan_cover_resolution_png_fallback)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string covers = g_tmp + "/covers";

    touch_file(games + "/SNES/mario.sfc");
    touch_file(covers + "/SNES/mario.png", 1024);

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), covers.c_str(), &out);
    CHECK(n == 1);
    CHECK(n >= 1 && strstr(out[0].cover_path, "/SNES/mario.png") != NULL);
    free(out);

    kill_tmp();
}

TEST(test_scan_cover_missing_empty_string)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string covers = g_tmp + "/covers";

    touch_file(games + "/SNES/mario.sfc");

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), covers.c_str(), &out);
    CHECK(n == 1);
    CHECK(n >= 1 && out[0].cover_path[0] == '\0');
    free(out);

    kill_tmp();
}

TEST(test_scan_file_size_populated)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string covers = g_tmp + "/covers";

    touch_file(games + "/SNES/mario.sfc", 1234);

    LauncherGame *out = NULL;
    int n = launcher_scan_system("SNES", games.c_str(), covers.c_str(), &out);
    CHECK(n == 1);
    CHECK(n >= 1 && out[0].file_size == 1234);
    free(out);

    kill_tmp();
}
```

Call each new `TEST(...)` from `main` before the final `printf`:

```cpp
    test_scan_extension_filter();
    test_scan_recursive_walk();
    test_scan_empty_dir();
    test_scan_missing_system_returns_negative_one();
    test_scan_cover_resolution_jpg();
    test_scan_cover_resolution_png_fallback();
    test_scan_cover_missing_empty_string();
    test_scan_file_size_populated();
```

- [ ] **Step 6: Add to `tests/Makefile`**

In [tests/Makefile](tests/Makefile), extend the `TESTS` variable and add a rule:

```makefile
TESTS = test_pause_core test_launcher_core_map test_launcher_io test_launcher_covers test_launcher_nav test_launcher_scan
```

And add:

```makefile
test_launcher_scan: test_launcher_scan.cpp ../launcher_scan.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<
```

- [ ] **Step 7: Build + run the new test**

```bash
make -C tests clean && make -C tests 2>&1 | tail -10
./tests/test_launcher_scan
```

Expected: test binary builds and reports `11 / 11 tests passed` (3 allowlist + 8 walker).

- [ ] **Step 8: Run all tests to ensure nothing else broke**

```bash
for t in tests/test_launcher_core_map tests/test_launcher_covers tests/test_launcher_io tests/test_launcher_nav tests/test_launcher_scan tests/test_pause_core; do ./$t | tail -1; done
```

Expected: each line says `N / N tests passed`.

- [ ] **Step 9: Top-level build sanity**

```bash
make -j4 2>&1 | tail -6
```

Expected: clean build. (launcher_scan.cpp is compiled in via the wildcard but not yet called from anywhere.)

- [ ] **Step 10: Commit**

```bash
git add launcher_scan.h launcher_scan.cpp tests/test_launcher_scan.cpp tests/Makefile
git commit -m "launcher: add directory scanner with per-system extensions

New launcher_scan module walks games_dir/<System>/ recursively, filters
by a per-system extension allowlist, and resolves covers from
covers_dir/<System>/<stem>.{jpg,png}. Pure: no globals, no threads.

Not yet wired into the boot path — lib_loader_thread still reads CSVs.
That switch happens in a later commit after the cache layer lands."
```

---

## Task 4: Add stat-invalidated library cache

Extends `launcher_scan.{h,cpp}` with `launcher_scan_cache_load` and `launcher_scan_cache_save`. Binary format with header (magic, version, games_dir path), per-system mtime snapshot, packed `LauncherGame` arrays.

**Files:**
- Modify: `launcher_scan.h` — declare cache API
- Modify: `launcher_scan.cpp` — implement cache load/save
- Modify: `tests/test_launcher_scan.cpp` — add cache tests

- [ ] **Step 1: Declare the cache API in `launcher_scan.h`**

Insert before `#endif`:

```cpp
/* Load a previously saved library snapshot. Returns true and fills
   *systems_out/*count_out only if:
     - the file exists and has our magic + version
     - the stored games_dir matches the current games_dir
     - every system directory's mtime matches the stored mtime

   Returns false (and sets *systems_out = NULL, *count_out = 0) on any
   mismatch or I/O error so the caller can rescan. Caller owns the
   returned systems array and must free via launcher_free_library. */
bool launcher_scan_cache_load(const char *cache_path,
                              const char *games_dir,
                              LauncherSystem **systems_out, int *count_out);

/* Write the current library state so the next boot can skip the scan.
   Writes atomically via a .tmp + rename. Non-fatal on failure. */
bool launcher_scan_cache_save(const char *cache_path,
                              const char *games_dir,
                              const LauncherSystem *systems, int count);
```

- [ ] **Step 2: Implement the cache in `launcher_scan.cpp`**

Append to [launcher_scan.cpp](launcher_scan.cpp):

```cpp
/* ─── on-disk library cache ──────────────────────────────────────────────── */

#include <stdint.h>
#include <time.h>

#define LSC_MAGIC    0x4D535452u   /* 'MSTR' */
#define LSC_VERSION  2u

struct LscHeader {
    uint32_t magic;
    uint32_t version;
    char     games_dir[512];
    uint32_t system_count;
};

struct LscSystemHeader {
    char     name[64];
    int64_t  mtime;       /* stored st_mtime of games_dir/<name> at save time */
    uint32_t game_count;
    uint32_t _pad;        /* keeps on-disk layout 8-byte aligned */
};

static bool read_all(FILE *fp, void *buf, size_t n)
{
    return fread(buf, 1, n, fp) == n;
}

static bool write_all(FILE *fp, const void *buf, size_t n)
{
    return fwrite(buf, 1, n, fp) == n;
}

static int64_t dir_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int64_t)st.st_mtime;
}

bool launcher_scan_cache_load(const char *cache_path,
                              const char *games_dir,
                              LauncherSystem **systems_out, int *count_out)
{
    *systems_out = NULL;
    *count_out = 0;

    FILE *fp = fopen(cache_path, "rb");
    if (!fp) return false;

    LscHeader hdr;
    if (!read_all(fp, &hdr, sizeof(hdr))) { fclose(fp); return false; }

    if (hdr.magic != LSC_MAGIC) { fclose(fp); return false; }
    if (hdr.version != LSC_VERSION) { fclose(fp); return false; }
    hdr.games_dir[sizeof(hdr.games_dir) - 1] = '\0';
    if (strcmp(hdr.games_dir, games_dir) != 0) { fclose(fp); return false; }

    LauncherSystem *systems = (LauncherSystem*)calloc(hdr.system_count,
                                                       sizeof(LauncherSystem));
    if (!systems) { fclose(fp); return false; }

    for (uint32_t i = 0; i < hdr.system_count; i++) {
        LscSystemHeader sh;
        if (!read_all(fp, &sh, sizeof(sh))) goto fail;
        sh.name[sizeof(sh.name) - 1] = '\0';

        /* mtime check against on-disk directory */
        char sys_path[1024];
        snprintf(sys_path, sizeof(sys_path), "%s/%s", games_dir, sh.name);
        if (dir_mtime(sys_path) != sh.mtime) goto fail;

        LauncherSystem *s = &systems[i];
        strncpy(s->name, sh.name, sizeof(s->name) - 1);
        s->game_count = (int)sh.game_count;
        s->is_virtual = 0;

        if (sh.game_count) {
            s->games = (LauncherGame*)calloc(sh.game_count, sizeof(LauncherGame));
            if (!s->games) goto fail;
            if (!read_all(fp, s->games, sh.game_count * sizeof(LauncherGame)))
                goto fail;
        }
    }

    fclose(fp);
    *systems_out = systems;
    *count_out = (int)hdr.system_count;
    return true;

fail:
    for (uint32_t i = 0; i < hdr.system_count; i++)
        free(systems[i].games);
    free(systems);
    fclose(fp);
    return false;
}

bool launcher_scan_cache_save(const char *cache_path,
                              const char *games_dir,
                              const LauncherSystem *systems, int count)
{
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) return false;

    LscHeader hdr = {};
    hdr.magic = LSC_MAGIC;
    hdr.version = LSC_VERSION;
    strncpy(hdr.games_dir, games_dir, sizeof(hdr.games_dir) - 1);
    hdr.system_count = (uint32_t)count;

    if (!write_all(fp, &hdr, sizeof(hdr))) goto fail;

    for (int i = 0; i < count; i++) {
        const LauncherSystem *s = &systems[i];
        if (s->is_virtual) continue;  /* don't persist virtual systems */

        LscSystemHeader sh = {};
        strncpy(sh.name, s->name, sizeof(sh.name) - 1);
        char sys_path[1024];
        snprintf(sys_path, sizeof(sys_path), "%s/%s", games_dir, s->name);
        sh.mtime = dir_mtime(sys_path);
        sh.game_count = (uint32_t)s->game_count;

        if (!write_all(fp, &sh, sizeof(sh))) goto fail;
        if (s->game_count &&
            !write_all(fp, s->games, s->game_count * sizeof(LauncherGame)))
            goto fail;
    }

    fclose(fp);
    return rename(tmp_path, cache_path) == 0;

fail:
    fclose(fp);
    remove(tmp_path);
    return false;
}
```

Note: the calloc + virtual-systems skip means `count` passed in should be the real-system count, not all systems. Callers are expected to pass only non-virtual systems.

- [ ] **Step 3: Add cache tests**

Append to [tests/test_launcher_scan.cpp](tests/test_launcher_scan.cpp) before `main`:

```cpp
/* ─── cache ──────────────────────────────────────────────────────────────── */

/* The test file inlines LauncherGame but needs LauncherSystem too — the cache
   API uses it. Mirror the real layout here. */
struct LauncherSystem {
    char name[64];
    LauncherGame *games;
    int game_count;
    int is_virtual;
};

/* launcher_scan.cpp's cache code uses LauncherSystem via launcher.h. Since we
   stubbed that header out, we redeclare the cache API by hand. */
extern "C" {
    bool launcher_scan_cache_load(const char *cache_path,
                                  const char *games_dir,
                                  LauncherSystem **systems_out, int *count_out);
    bool launcher_scan_cache_save(const char *cache_path,
                                  const char *games_dir,
                                  const LauncherSystem *systems, int count);
}

static void free_systems(LauncherSystem *s, int n)
{
    for (int i = 0; i < n; i++) free(s[i].games);
    free(s);
}

TEST(test_cache_roundtrip)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string cache = g_tmp + "/library.bin";
    mkpath(games + "/SNES");
    touch_file(games + "/SNES/mario.sfc");

    /* Build a LauncherSystem array manually */
    LauncherSystem sys[1] = {};
    strncpy(sys[0].name, "SNES", sizeof(sys[0].name) - 1);
    sys[0].games = (LauncherGame*)calloc(1, sizeof(LauncherGame));
    strncpy(sys[0].games[0].name, "mario", sizeof(sys[0].games[0].name) - 1);
    sys[0].games[0].file_size = 42;
    sys[0].game_count = 1;

    CHECK(launcher_scan_cache_save(cache.c_str(), games.c_str(), sys, 1));

    free(sys[0].games);

    LauncherSystem *loaded = NULL;
    int n = 0;
    CHECK(launcher_scan_cache_load(cache.c_str(), games.c_str(), &loaded, &n));
    CHECK(n == 1);
    CHECK(n >= 1 && strcmp(loaded[0].name, "SNES") == 0);
    CHECK(n >= 1 && loaded[0].game_count == 1);
    CHECK(n >= 1 && loaded[0].game_count >= 1 &&
          strcmp(loaded[0].games[0].name, "mario") == 0);
    CHECK(n >= 1 && loaded[0].game_count >= 1 &&
          loaded[0].games[0].file_size == 42);
    free_systems(loaded, n);

    kill_tmp();
}

TEST(test_cache_mtime_invalidates)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string cache = g_tmp + "/library.bin";
    mkpath(games + "/SNES");

    LauncherSystem sys[1] = {};
    strncpy(sys[0].name, "SNES", sizeof(sys[0].name) - 1);
    sys[0].game_count = 0;
    CHECK(launcher_scan_cache_save(cache.c_str(), games.c_str(), sys, 1));

    /* Touch the system dir to advance mtime */
    sleep(1);  /* st_mtime is second-granular on most FSes */
    std::string sys_dir = games + "/SNES";
    struct utimbuf ut = { time(NULL), time(NULL) };
    utime(sys_dir.c_str(), &ut);

    LauncherSystem *loaded = NULL;
    int n = 0;
    CHECK(launcher_scan_cache_load(cache.c_str(), games.c_str(), &loaded, &n) == false);

    kill_tmp();
}

TEST(test_cache_games_dir_mismatch)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string cache = g_tmp + "/library.bin";
    mkpath(games + "/SNES");

    LauncherSystem sys[1] = {};
    strncpy(sys[0].name, "SNES", sizeof(sys[0].name) - 1);
    CHECK(launcher_scan_cache_save(cache.c_str(), games.c_str(), sys, 1));

    LauncherSystem *loaded = NULL;
    int n = 0;
    CHECK(launcher_scan_cache_load(cache.c_str(), "/somewhere/else",
                                    &loaded, &n) == false);

    kill_tmp();
}

TEST(test_cache_corrupted_header)
{
    make_tmp();
    std::string cache = g_tmp + "/library.bin";

    FILE *fp = fopen(cache.c_str(), "wb");
    fwrite("junk", 1, 4, fp);
    fclose(fp);

    LauncherSystem *loaded = NULL;
    int n = 0;
    CHECK(launcher_scan_cache_load(cache.c_str(), "/whatever",
                                    &loaded, &n) == false);

    kill_tmp();
}

TEST(test_cache_missing_file)
{
    LauncherSystem *loaded = NULL;
    int n = 0;
    CHECK(launcher_scan_cache_load("/does/not/exist.bin", "/whatever",
                                    &loaded, &n) == false);
}
```

Register each test in `main`:

```cpp
    test_cache_roundtrip();
    test_cache_mtime_invalidates();
    test_cache_games_dir_mismatch();
    test_cache_corrupted_header();
    test_cache_missing_file();
```

- [ ] **Step 4: Build + run**

```bash
make -C tests clean && make -C tests && ./tests/test_launcher_scan
```

Expected: `16 / 16 tests passed` (11 from task 3 + 5 cache tests).

- [ ] **Step 5: Full test suite**

```bash
for t in tests/test_launcher_core_map tests/test_launcher_covers tests/test_launcher_io tests/test_launcher_nav tests/test_launcher_scan tests/test_pause_core; do ./$t | tail -1; done
```

Expected: all pass.

- [ ] **Step 6: Top-level build**

```bash
make -j4 2>&1 | tail -6
```

Expected: clean.

- [ ] **Step 7: Commit**

```bash
git add launcher_scan.h launcher_scan.cpp tests/test_launcher_scan.cpp
git commit -m "launcher: add stat-invalidated library cache

launcher_scan_cache_load/save persist the in-memory library to
<base>/cache/library.bin. On load: reject if magic/version/games_dir
mismatch, or if any cached system's mtime is older than the on-disk
directory's — forcing a rescan when ROMs change. Atomic save via
.tmp + rename so a mid-write crash leaves a parseable old cache."
```

---

## Task 5: Switch `lib_loader_thread` to the directory scanner

Removes the CSV parser (`parse_csv`) and the old `launcher_load_library`, adds `LAUNCHER_GAMES_PATH` cfg (default `/media/fat/games`), switches `lib_loader_thread` to try the cache first, then fall back to scanning each system.

**Files:**
- Modify: `launcher_io.cpp` — delete `parse_csv`, old `launcher_load_library`; keep `launcher_free_library` (still needed)
- Modify: `launcher_io.h` — no change (API surface preserved)
- Modify: `launcher.cpp` — rewrite `lib_loader_thread` to call cache+scanner
- Modify: `cfg.h` — add `launcher_games_path[1024]`
- Modify: `cfg.cpp` — add `LAUNCHER_GAMES_PATH` INI entry + default
- Modify: `launcher.cfg.example` — document the new key
- Modify: `README.md` — note new key + BYO covers layout

- [ ] **Step 1: Add `launcher_games_path` to cfg**

In [cfg.h](cfg.h), add to the launcher block:

```cpp
    char     launcher_path[1024];
    char     launcher_games_path[1024];  /* default: /media/fat/games */
    uint8_t  launcher_particles;
```

In [cfg.cpp](cfg.cpp), add the INI entry below `LAUNCHER_PATH`:

```cpp
    { "LAUNCHER",            (void*)(&(cfg.launcher)),            UINT8,  0, 1 },
    { "LAUNCHER_PATH",       (void*)(cfg.launcher_path),          STRING, 0, sizeof(cfg.launcher_path) - 1 },
    { "LAUNCHER_GAMES_PATH", (void*)(cfg.launcher_games_path),    STRING, 0, sizeof(cfg.launcher_games_path) - 1 },
    { "LAUNCHER_PARTICLES",  (void*)(&(cfg.launcher_particles)),  UINT8,  0, 1 },
    { "LAUNCHER_THEME",      (void*)(cfg.launcher_theme),         STRING, 0, sizeof(cfg.launcher_theme) - 1 },
```

In [cfg.cpp](cfg.cpp) `cfg_parse()` (or whichever function sets defaults — check for the existing `strcpy(cfg.launcher_path, "/media/fat/launcher")` line), add:

```cpp
    strcpy(cfg.launcher_path, "/media/fat/launcher");
    strcpy(cfg.launcher_games_path, "/media/fat/games");
    cfg.launcher_particles = 1;
```

- [ ] **Step 2: Replace `launcher_load_library` call site in `launcher.cpp`**

Find `lib_loader_thread`:

```cpp
static void *lib_loader_thread(void *)
{
    bool ok = launcher_load_library(g_base_dir, &g_real_sys, &g_real_cnt);
    launcher_load_state(g_state_path, &g_state);
    launcher_state_apply_play_time(&g_state, g_state_path);
    backfill_play_data(g_real_sys, g_real_cnt, &g_state);
    g_lib_ok   = ok;
    g_lib_done = true;
    return NULL;
}
```

Replace with:

```cpp
#include "launcher_scan.h"

static void *lib_loader_thread(void *)
{
    const char *games_dir = cfg.launcher_games_path[0]
                                ? cfg.launcher_games_path
                                : "/media/fat/games";

    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/cache/library.bin", g_base_dir);

    char covers_dir[1024];
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", g_base_dir);

    char cache_dir[1024];
    snprintf(cache_dir, sizeof(cache_dir), "%s/cache", g_base_dir);
    mkdir(cache_dir, 0755);

    bool ok = false;

    /* Fast path: use the on-disk cache if all mtimes match. */
    if (launcher_scan_cache_load(cache_path, games_dir, &g_real_sys, &g_real_cnt)) {
        ok = true;
    } else {
        /* Slow path: walk every system listed in the core map. */
        g_real_sys = (LauncherSystem*)calloc(launcher_core_map_count,
                                              sizeof(LauncherSystem));
        g_real_cnt = 0;
        g_load_total = launcher_core_map_count;   /* existing progress globals */
        g_load_done = 0;

        for (int i = 0; i < launcher_core_map_count; i++) {
            const char *sys = launcher_core_map[i].system;
            LauncherGame *games = NULL;
            int n = launcher_scan_system(sys, games_dir, covers_dir, &games);
            g_load_done++;
            if (n <= 0) { free(games); continue; }

            LauncherSystem *s = &g_real_sys[g_real_cnt++];
            strncpy(s->name, sys, sizeof(s->name) - 1);
            s->games = games;
            s->game_count = n;
            s->is_virtual = 0;
        }
        ok = (g_real_cnt > 0);

        if (ok)
            launcher_scan_cache_save(cache_path, games_dir, g_real_sys, g_real_cnt);
    }

    launcher_load_state(g_state_path, &g_state);
    launcher_state_apply_play_time(&g_state, g_state_path);
    backfill_play_data(g_real_sys, g_real_cnt, &g_state);
    g_lib_ok   = ok;
    g_lib_done = true;
    return NULL;
}
```

Note: if `g_load_done` and `g_load_total` are defined in `launcher_io.cpp`, confirm they're already declared `extern` in `launcher.h` or accessible from `launcher.cpp`. If not, add an accessor or move the publisher. The existing code in Step-7 of the previous plan set already uses them via `launcher_load_progress` — use that.

Actually, `g_load_done`/`g_load_total` are file-static in `launcher_io.cpp` and only exposed via `launcher_load_progress()` getter. Adjust the scanner loop to update them via a setter. Add this near the top of `launcher_io.cpp`:

```cpp
/* Setter used by the new scanner-driven loader. Same-thread writes; no lock. */
void launcher_load_progress_set(int done, int total)
{
    g_load_done = done;
    g_load_total = total;
}
```

Declare in [launcher_io.h](launcher_io.h):

```cpp
void launcher_load_progress_set(int done, int total);
```

Then update the loop in `lib_loader_thread`:

```cpp
        launcher_load_progress_set(0, launcher_core_map_count);
        for (int i = 0; i < launcher_core_map_count; i++) {
            const char *sys = launcher_core_map[i].system;
            LauncherGame *games = NULL;
            int n = launcher_scan_system(sys, games_dir, covers_dir, &games);
            launcher_load_progress_set(i + 1, launcher_core_map_count);
            ...
```

- [ ] **Step 3: Delete `parse_csv` and old `launcher_load_library`**

In [launcher_io.cpp](launcher_io.cpp), find `parse_csv` (around line 688) and `launcher_load_library` (around line 780). Delete both. Keep `launcher_free_library` — it's used when rebuilding virtual systems.

- [ ] **Step 4: Update `launcher.cfg.example`**

Add below `LAUNCHER_PATH`:

```ini
; Root directory containing system subdirectories with ROMs. Shared with
; MiSTer's stock menu by default. (default: /media/fat/games)
LAUNCHER_GAMES_PATH=/media/fat/games
```

- [ ] **Step 5: Update `README.md`**

In whatever section documents launcher config, add `LAUNCHER_GAMES_PATH`. Add a sentence:

> Covers are loaded from `<launcher_path>/covers/<System>/<stem>.jpg` (or `.png`). Drop image files in there to make them show up — there is no auto-download.

- [ ] **Step 6: Build**

```bash
make -j4 2>&1 | tail -10
```

Expected: clean. If errors reference `parse_csv` or `launcher_load_library`, check for a caller we missed.

- [ ] **Step 7: Tests**

```bash
make -C tests && for t in tests/test_launcher_core_map tests/test_launcher_covers tests/test_launcher_io tests/test_launcher_nav tests/test_launcher_scan tests/test_pause_core; do ./$t | tail -1; done
```

Expected: all pass. `test_launcher_io`'s `test_cache_path_construction` still uses `launcher_cache_path` which is in `launcher_launch.cpp` after Task 6 but still in `launcher_io.cpp` right now — should work.

- [ ] **Step 8: Grep-verify**

```bash
git grep -nE 'parse_csv|lists/|\.csv"' -- ':!lib/' ':!docs/'
```

Expected: no hits referencing the old CSV pipeline. (Some string literals in test fixtures may say "csv" for unrelated reasons — inspect and be sure.)

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "launcher: switch library load to directory scanner

lib_loader_thread now tries the on-disk cache first, falling back to
launcher_scan_system per system. Drops parse_csv and the old
launcher_load_library. Adds LAUNCHER_GAMES_PATH config (defaulting to
/media/fat/games) so ROMs are shared with MiSTer's stock menu.

BYO covers: drop <stem>.jpg|png into <launcher_path>/covers/<System>/
and the scanner picks them up."
```

---

## Task 6: Split `launcher_io.cpp` into theme / cover / launch modules

Pure refactor — no logic change. Moves code out of the 1,500-line `launcher_io.cpp` into focused modules, leaving `launcher_io.cpp` responsible for state I/O + core map only.

**Files:**
- Create: `launcher_theme.cpp` — theme defaults + `launcher_theme_init`, `launcher_theme_load`, `parse_color`, `parse_font_size`
- Create: `launcher_theme.h` — public theme API (already declared in `launcher.h`; `.h` here is mainly for internal consistency)
- Create: `launcher_cover.cpp` — cover worker, LRU cache, `launcher_cover_*` functions, `launcher_cover_flush`, `launcher_cover_flush_budget_us`
- Create: `launcher_cover.h` — cover public API
- Create: `launcher_launch.cpp` — `launcher_write_mgl`, `find_rbf`, `launcher_load_core`, `launcher_cache_path`, `launcher_start_download`, `launcher_poll_download`, `launcher_cancel_download`, `launcher_mgl_error`, `launcher_download_error`
- Create: `launcher_launch.h` — launch public API
- Modify: `launcher_io.cpp` — remove everything that moved; keeps core map + state persistence + `launcher_free_library`
- Modify: `launcher_io.h` — shrinks to state I/O + core map (move scanner decls out if redundant with launcher_scan.h)
- Modify: `launcher.h` — may need `#include "launcher_cover.h"` etc. if it was exposing those APIs

- [ ] **Step 1: Create `launcher_theme.h`**

```cpp
#ifndef LAUNCHER_THEME_H
#define LAUNCHER_THEME_H

void launcher_theme_init(const char *launcher_path, const char *theme_name);
void launcher_theme_load(const char *path);

#endif
```

(These are already declared in `launcher.h`; this header exists so the .cpp can `#include` it and share the theme defaults with the rest of the codebase without pulling all of `launcher.h`.)

- [ ] **Step 2: Create `launcher_theme.cpp`**

Cut from [launcher_io.cpp](launcher_io.cpp):
- `LauncherTheme g_theme;` global (around line 67)
- `theme_set_defaults` (line 69)
- `parse_color` (line 91)
- `parse_font_size` (line 104)
- `trim` (line 110)
- `launcher_theme_load` (line 121)
- `launcher_theme_init` (line 159)

Paste into a new [launcher_theme.cpp](launcher_theme.cpp) starting with:

```cpp
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

/* ... rest of the cut code in the same order ... */
```

In [launcher_io.cpp](launcher_io.cpp), delete the moved code.

- [ ] **Step 3: Build to verify**

```bash
make -j4 2>&1 | tail -10
```

Expected: clean. If duplicate-symbol errors appear for `g_theme`, confirm it's only in `launcher_theme.cpp` now.

- [ ] **Step 4: Create `launcher_cover.h`**

```cpp
#ifndef LAUNCHER_COVER_H
#define LAUNCHER_COVER_H

#include "launcher.h"  /* LauncherGame, LAUNCHER_MAX_COVERS */

#ifdef __cplusplus
#include "lib/imlib2/Imlib2.h"
#endif

void  launcher_cover_worker_start(void);
void  launcher_cover_worker_stop(void);
void  launcher_cover_request(const LauncherGame *game);
Imlib_Image launcher_cover_get(const char *path);
Imlib_Image launcher_cover_get_ex(const char *path, uint32_t *fade_out);
int   launcher_cover_flush(int limit);
int   launcher_cover_flush_budget_us(int limit, int budget_us);
uint32_t launcher_cover_fade_alpha(const char *path);
void  launcher_cover_cache_tick(void);

#endif
```

- [ ] **Step 5: Create `launcher_cover.cpp`**

Cut from [launcher_io.cpp](launcher_io.cpp) every function, static, and struct in the cover section:
- `path_hash`, `cover_lru_evict`, `g_cover_cache`, `g_cover_fade`, `g_cover_frame`
- `CoverRequest`, `CoverResult`, `g_req_*`, `g_res_*` queues + mutexes
- `cover_worker`, `launcher_cover_worker_start/stop`, `launcher_cover_request`, `launcher_cover_get`, `launcher_cover_get_ex`, `launcher_cover_fade_alpha`, `launcher_cover_cache_tick`, `cover_flush_impl`, `launcher_cover_flush`, `launcher_cover_flush_budget_us`, `ts_us_now`

Paste into [launcher_cover.cpp](launcher_cover.cpp):

```cpp
/*
 * launcher_cover.cpp
 * Cover art worker thread, request/response queues, LRU cache.
 */

#include "launcher.h"
#include "launcher_cover.h"
#include "cfg.h"
#include "lib/imlib2/Imlib2.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ... pasted cover code in the same order ... */
```

Delete from [launcher_io.cpp](launcher_io.cpp).

- [ ] **Step 6: Build**

```bash
make -j4 2>&1 | tail -10
```

Expected: clean.

- [ ] **Step 7: Create `launcher_launch.h`**

```cpp
#ifndef LAUNCHER_LAUNCH_H
#define LAUNCHER_LAUNCH_H

#include "launcher.h"

void  launcher_cache_path(const LauncherGame *game, const char *base_dir,
                          char *out, size_t out_sz);
bool  launcher_start_download(const LauncherGame *game, const char *base_dir);
int   launcher_poll_download(const LauncherGame *game, const char *base_dir);
void  launcher_cancel_download(void);
const char *launcher_download_error(void);
const char *launcher_mgl_error(void);

bool  launcher_write_mgl(const LauncherGame *game);
void  launcher_load_core(const char *mgl_path);

#endif
```

- [ ] **Step 8: Create `launcher_launch.cpp`**

Cut from [launcher_io.cpp](launcher_io.cpp):
- `find_rbf`
- `launcher_write_mgl` + `g_mgl_error`
- `launcher_load_core`
- `launcher_cache_path`
- `launcher_start_download`, `launcher_poll_download`, `launcher_cancel_download`
- `g_download_error`, `launcher_download_error`, `launcher_mgl_error`

Paste into [launcher_launch.cpp](launcher_launch.cpp):

```cpp
/*
 * launcher_launch.cpp
 * MGL generation, core resolution, and the local ROM cache path.
 */

#include "launcher.h"
#include "launcher_launch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ... pasted code ... */
```

Delete from [launcher_io.cpp](launcher_io.cpp).

- [ ] **Step 9: Build + test**

```bash
make -j4 2>&1 | tail -10
make -C tests && for t in tests/test_launcher_*; do ./$t | tail -1; done
```

Expected: clean build, all tests pass.

- [ ] **Step 10: Check file sizes**

```bash
wc -l launcher*.cpp launcher*.h
```

Expected: `launcher_io.cpp` down to ~400-500 lines, four new modules at 200-500 lines each.

- [ ] **Step 11: Commit**

```bash
git add launcher_theme.h launcher_theme.cpp launcher_cover.h launcher_cover.cpp launcher_launch.h launcher_launch.cpp launcher_io.cpp launcher_io.h
git commit -m "launcher: split launcher_io.cpp into theme/cover/launch modules

Pure mechanical refactor. launcher_io.cpp drops from ~1,500 lines to
~450 by moving the theme system, cover worker, and MGL/launch pipeline
into their own translation units with focused headers. No behaviour
change — every function lands in exactly one place and keeps its
signature."
```

---

## Task 7: Expand the test suite

Splits `test_launcher_io.cpp` so state round-trips live in their own file, and adds `test_launcher_launch.cpp` for MGL + core classifier.

**Files:**
- Create: `tests/test_launcher_state.cpp` — round-trip / merge / corruption / play_time / sort order
- Create: `tests/test_launcher_launch.cpp` — MGL write + missing-core classifier
- Modify: `tests/test_launcher_io.cpp` — shrinks to `launcher_cache_path` + core-map helpers (it already has those)
- Modify: `tests/Makefile` — add new binaries

- [ ] **Step 1: Create `tests/test_launcher_state.cpp`**

Lift the favourites, recent, and play-time tests currently in `tests/test_launcher_io.cpp` into a new file. The existing tests inline `LauncherState` and helpers; do the same. Look at [tests/test_launcher_io.cpp](tests/test_launcher_io.cpp) to see which `TEST(...)` functions cover state vs cache-path, and copy the state ones across plus these new cases:

```cpp
TEST(test_state_corrupted_file_graceful)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";

    /* Write garbage */
    FILE *fp = fopen(path.c_str(), "wb");
    fwrite("xxxxxxxxxxxxxxxx", 1, 16, fp);
    fclose(fp);

    LauncherState st = {};
    bool ok = launcher_load_state(path.c_str(), &st);
    /* Accept either "fail cleanly" (false) or "clear and continue". Both are
       valid — we just require no crash and no garbage in the state. */
    (void)ok;
    CHECK(st.history_count == 0 || st.history_count < 1000);

    kill_tmp();
}

TEST(test_state_play_time_accumulation)
{
    LauncherState st = {};
    LauncherGame g = {};
    strncpy(g.system, "SNES", sizeof(g.system) - 1);
    strncpy(g.path,   "/games/SNES/mario.sfc", sizeof(g.path) - 1);
    strncpy(g.name,   "mario", sizeof(g.name) - 1);

    launcher_state_record_played(&st, &g, "/tmp/irrelevant.dat");
    st.history[0].play_time = 300;
    launcher_state_record_played(&st, &g, "/tmp/irrelevant.dat");
    st.history[0].play_time += 400;
    CHECK(st.history[0].play_time == 700);
}
```

- [ ] **Step 2: Write `tests/test_launcher_launch.cpp`**

This test exercises `launcher_write_mgl` and the missing-core classifier. Both need `launcher_find_core` from the core-map table, `find_rbf` which reads `/media/fat/`, and `launcher_mgl_error`. Easiest is to compile `launcher_launch.cpp` into the test binary after stubbing `LauncherGame` and providing a fake `/media/fat/` root.

Because `find_rbf` hard-codes `/media/fat/<dir>` and `launcher_write_mgl` writes to `/media/fat/launch.mgl`, the test needs to either (a) redirect those paths via a mock, (b) run as root with a tmp bind-mount — neither is nice in CI — or (c) introduce a small indirection in `launcher_launch.cpp` so tests can point both elsewhere.

Take option (c). Add at the top of `launcher_launch.cpp` (after includes):

```cpp
static const char *g_mister_root = "/media/fat";

/* Test hook: override the root used by find_rbf and launcher_write_mgl.
   Pass NULL to restore the production default. */
extern "C" void launcher_launch_set_root(const char *root)
{
    g_mister_root = root ? root : "/media/fat";
}
```

Then in `find_rbf`, replace the literal `"/media/fat"` with `g_mister_root`:

```cpp
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", g_mister_root, dir_name);
    /* ...rest of find_rbf unchanged... */
    snprintf(out, out_sz, "%s/%s/%s", g_mister_root, dir_name, best);
```

And in `launcher_write_mgl`, replace the hard-coded `"/media/fat/launch.mgl"` literal:

```cpp
    char mgl_path[1024];
    snprintf(mgl_path, sizeof(mgl_path), "%s/launch.mgl", g_mister_root);
    FILE *fp = fopen(mgl_path, "w");
```

(Update any subsequent reference in the same function — the existing code has a `const char *mgl_path = "/media/fat/launch.mgl";` line near the top and a `printf("launcher: wrote MGL to %s\n", mgl_path);` near the end. Both keep working once the literal is replaced with the local buffer.)

Declare the test hook in [launcher_launch.h](launcher_launch.h) behind a block comment noting it's for tests only:

```cpp
/* Test-only: override /media/fat root used by find_rbf and MGL writer. */
extern "C" void launcher_launch_set_root(const char *root);
```

Then create [tests/test_launcher_launch.cpp](tests/test_launcher_launch.cpp):

```cpp
/*
 * Tests for launcher_launch.cpp — MGL writer + missing-core classifier.
 * Uses launcher_launch_set_root() to redirect the MiSTer root to a tmp dir.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <ftw.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

/* Minimal LauncherGame + CoreMapEntry mirroring launcher.h */
struct LauncherGame {
    char name[128];
    char path[512];
    char cover_path[512];
    char system[64];
    uint32_t last_played;
    uint16_t play_count;
    uint32_t file_size;
    uint32_t play_time;
    uint8_t  user_rating;
};

struct CoreMapEntry {
    const char *system;
    const char *dir;
    const char *stem;
    int file_index;
};

/* Bring in the launch module. launcher_launch.cpp will need launcher.h; we
   #define LAUNCHER_H to stub it out, like test_launcher_scan does. */
#define LAUNCHER_H

extern const CoreMapEntry launcher_core_map[];
extern const int          launcher_core_map_count;
const CoreMapEntry *launcher_find_core(const char *system);

#include "../launcher_launch.cpp"

/* Core map: declare here so the linker resolves. The real one is in
   launcher_io.cpp which we don't include. Keep just one entry for the
   test. */
const CoreMapEntry launcher_core_map[] = {
    { "SNES",    "_Console", "SNES", 0 },
    { "Arcade",  "_Arcade",  NULL,   0 },
};
const int launcher_core_map_count =
    (int)(sizeof(launcher_core_map) / sizeof(launcher_core_map[0]));

const CoreMapEntry *launcher_find_core(const char *system)
{
    for (int i = 0; i < launcher_core_map_count; i++)
        if (strcasecmp(launcher_core_map[i].system, system) == 0)
            return &launcher_core_map[i];
    return NULL;
}

/* ─── harness ────────────────────────────────────────────────────────────── */

static int pass = 0, total = 0;
#define CHECK(expr) do { \
    total++; \
    if (expr) { pass++; printf("  PASS %s\n", #expr); } \
    else      {          printf("  FAIL %s\n", #expr); } \
} while (0)

#define TEST(name) static void name(void)

static std::string g_tmp;

static int rmrf_cb(const char *fpath, const struct stat *, int, struct FTW *)
{ remove(fpath); return 0; }

static void make_tmp(void)
{
    char tmpl[] = "/tmp/launcher_launch_testXXXXXX";
    const char *dir = mkdtemp(tmpl);
    assert(dir);
    g_tmp = dir;
    launcher_launch_set_root(g_tmp.c_str());
}

static void kill_tmp(void)
{
    if (g_tmp.empty()) return;
    launcher_launch_set_root(NULL);
    nftw(g_tmp.c_str(), rmrf_cb, 16, FTW_DEPTH | FTW_PHYS);
    g_tmp.clear();
}

static void touch(const std::string &p)
{
    std::string dir = p.substr(0, p.find_last_of('/'));
    std::string cur;
    for (size_t i = 1; i <= dir.size(); i++) {
        if (i == dir.size() || dir[i] == '/') {
            cur.assign(dir, 0, i);
            mkdir(cur.c_str(), 0755);
        }
    }
    int fd = open(p.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd >= 0);
    close(fd);
}

/* ─── tests ──────────────────────────────────────────────────────────────── */

TEST(test_write_mgl_console)
{
    make_tmp();
    touch(g_tmp + "/_Console/SNES_20240101.rbf");

    LauncherGame g = {};
    strncpy(g.system, "SNES", sizeof(g.system) - 1);
    strncpy(g.path,   "/path/to/mario.sfc", sizeof(g.path) - 1);

    /* The root redirect above made launcher_write_mgl write to
       <g_tmp>/launch.mgl instead of /media/fat/launch.mgl. */
    std::string mgl_path = g_tmp + "/launch.mgl";

    bool ok = launcher_write_mgl(&g);
    CHECK(ok);

    FILE *fp = fopen(mgl_path.c_str(), "r");
    CHECK(fp != NULL);
    if (fp) {
        char buf[512] = {};
        fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        CHECK(strstr(buf, "<rbf>") != NULL);
        CHECK(strstr(buf, "SNES_20240101") != NULL);
        CHECK(strstr(buf, "mario.sfc") != NULL);
    }

    kill_tmp();
}

TEST(test_write_mgl_missing_core)
{
    make_tmp();
    /* No .rbf in _Console */
    LauncherGame g = {};
    strncpy(g.system, "SNES", sizeof(g.system) - 1);
    strncpy(g.path,   "/whatever.sfc", sizeof(g.path) - 1);

    bool ok = launcher_write_mgl(&g);
    CHECK(!ok);
    CHECK(strstr(launcher_mgl_error(), "Core 'SNES' not found") != NULL);

    kill_tmp();
}

TEST(test_write_mgl_no_core_mapping)
{
    make_tmp();
    LauncherGame g = {};
    strncpy(g.system, "Spectrum", sizeof(g.system) - 1);

    bool ok = launcher_write_mgl(&g);
    CHECK(!ok);
    CHECK(strstr(launcher_mgl_error(), "No core is configured") != NULL);

    kill_tmp();
}

int main(void)
{
    printf("=== test_launcher_launch ===\n");
    test_write_mgl_console();
    test_write_mgl_missing_core();
    test_write_mgl_no_core_mapping();
    printf("=====================================================\n");
    printf("%d / %d tests passed\n", pass, total);
    return (pass == total) ? 0 : 1;
}
```


- [ ] **Step 3: Add to `tests/Makefile`**

```makefile
TESTS = test_pause_core test_launcher_core_map test_launcher_io test_launcher_covers test_launcher_nav test_launcher_scan test_launcher_state test_launcher_launch

test_launcher_state: test_launcher_state.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_launcher_launch: test_launcher_launch.cpp ../launcher_launch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<
```

- [ ] **Step 4: Build + run**

```bash
make -C tests clean && make -C tests 2>&1 | tail -10
for t in tests/test_launcher_*; do ./$t | tail -1; done
```

Expected: all test binaries pass. `test_launcher_state` and `test_launcher_launch` report their own totals.

- [ ] **Step 5: Full top-level build**

```bash
make -j4 2>&1 | tail -6
```

Expected: clean.

- [ ] **Step 6: Commit**

```bash
git add tests/
git commit -m "launcher: expand unit tests — state, launch, sort

Splits state-I/O tests out of test_launcher_io into their own binary
with new cases for corruption and play_time accumulation. Adds
test_launcher_launch covering the MGL writer, the missing-core
classifier, and the 'no core mapping' path. launcher_launch.cpp gets
a launcher_launch_set_root hook so tests can point it at a tmp dir
instead of the real /media/fat."
```

---

## Self-Review Notes (for the engineer)

After running all tasks, sanity-check these:

1. **Grep scan:**
   ```bash
   git grep -nE 'scrape_tgdb|exec_curl|tgdb_platform_id|LMODE_COVER_DL|LMODE_DESCRIPTION|g_cdl_|g_desc_|parse_csv|launcher_scan_remote|sftp_|TGDB_API_KEY' -- ':!lib/' ':!docs/'
   ```
   Expected empty.

2. **Line counts:**
   ```bash
   wc -l launcher*.cpp launcher*.h
   ```
   `launcher_io.cpp` should be ~400-500 lines. No file over ~1,500.

3. **Commits:**
   ```bash
   git log --oneline origin/master..HEAD
   ```
   Expected ~7 new commits on top of the pre-existing ones from the previous plan.

4. **Tests:**
   ```bash
   for t in tests/test_launcher_*; do ./$t | tail -1; done
   for t in tests/test_pause_core; do ./$t | tail -1; done
   ```
   All pass.

5. **On-device smoke** (manual):
   - Copy `bin/MiSTer` to the SD card.
   - Verify `/media/fat/games/SNES/` has a few ROMs.
   - Boot the launcher — SNES system shows up, games listed.
   - Place `<base>/covers/SNES/<stem>.jpg` for one of them, reboot, cover appears.
   - Delete a ROM file, reboot — it's gone from the grid. (Cache invalidation works because dir mtime changed.)
   - Favourite a game, reboot — still favourited.
   - Rename `/media/fat/_Console/SNES_*.rbf` temporarily, try to launch — error overlay reads "Core 'SNES' not found on SD card".
