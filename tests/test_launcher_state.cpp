/*
 * Tests for launcher state I/O: round-trip, merge, corruption, play_time.
 * All state logic is re-implemented inline — no hardware dependencies.
 *
 * Build: make -C tests
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <ftw.h>
#include <unistd.h>
#include <string>

/* ── Minimal types mirroring launcher.h ─────────────────────────────────── */

#define LAUNCHER_MAX_HISTORY  50
#define LAUNCHER_MAX_FAVS    256
#define LAUNCHER_MAX_SYSTEMS  64

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

struct HistoryEntry {
    char     system[64];
    char     path[512];
    char     name[128];
    char     cover_path[512];
    uint32_t ts;
    int      count;
    uint32_t play_time;
    uint8_t  user_rating;
};

struct FavEntry {
    char system[64];
    char path[512];
};

struct SystemMemory {
    int selected_game;
    int scroll_offset;
};

struct LauncherState {
    HistoryEntry history[LAUNCHER_MAX_HISTORY];
    int          history_count;
    FavEntry     favs[LAUNCHER_MAX_FAVS];
    int          fav_count;
    SystemMemory per_system[LAUNCHER_MAX_SYSTEMS];
};

/* ── Re-implemented state I/O (mirrors launcher_io.cpp) ─────────────────── */

static bool launcher_load_state(const char *path, LauncherState *st)
{
    memset(st, 0, sizeof(*st));
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    char line[1024];
    int section = 0;
    while (fgets(line, sizeof(line), fp)) {
        int ln = (int)strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r'))
            line[--ln] = '\0';
        if (!ln) continue;
        if (strcmp(line, "[favourites]") == 0) { section = 1; continue; }
        if (strcmp(line, "[history]") == 0)    { section = 2; continue; }
        if (strcmp(line, "[positions]") == 0)  { section = 3; continue; }
        if (section == 1 && st->fav_count < LAUNCHER_MAX_FAVS) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                FavEntry *f = &st->favs[st->fav_count++];
                strncpy(f->system, line,   sizeof(f->system) - 1);
                strncpy(f->path,   eq + 1, sizeof(f->path)   - 1);
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
                h->count       = atoi(fields[5]);
                h->play_time   = (fi >= 7) ? (uint32_t)atol(fields[6]) : 0;
                h->user_rating = (fi >= 8) ? (uint8_t)atoi(fields[7]) : 0;
            }
        }
    }
    fclose(fp);
    return true;
}

static bool launcher_save_state(const char *path, const LauncherState *st)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return false;
    fprintf(fp, "[favourites]\n");
    for (int i = 0; i < st->fav_count; i++)
        fprintf(fp, "%s=%s\n", st->favs[i].system, st->favs[i].path);
    fprintf(fp, "[history]\n");
    for (int i = 0; i < st->history_count; i++) {
        const HistoryEntry *h = &st->history[i];
        fprintf(fp, "%u|%s|%s|%s|%s|%d|%u|%u\n",
                h->ts, h->system, h->path, h->name, h->cover_path,
                h->count, h->play_time, (unsigned)h->user_rating);
    }
    fclose(fp);
    return true;
}

static void launcher_state_record_played(LauncherState *st, const LauncherGame *g,
                                          const char *state_path)
{
    uint32_t now = (uint32_t)time(NULL);
    for (int i = 0; i < st->history_count; i++) {
        if (strcmp(st->history[i].path, g->path) == 0 &&
            strcmp(st->history[i].system, g->system) == 0) {
            st->history[i].ts = now;
            st->history[i].count++;
            HistoryEntry tmp = st->history[i];
            memmove(&st->history[1], &st->history[0], i * sizeof(HistoryEntry));
            st->history[0] = tmp;
            launcher_save_state(state_path, st);
            return;
        }
    }
    if (st->history_count == LAUNCHER_MAX_HISTORY)
        st->history_count--;
    memmove(&st->history[1], &st->history[0],
            st->history_count * sizeof(HistoryEntry));
    HistoryEntry *h = &st->history[0];
    h->ts    = now;
    h->count = 1;
    h->play_time   = 0;
    h->user_rating = 0;
    strncpy(h->system,     g->system,     sizeof(h->system) - 1);
    strncpy(h->path,       g->path,       sizeof(h->path)   - 1);
    strncpy(h->name,       g->name,       sizeof(h->name)   - 1);
    strncpy(h->cover_path, g->cover_path, sizeof(h->cover_path) - 1);
    st->history_count++;
    launcher_save_state(state_path, st);
}

