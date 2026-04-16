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

/* Minimal LauncherSystem matching the layout in launcher.h. Must stay in sync.
   Defined before #include so the cache code in launcher_scan.cpp can see it. */
struct LauncherSystem {
    char name[64];
    LauncherGame *games;
    int game_count;
    int is_virtual;
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

/* ─── cache ──────────────────────────────────────────────────────────────── */

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
    /* Force a second-level mtime bump so the cache loader detects the change.
       st_mtime on most Linux filesystems (tmpfs, ext4, xfs) is second-granular
       regardless of nanosecond support in the kernel. Removing this sleep
       makes the test flaky on fast hosts. */
    sleep(1);
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

TEST(test_cache_skips_virtual_systems)
{
    make_tmp();
    std::string games = g_tmp + "/games";
    std::string cache = g_tmp + "/library.bin";
    mkpath(games + "/SNES");

    LauncherSystem sys[2] = {};
    strncpy(sys[0].name, "SNES", sizeof(sys[0].name) - 1);
    sys[0].game_count = 0;
    sys[0].is_virtual = 0;
    strncpy(sys[1].name, "Favourites", sizeof(sys[1].name) - 1);
    sys[1].game_count = 0;
    sys[1].is_virtual = 1;

    CHECK(launcher_scan_cache_save(cache.c_str(), games.c_str(), sys, 2));

    LauncherSystem *loaded = NULL;
    int n = 0;
    CHECK(launcher_scan_cache_load(cache.c_str(), games.c_str(), &loaded, &n));
    CHECK(n == 1);                                      /* virtual dropped */
    CHECK(n >= 1 && strcmp(loaded[0].name, "SNES") == 0);
    free_systems(loaded, n);

    kill_tmp();
}

int main(void)
{
    printf("=== test_launcher_scan ===\n");

    test_extensions_known_system();
    test_extensions_case_insensitive();
    test_extensions_unknown_returns_null();
    test_scan_extension_filter();
    test_scan_recursive_walk();
    test_scan_empty_dir();
    test_scan_missing_system_returns_negative_one();
    test_scan_cover_resolution_jpg();
    test_scan_cover_resolution_png_fallback();
    test_scan_cover_missing_empty_string();
    test_scan_file_size_populated();
    test_cache_roundtrip();
    test_cache_mtime_invalidates();
    test_cache_games_dir_mismatch();
    test_cache_corrupted_header();
    test_cache_missing_file();
    test_cache_skips_virtual_systems();

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", pass, total);
    return (pass == total) ? 0 : 1;
}
