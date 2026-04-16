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

/* Bring in the launch module. launcher_launch.cpp includes launcher.h and
   launcher_io.h — stub both out so our inline types take precedence. */
#define LAUNCHER_H
#define LAUNCHER_IO_H

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