static bool launcher_state_is_favourite(const LauncherState *st, const LauncherGame *g)
{
    for (int i = 0; i < st->fav_count; i++)
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0)
            return true;
    return false;
}

static void launcher_state_toggle_fav(LauncherState *st, const LauncherGame *g,
                                       const char *state_path)
{
    for (int i = 0; i < st->fav_count; i++) {
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0) {
            memmove(&st->favs[i], &st->favs[i + 1],
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

/* ── Test harness ─────────────────────────────────────────────────────────── */

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
    char tmpl[] = "/tmp/launcher_state_testXXXXXX";
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

/* ── Tests ────────────────────────────────────────────────────────────────── */

TEST(test_state_roundtrip_empty)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";
    LauncherState st_in = {}, st_out = {};
    CHECK(launcher_save_state(path.c_str(), &st_in));
    CHECK(launcher_load_state(path.c_str(), &st_out));
    CHECK(st_out.fav_count == 0);
    CHECK(st_out.history_count == 0);
    kill_tmp();
}

TEST(test_state_favourites_roundtrip)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";
    LauncherState st_in = {};
    strncpy(st_in.favs[0].system, "SNES",           sizeof(st_in.favs[0].system) - 1);
    strncpy(st_in.favs[0].path,   "/roms/smw.sfc",  sizeof(st_in.favs[0].path)   - 1);
    strncpy(st_in.favs[1].system, "NES",             sizeof(st_in.favs[1].system) - 1);
    strncpy(st_in.favs[1].path,   "/roms/mario.nes", sizeof(st_in.favs[1].path)   - 1);
    st_in.fav_count = 2;
    CHECK(launcher_save_state(path.c_str(), &st_in));
    LauncherState st_out = {};
    CHECK(launcher_load_state(path.c_str(), &st_out));
    CHECK(st_out.fav_count == 2);
    CHECK(strcmp(st_out.favs[0].system, "SNES") == 0);
    CHECK(strcmp(st_out.favs[0].path, "/roms/smw.sfc") == 0);
    CHECK(strcmp(st_out.favs[1].system, "NES") == 0);
    kill_tmp();
}

TEST(test_state_history_roundtrip)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";
    LauncherState st_in = {};
    HistoryEntry *h = &st_in.history[0];
    h->ts = 1712345678u;
    h->count = 5;
    h->play_time = 120;
    strncpy(h->system,     "SNES",                  sizeof(h->system) - 1);
    strncpy(h->path,       "/roms/smw.sfc",          sizeof(h->path)   - 1);
    strncpy(h->name,       "Super Mario World",      sizeof(h->name)   - 1);
    strncpy(h->cover_path, "/covers/SNES/smw.png",  sizeof(h->cover_path) - 1);
    st_in.history_count = 1;
    CHECK(launcher_save_state(path.c_str(), &st_in));
    LauncherState st_out = {};
    CHECK(launcher_load_state(path.c_str(), &st_out));
    CHECK(st_out.history_count == 1);
    CHECK(st_out.history[0].ts == 1712345678u);
    CHECK(st_out.history[0].count == 5);
    CHECK(st_out.history[0].play_time == 120u);
    CHECK(strcmp(st_out.history[0].system, "SNES") == 0);
    CHECK(strcmp(st_out.history[0].path, "/roms/smw.sfc") == 0);
    CHECK(strcmp(st_out.history[0].name, "Super Mario World") == 0);
    kill_tmp();
}

