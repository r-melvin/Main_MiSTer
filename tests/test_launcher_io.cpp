/*
 * Tests for launcher I/O: CSV parsing, state persistence, cache path,
 * and favourite management.
 *
 * All logic is re-implemented inline here (no hardware dependencies).
 * Mirrors the code in launcher_io.cpp.
 *
 * Build: make -C tests
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <unistd.h>

/* ── Minimal types mirroring launcher.h ─────────────────────────────────── */

#define LAUNCHER_MAX_HISTORY  50
#define LAUNCHER_MAX_FAVS    256

struct LauncherGame {
    char name[128];
    char path[512];
    char cover_path[512];
    char system[64];
};

struct LauncherSystem {
    char name[64];
    LauncherGame *games;
    int  game_count;
    int  is_virtual;
};

struct HistoryEntry {
    char     system[64];
    char     path[512];
    char     name[128];
    char     cover_path[512];
    uint32_t ts;
    int      count;
};

struct FavEntry {
    char system[64];
    char path[512];
};

struct LauncherState {
    HistoryEntry history[LAUNCHER_MAX_HISTORY];
    int          history_count;
    FavEntry     favs[LAUNCHER_MAX_FAVS];
    int          fav_count;
};

/* ── Re-implemented CSV parser (mirrors launcher_io.cpp:parse_csv) ────────── */

static int parse_csv_str(const char *csv_content, const char *system_name,
                          const char *covers_dir,
                          LauncherGame **games_out)
{
    int capacity = 16;
    int count = 0;
    LauncherGame *games = (LauncherGame*)malloc(capacity * sizeof(LauncherGame));

    /* write csv_content to a temp file so we can use fgets */
    char tmp_path[] = "/tmp/test_csv_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) { free(games); *games_out = nullptr; return -1; }
    FILE *tmp = fdopen(fd, "w");
    fputs(csv_content, tmp);
    fclose(tmp);

    FILE *fp = fopen(tmp_path, "r");
    if (!fp) { free(games); remove(tmp_path); *games_out = nullptr; return -1; }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        int ln = (int)strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[--ln] = '\0';
        if (!ln) continue;

        char *name_col = line;
        char *path_col = strchr(name_col, '|');
        if (!path_col) continue;
        *path_col++ = '\0';
        char *cover_col = strchr(path_col, '|');
        if (!cover_col) continue;
        *cover_col++ = '\0';

        if (count == capacity) {
            capacity *= 2;
            games = (LauncherGame*)realloc(games, capacity * sizeof(LauncherGame));
        }
        LauncherGame *g = &games[count++];
        strncpy(g->name, name_col, sizeof(g->name) - 1);
        g->name[sizeof(g->name) - 1] = '\0';
        strncpy(g->path, path_col, sizeof(g->path) - 1);
        g->path[sizeof(g->path) - 1] = '\0';
        snprintf(g->cover_path, sizeof(g->cover_path),
                 "%s/%s/%s", covers_dir, system_name, cover_col);
        strncpy(g->system, system_name, sizeof(g->system) - 1);
        g->system[sizeof(g->system) - 1] = '\0';
    }
    fclose(fp);
    remove(tmp_path);
    *games_out = games;
    return count;
}

/* ── Re-implemented state I/O (mirrors launcher_io.cpp) ─────────────────── */

static bool save_state(const char *path, const LauncherState *st)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return false;
    fprintf(fp, "[favourites]\n");
    for (int i = 0; i < st->fav_count; i++)
        fprintf(fp, "%s=%s\n", st->favs[i].system, st->favs[i].path);
    fprintf(fp, "[history]\n");
    for (int i = 0; i < st->history_count; i++) {
        const HistoryEntry *h = &st->history[i];
        fprintf(fp, "%u|%s|%s|%s|%s|%d\n",
                h->ts, h->system, h->path, h->name, h->cover_path, h->count);
    }
    fclose(fp);
    return true;
}

static bool load_state(const char *path, LauncherState *st)
{
    memset(st, 0, sizeof(*st));
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    char line[1024];
    int section = 0;
    while (fgets(line, sizeof(line), fp)) {
        int ln = (int)strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[--ln] = '\0';
        if (!ln) continue;
        if (strcmp(line, "[favourites]") == 0) { section = 1; continue; }
        if (strcmp(line, "[history]") == 0)    { section = 2; continue; }
        if (section == 1 && st->fav_count < LAUNCHER_MAX_FAVS) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                FavEntry *f = &st->favs[st->fav_count++];
                strncpy(f->system, line,   sizeof(f->system) - 1);
                strncpy(f->path,   eq + 1, sizeof(f->path)   - 1);
            }
        } else if (section == 2 && st->history_count < LAUNCHER_MAX_HISTORY) {
            char *p = line;
            char *fields[6]; int fi = 0;
            fields[fi++] = p;
            while (*p && fi < 6) {
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
                h->count = atoi(fields[5]);
            }
        }
    }
    fclose(fp);
    return true;
}

