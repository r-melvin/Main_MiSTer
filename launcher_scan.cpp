/*
 * launcher_scan.cpp
 * Directory walker and per-system ROM extension allowlist.
 */

#include "launcher_scan.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

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
    bool oom;
};

static void ensure_capacity(ScanCtx *ctx)
{
    if (ctx->oom) return;
    if (ctx->count < ctx->capacity) return;
    int new_cap = ctx->capacity ? ctx->capacity * 2 : 128;
    LauncherGame *nb = (LauncherGame*)realloc(ctx->games, new_cap * sizeof(LauncherGame));
    if (!nb) { ctx->oom = true; return; }
    ctx->games = nb;
    ctx->capacity = new_cap;
}

static void walk_dir(ScanCtx *ctx, const char *path)
{
    if (ctx->oom) return;
    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ctx->oom) break;
        if (ent->d_name[0] == '.') continue;  /* skip hidden + . / .. */

        char child[PATH_MAX];
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
        if (ctx->oom) break;
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

    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/%s", games_dir, system);

    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;

    ScanCtx ctx = { system, covers_dir, exts, NULL, 0, 0, false };
    walk_dir(&ctx, root);

    if (ctx.oom) { free(ctx.games); *out = NULL; return -1; }

    if (ctx.count > 0)
        qsort(ctx.games, ctx.count, sizeof(LauncherGame), cmp_name);

    *out = ctx.games;
    return ctx.count;
}

/* ─── on-disk library cache ──────────────────────────────────────────────── */

#define LSC_MAGIC    0x4D535452u   /* 'MSTR' */
#define LSC_VERSION  2u

struct LscHeader {
    uint32_t magic;
    uint32_t version;
    char     games_dir[512];
    uint32_t system_count;
};

static_assert(sizeof(LscHeader) == 524,
              "LscHeader on-disk layout changed — bump LSC_VERSION");

struct LscSystemHeader {
    char     name[64];
    int64_t  mtime;       /* stored st_mtime of games_dir/<name> at save time */
    uint32_t game_count;
    uint32_t _pad;        /* keeps on-disk layout 8-byte aligned */
};

/* On-disk size is 80 bytes. Implicit compiler padding between name[64] and
   the int64_t mtime accounts for 0 bytes on common ABIs (name ends at 64,
   already 8-aligned); trailing _pad rounds the whole struct to an 8-byte
   boundary. Lock that in so any layout change is caught at compile time. */
static_assert(sizeof(LscSystemHeader) == 80,
              "LscSystemHeader on-disk layout changed — bump LSC_VERSION");

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
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) return false;

    /* Count real (non-virtual) systems first so the on-disk header matches
       the number of system blocks we actually write. */
    uint32_t real_count = 0;
    for (int i = 0; i < count; i++)
        if (!systems[i].is_virtual) real_count++;

    LscHeader hdr = {};
    hdr.magic = LSC_MAGIC;
    hdr.version = LSC_VERSION;
    strncpy(hdr.games_dir, games_dir, sizeof(hdr.games_dir) - 1);
    hdr.system_count = real_count;

    if (!write_all(fp, &hdr, sizeof(hdr))) goto fail;

    for (int i = 0; i < count; i++) {
        const LauncherSystem *s = &systems[i];
        if (s->is_virtual) continue;

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