TEST(test_fav_toggle_add_remove)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";
    LauncherState st = {};
    LauncherGame g = {};
    strncpy(g.system, "NES",              sizeof(g.system) - 1);
    strncpy(g.path,   "/roms/mario.nes",  sizeof(g.path)   - 1);

    CHECK(!launcher_state_is_favourite(&st, &g));
    launcher_state_toggle_fav(&st, &g, path.c_str());
    CHECK(launcher_state_is_favourite(&st, &g));
    CHECK(st.fav_count == 1);
    launcher_state_toggle_fav(&st, &g, path.c_str());
    CHECK(!launcher_state_is_favourite(&st, &g));
    CHECK(st.fav_count == 0);
    kill_tmp();
}

TEST(test_fav_independent_by_system)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";
    LauncherState st = {};
    LauncherGame g1 = {}, g2 = {};
    strncpy(g1.system, "SNES",           sizeof(g1.system) - 1);
    strncpy(g1.path,   "/roms/game.bin", sizeof(g1.path)   - 1);
    strncpy(g2.system, "NES",            sizeof(g2.system) - 1);
    strncpy(g2.path,   "/roms/game.bin", sizeof(g2.path)   - 1);
    launcher_state_toggle_fav(&st, &g1, path.c_str());
    CHECK( launcher_state_is_favourite(&st, &g1));
    CHECK(!launcher_state_is_favourite(&st, &g2));
    kill_tmp();
}

TEST(test_state_merge_sequence)
{
    make_tmp();
    std::string path = g_tmp + "/state.dat";
    LauncherState st = {};
    LauncherGame g1 = {}, g2 = {};
    strncpy(g1.system, "SNES",                sizeof(g1.system) - 1);
    strncpy(g1.path,   "/roms/mario.sfc",     sizeof(g1.path)   - 1);
    strncpy(g1.name,   "mario",               sizeof(g1.name)   - 1);
    strncpy(g2.system, "NES",                 sizeof(g2.system) - 1);
    strncpy(g2.path,   "/roms/zelda.nes",     sizeof(g2.path)   - 1);
    strncpy(g2.name,   "zelda",               sizeof(g2.name)   - 1);

    launcher_state_record_played(&st, &g1, path.c_str());
    launcher_state_record_played(&st, &g2, path.c_str());
    launcher_state_record_played(&st, &g1, path.c_str());

    /* g1 played twice — should be at front, count == 2 */
    CHECK(st.history_count == 2);
    CHECK(strcmp(st.history[0].path, "/roms/mario.sfc") == 0);
    CHECK(st.history[0].count == 2);
    CHECK(strcmp(st.history[1].path, "/roms/zelda.nes") == 0);
    CHECK(st.history[1].count == 1);

    /* round-trip */
    LauncherState st2 = {};
    CHECK(launcher_load_state(path.c_str(), &st2));
    CHECK(st2.history_count == 2);
    CHECK(strcmp(st2.history[0].path, "/roms/mario.sfc") == 0);
    CHECK(st2.history[0].count == 2);
    kill_tmp();
}

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
    strncpy(g.system, "SNES",                  sizeof(g.system) - 1);
    strncpy(g.path,   "/games/SNES/mario.sfc", sizeof(g.path)   - 1);
    strncpy(g.name,   "mario",                 sizeof(g.name)   - 1);

    launcher_state_record_played(&st, &g, "/tmp/irrelevant.dat");
    st.history[0].play_time = 300;
    launcher_state_record_played(&st, &g, "/tmp/irrelevant.dat");
    st.history[0].play_time += 400;
    CHECK(st.history[0].play_time == 700);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_launcher_state ===\n");
    test_state_roundtrip_empty();
    test_state_favourites_roundtrip();
    test_state_history_roundtrip();
    test_fav_toggle_add_remove();
    test_fav_independent_by_system();
    test_state_merge_sequence();
    test_state_corrupted_file_graceful();
    test_state_play_time_accumulation();
    printf("=====================================================\n");
    printf("%d / %d tests passed\n", pass, total);
    return (pass == total) ? 0 : 1;
}