static bool state_is_fav(const LauncherState *st, const LauncherGame *g)
{
    for (int i = 0; i < st->fav_count; i++)
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0)
            return true;
    return false;
}

static void state_toggle_fav(LauncherState *st, const LauncherGame *g)
{
    for (int i = 0; i < st->fav_count; i++) {
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0) {
            memmove(&st->favs[i], &st->favs[i + 1],
                    (st->fav_count - i - 1) * sizeof(FavEntry));
            st->fav_count--;
            return;
        }
    }
    if (st->fav_count < LAUNCHER_MAX_FAVS) {
        FavEntry *f = &st->favs[st->fav_count++];
        strncpy(f->system, g->system, sizeof(f->system) - 1);
        strncpy(f->path,   g->path,   sizeof(f->path)   - 1);
    }
}

static void launcher_cache_path(const LauncherGame *game, const char *base_dir,
                                 char *out, size_t out_sz)
{
    const char *rom_name = strrchr(game->path, '/');
    rom_name = rom_name ? rom_name + 1 : game->path;
    snprintf(out, out_sz, "%s/cache/%s/%s", base_dir, game->system, rom_name);
}

/* ── Test helpers ─────────────────────────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(); \
    static void name()

#define RUN(name) do { \
    tests_run++; \
    printf("  %-55s", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ── CSV tests ────────────────────────────────────────────────────────────── */

TEST(test_csv_single_game)
{
    LauncherGame *games = nullptr;
    int n = parse_csv_str("Super Mario World|/roms/smw.sfc|smw.png\n",
                           "SNES", "/covers", &games);
    assert(n == 1);
    assert(strcmp(games[0].name,   "Super Mario World") == 0);
    assert(strcmp(games[0].path,   "/roms/smw.sfc") == 0);
    assert(strcmp(games[0].cover_path, "/covers/SNES/smw.png") == 0);
    assert(strcmp(games[0].system, "SNES") == 0);
    free(games);
}

TEST(test_csv_multiple_games)
{
    const char *csv =
        "Zelda|/roms/zelda.nes|zelda.png\n"
        "Mario|/roms/mario.nes|mario.png\n"
        "Metroid|/roms/metroid.nes|metroid.png\n";
    LauncherGame *games = nullptr;
    int n = parse_csv_str(csv, "NES", "/covers", &games);
    assert(n == 3);
    free(games);
}

TEST(test_csv_strips_newline)
{
    LauncherGame *games = nullptr;
    int n = parse_csv_str("Game A|/a.rom|a.png\r\n", "SNES", "/c", &games);
    assert(n == 1);
    assert(games[0].name[strlen(games[0].name) - 1] != '\n');
    assert(games[0].name[strlen(games[0].name) - 1] != '\r');
    free(games);
}

TEST(test_csv_skips_malformed_lines)
{
    const char *csv =
        "GoodGame|/path/rom.sfc|cover.png\n"
        "BadLine_no_pipes\n"           /* no pipes → skip */
        "OnePipe|/path/rom.sfc\n"     /* only one pipe → skip */
        "OK2|/path2.sfc|cover2.png\n";
    LauncherGame *games = nullptr;
    int n = parse_csv_str(csv, "SNES", "/c", &games);
    assert(n == 2);
    free(games);
}

TEST(test_csv_cover_path_assembled)
{
    LauncherGame *games = nullptr;
    parse_csv_str("G|/path.rom|my_cover.png\n", "Genesis", "/media/fat/covers", &games);
    assert(strcmp(games[0].cover_path, "/media/fat/covers/Genesis/my_cover.png") == 0);
    free(games);
}

/* ── State file tests ────────────────────────────────────────────────────── */

TEST(test_state_roundtrip_empty)
{
    char tmp[] = "/tmp/test_state_XXXXXX";
    int fd = mkstemp(tmp); close(fd);

    LauncherState st_in = {}, st_out = {};
    assert(save_state(tmp, &st_in));
    assert(load_state(tmp, &st_out));
    assert(st_out.fav_count == 0);
    assert(st_out.history_count == 0);
    remove(tmp);
}

TEST(test_state_favourites_roundtrip)
{
    char tmp[] = "/tmp/test_state_XXXXXX";
    int fd = mkstemp(tmp); close(fd);

    LauncherState st_in = {};
    strncpy(st_in.favs[0].system, "SNES",       sizeof(st_in.favs[0].system) - 1);
    strncpy(st_in.favs[0].path,   "/roms/smw.sfc", sizeof(st_in.favs[0].path) - 1);
    strncpy(st_in.favs[1].system, "NES",           sizeof(st_in.favs[1].system) - 1);
    strncpy(st_in.favs[1].path,   "/roms/mario.nes", sizeof(st_in.favs[1].path) - 1);
    st_in.fav_count = 2;

    assert(save_state(tmp, &st_in));

    LauncherState st_out = {};
    assert(load_state(tmp, &st_out));
    assert(st_out.fav_count == 2);
    assert(strcmp(st_out.favs[0].system, "SNES") == 0);
    assert(strcmp(st_out.favs[0].path, "/roms/smw.sfc") == 0);
    assert(strcmp(st_out.favs[1].system, "NES") == 0);

    remove(tmp);
}

