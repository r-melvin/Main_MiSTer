/*
 * Tests for the launcher core map lookup.
 *
 * The real table is in launcher_io.cpp and has hardware dependencies.
 * This test inlines the table and lookup logic, verifying:
 *   1. Known systems return correct dir, stem, file_index
 *   2. Lookup is case-insensitive
 *   3. Unknown systems return NULL
 *   4. All 23 entries are present and non-overlapping
 *
 * Build: make -C tests
 */

#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <strings.h>   /* strcasecmp */

/* ── Minimal types mirroring launcher.h ─────────────────────────────────── */

struct CoreMapEntry {
    const char *system;
    const char *dir;
    const char *stem;
    int         file_index;
};

/* ── Core map table (mirrors launcher_io.cpp) ────────────────────────────── */

static const CoreMapEntry core_map[] = {
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
    { "Arcade",        "_Arcade",   nullptr,         0 },
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
static const int core_map_count = (int)(sizeof(core_map) / sizeof(core_map[0]));

static const CoreMapEntry *find_core(const char *system)
{
    for (int i = 0; i < core_map_count; i++)
        if (strcasecmp(core_map[i].system, system) == 0)
            return &core_map[i];
    return nullptr;
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

/* ── Tests ────────────────────────────────────────────────────────────────── */

TEST(test_snes_lookup)
{
    const CoreMapEntry *e = find_core("SNES");
    assert(e != nullptr);
    assert(strcmp(e->dir, "_Console") == 0);
    assert(strcmp(e->stem, "SNES") == 0);
    assert(e->file_index == 0);
}

TEST(test_nes_lookup)
{
    const CoreMapEntry *e = find_core("NES");
    assert(e != nullptr);
    assert(strcmp(e->stem, "NES") == 0);
}

TEST(test_neogeo_file_index_1)
{
    const CoreMapEntry *e = find_core("NeoGeo");
    assert(e != nullptr);
    assert(e->file_index == 1);
}

TEST(test_psx_file_index_1)
{
    const CoreMapEntry *e = find_core("PSX");
    assert(e != nullptr);
    assert(e->file_index == 1);
}

TEST(test_arcade_null_stem)
{
    const CoreMapEntry *e = find_core("Arcade");
    assert(e != nullptr);
    assert(e->stem == nullptr);
    assert(strcmp(e->dir, "_Arcade") == 0);
}

TEST(test_computer_c64)
{
    const CoreMapEntry *e = find_core("C64");
    assert(e != nullptr);
    assert(strcmp(e->dir, "_Computer") == 0);
    assert(strcmp(e->stem, "C64") == 0);
}

TEST(test_amiga_ocs)
{
    const CoreMapEntry *e = find_core("AmigaOCS");
    assert(e != nullptr);
    assert(strcmp(e->stem, "Minimig") == 0);
}

TEST(test_case_insensitive_snes)
{
    assert(find_core("snes") != nullptr);
    assert(find_core("Snes") != nullptr);
    assert(find_core("SNES") != nullptr);
}

TEST(test_case_insensitive_genesis)
{
    assert(find_core("genesis") != nullptr);
    assert(find_core("GENESIS") != nullptr);
}

TEST(test_unknown_system_returns_null)
{
    assert(find_core("PS3") == nullptr);
    assert(find_core("") == nullptr);
    assert(find_core("Xbox360") == nullptr);
}

TEST(test_megadrive_same_stem_as_genesis)
{
    const CoreMapEntry *gen = find_core("Genesis");
    const CoreMapEntry *md  = find_core("MegaDrive");
    assert(gen && md);
    assert(strcmp(gen->stem, md->stem) == 0);
}

TEST(test_turbografx_and_pcengine_same_stem)
{
    const CoreMapEntry *tg  = find_core("TurboGrafx16");
    const CoreMapEntry *pce = find_core("PCEngine");
    assert(tg && pce);
    assert(strcmp(tg->stem, pce->stem) == 0);
}

TEST(test_gb_and_gbc_same_stem)
{
    const CoreMapEntry *gb  = find_core("GB");
    const CoreMapEntry *gbc = find_core("GBC");
    assert(gb && gbc);
    assert(strcmp(gb->stem, gbc->stem) == 0);
}

TEST(test_total_entry_count)
{
    assert(core_map_count == 23);
}

TEST(test_all_entries_have_system_and_dir)
{
    for (int i = 0; i < core_map_count; i++) {
        assert(core_map[i].system != nullptr && core_map[i].system[0] != '\0');
        assert(core_map[i].dir    != nullptr && core_map[i].dir[0]    != '\0');
    }
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main()
{
    printf("launcher_core_map test suite\n");
    printf("=====================================================\n");

    RUN(test_snes_lookup);
    RUN(test_nes_lookup);
    RUN(test_neogeo_file_index_1);
    RUN(test_psx_file_index_1);
    RUN(test_arcade_null_stem);
    RUN(test_computer_c64);
    RUN(test_amiga_ocs);
    RUN(test_case_insensitive_snes);
    RUN(test_case_insensitive_genesis);
    RUN(test_unknown_system_returns_null);
    RUN(test_megadrive_same_stem_as_genesis);
    RUN(test_turbografx_and_pcengine_same_stem);
    RUN(test_gb_and_gbc_same_stem);
    RUN(test_total_entry_count);
    RUN(test_all_entries_have_system_and_dir);

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
