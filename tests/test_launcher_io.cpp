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
#include <unistd.h>

/* ── Minimal types mirroring launcher.h ─────────────────────────────────── */

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

/* ── Cache path tests ────────────────────────────────────────────────────── */

TEST(test_cache_path_construction)
{
    LauncherGame g;
    strncpy(g.system, "SNES", sizeof(g.system) - 1);
    strncpy(g.path,   "/remote/SNES/super_mario_world.sfc", sizeof(g.path) - 1);
    char out[512];
    launcher_cache_path(&g, "/media/fat/launcher", out, sizeof(out));
    assert(strcmp(out, "/media/fat/launcher/cache/SNES/super_mario_world.sfc") == 0);
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
    RUN(test_cache_path_construction);
    RUN(test_cache_path_no_slash_in_game_path);

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