TEST(test_state_history_roundtrip)
{
    char tmp[] = "/tmp/test_state_XXXXXX";
    int fd = mkstemp(tmp); close(fd);

    LauncherState st_in = {};
    HistoryEntry *h = &st_in.history[0];
    h->ts = 1712345678u;
    h->count = 5;
    strncpy(h->system, "SNES",         sizeof(h->system) - 1);
    strncpy(h->path,   "/roms/smw.sfc", sizeof(h->path)   - 1);
    strncpy(h->name,   "Super Mario World", sizeof(h->name) - 1);
    strncpy(h->cover_path, "/covers/SNES/smw.png", sizeof(h->cover_path) - 1);
    st_in.history_count = 1;

    assert(save_state(tmp, &st_in));

    LauncherState st_out = {};
    assert(load_state(tmp, &st_out));
    assert(st_out.history_count == 1);
    assert(st_out.history[0].ts == 1712345678u);
    assert(st_out.history[0].count == 5);
    assert(strcmp(st_out.history[0].system, "SNES") == 0);
    assert(strcmp(st_out.history[0].path, "/roms/smw.sfc") == 0);
    assert(strcmp(st_out.history[0].name, "Super Mario World") == 0);

    remove(tmp);
}

TEST(test_fav_toggle_add)
{
    LauncherState st = {};
    LauncherGame g;
    strncpy(g.system, "NES",        sizeof(g.system) - 1);
    strncpy(g.path,   "/roms/nes/mario.nes", sizeof(g.path) - 1);

    assert(!state_is_fav(&st, &g));
    state_toggle_fav(&st, &g);
    assert(state_is_fav(&st, &g));
    assert(st.fav_count == 1);
}

TEST(test_fav_toggle_remove)
{
    LauncherState st = {};
    LauncherGame g;
    strncpy(g.system, "NES",        sizeof(g.system) - 1);
    strncpy(g.path,   "/roms/mario.nes", sizeof(g.path) - 1);

    state_toggle_fav(&st, &g);  /* add */
    assert(state_is_fav(&st, &g));
    state_toggle_fav(&st, &g);  /* remove */
    assert(!state_is_fav(&st, &g));
    assert(st.fav_count == 0);
}

TEST(test_fav_independent_by_system)
{
    LauncherState st = {};
    LauncherGame g1, g2;
    /* same path, different system → treated as different */
    strncpy(g1.system, "SNES", sizeof(g1.system) - 1);
    strncpy(g1.path, "/roms/game.bin", sizeof(g1.path) - 1);
    strncpy(g2.system, "NES",  sizeof(g2.system) - 1);
    strncpy(g2.path, "/roms/game.bin", sizeof(g2.path) - 1);

    state_toggle_fav(&st, &g1);
    assert( state_is_fav(&st, &g1));
    assert(!state_is_fav(&st, &g2));
}

/* ── Cache path tests ────────────────────────────────────────────────────── */

TEST(test_cache_path_construction)
{
    LauncherGame g;
    strncpy(g.system, "SNES", sizeof(g.system) - 1);
    strncpy(g.path,   "/remote/SNES/super_mario_world.sfc", sizeof(g.path) - 1);
    char out[512];
    launcher_cache_path(&g, "/media/fat/remote_ui", out, sizeof(out));
    assert(strcmp(out, "/media/fat/remote_ui/cache/SNES/super_mario_world.sfc") == 0);
}

TEST(test_cache_path_no_slash_in_game_path)
{
    LauncherGame g;
    strncpy(g.system, "NES", sizeof(g.system) - 1);
    strncpy(g.path,   "mario.nes", sizeof(g.path) - 1);
    char out[512];
    launcher_cache_path(&g, "/base", out, sizeof(out));
    assert(strcmp(out, "/base/cache/NES/mario.nes") == 0);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main()
{
    printf("launcher_io test suite\n");
    printf("=====================================================\n");

    RUN(test_csv_single_game);
    RUN(test_csv_multiple_games);
    RUN(test_csv_strips_newline);
    RUN(test_csv_skips_malformed_lines);
    RUN(test_csv_cover_path_assembled);
    RUN(test_state_roundtrip_empty);
    RUN(test_state_favourites_roundtrip);
    RUN(test_state_history_roundtrip);
    RUN(test_fav_toggle_add);
    RUN(test_fav_toggle_remove);
    RUN(test_fav_independent_by_system);
    RUN(test_cache_path_construction);
    RUN(test_cache_path_no_slash_in_game_path);

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
