/*
 * Tests for the pause_core functionality added to MiSTer.
 *
 * Since the real OSD/SPI code requires FPGA hardware, we mock the SPI layer
 * and re-implement the thin OsdPause/OsdIsPaused logic here.  The tests verify:
 *   1. OsdPause(1) sends OSD_CMD_ENABLE  (0x41) and sets paused state
 *   2. OsdPause(0) sends OSD_CMD_DISABLE (0x40) and clears paused state
 *   3. Toggle semantics (no explicit arg flips state)
 *   4. FIFO command-string parsing ("pause_core 1", "pause_core 0", "pause_core")
 *
 * Build with: make -C tests   (uses native g++, not the ARM cross-compiler)
 * Run  with: ./tests/test_pause_core
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <vector>

// ── SPI mock ────────────────────────────────────────────────────────────────

static std::vector<uint8_t> spi_log;          // records every spi_osd_cmd call

static void spi_osd_cmd(uint8_t cmd)
{
    spi_log.push_back(cmd);
}

// ── Re-implement the pause functions (mirrors osd.cpp) ──────────────────────

#define OSD_CMD_ENABLE   0x41
#define OSD_CMD_DISABLE  0x40

static int osd_paused = 0;

static void OsdPause(int pause)
{
    osd_paused = pause;
    if (pause)
        spi_osd_cmd(OSD_CMD_ENABLE);
    else
        spi_osd_cmd(OSD_CMD_DISABLE);
}

static int OsdIsPaused()
{
    return osd_paused;
}

// ── Re-implement the FIFO command parser (mirrors input.cpp) ────────────────

static void handle_pause_cmd(const char *cmd)
{
    if (strncmp(cmd, "pause_core", 10) != 0) return;

    const char *arg = cmd + 10;
    while (*arg == ' ' || *arg == '\t') arg++;

    int pause;
    if (*arg == '1')      pause = 1;
    else if (*arg == '0') pause = 0;
    else                  pause = !OsdIsPaused();   // toggle

    OsdPause(pause);
}

// ── Helpers ─────────────────────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(); \
    static void name()

#define RUN(name) do { \
    spi_log.clear(); \
    osd_paused = 0; \
    tests_run++; \
    printf("  %-50s", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

// ── Tests ───────────────────────────────────────────────────────────────────

TEST(test_pause_sets_state)
{
    OsdPause(1);
    assert(OsdIsPaused() == 1);
}

TEST(test_resume_clears_state)
{
    OsdPause(1);
    OsdPause(0);
    assert(OsdIsPaused() == 0);
}

TEST(test_pause_sends_enable_cmd)
{
    OsdPause(1);
    assert(spi_log.size() == 1);
    assert(spi_log[0] == OSD_CMD_ENABLE);
}

TEST(test_resume_sends_disable_cmd)
{
    OsdPause(1);
    spi_log.clear();
    OsdPause(0);
    assert(spi_log.size() == 1);
    assert(spi_log[0] == OSD_CMD_DISABLE);
}

TEST(test_pause_enable_value_is_0x41)
{
    OsdPause(1);
    assert(spi_log.back() == 0x41);
}

TEST(test_resume_disable_value_is_0x40)
{
    OsdPause(1);
    OsdPause(0);
    assert(spi_log.back() == 0x40);
}

TEST(test_double_pause_keeps_state)
{
    OsdPause(1);
    OsdPause(1);
    assert(OsdIsPaused() == 1);
    assert(spi_log.size() == 2);
    assert(spi_log[0] == OSD_CMD_ENABLE);
    assert(spi_log[1] == OSD_CMD_ENABLE);
}

TEST(test_double_resume_keeps_state)
{
    OsdPause(0);
    OsdPause(0);
    assert(OsdIsPaused() == 0);
}

// ── FIFO command parsing tests ──────────────────────────────────────────────

TEST(test_cmd_pause_core_1)
{
    handle_pause_cmd("pause_core 1");
    assert(OsdIsPaused() == 1);
    assert(spi_log.back() == OSD_CMD_ENABLE);
}

TEST(test_cmd_pause_core_0)
{
    OsdPause(1);
    spi_log.clear();
    handle_pause_cmd("pause_core 0");
    assert(OsdIsPaused() == 0);
    assert(spi_log.back() == OSD_CMD_DISABLE);
}

TEST(test_cmd_pause_core_toggle_from_unpaused)
{
    // Starting unpaused → toggle should pause
    handle_pause_cmd("pause_core");
    assert(OsdIsPaused() == 1);
    assert(spi_log.back() == OSD_CMD_ENABLE);
}

TEST(test_cmd_pause_core_toggle_from_paused)
{
    OsdPause(1);
    spi_log.clear();
    // Starting paused → toggle should resume
    handle_pause_cmd("pause_core");
    assert(OsdIsPaused() == 0);
    assert(spi_log.back() == OSD_CMD_DISABLE);
}

TEST(test_cmd_pause_core_extra_whitespace)
{
    handle_pause_cmd("pause_core   \t 1");
    assert(OsdIsPaused() == 1);
}

TEST(test_cmd_pause_core_no_match)
{
    // A non-matching command should be a no-op
    handle_pause_cmd("load_core foo.rbf");
    assert(OsdIsPaused() == 0);
    assert(spi_log.empty());
}

TEST(test_cmd_roundtrip_pause_resume)
{
    handle_pause_cmd("pause_core 1");
    assert(OsdIsPaused() == 1);
    handle_pause_cmd("pause_core 0");
    assert(OsdIsPaused() == 0);
    assert(spi_log.size() == 2);
    assert(spi_log[0] == OSD_CMD_ENABLE);
    assert(spi_log[1] == OSD_CMD_DISABLE);
}

TEST(test_cmd_toggle_sequence)
{
    handle_pause_cmd("pause_core");   // 0 → 1
    assert(OsdIsPaused() == 1);
    handle_pause_cmd("pause_core");   // 1 → 0
    assert(OsdIsPaused() == 0);
    handle_pause_cmd("pause_core");   // 0 → 1
    assert(OsdIsPaused() == 1);
    assert(spi_log.size() == 3);
}

// ── main ────────────────────────────────────────────────────────────────────

int main()
{
    printf("pause_core test suite\n");
    printf("=====================================================\n");

    // OsdPause / OsdIsPaused unit tests
    RUN(test_pause_sets_state);
    RUN(test_resume_clears_state);
    RUN(test_pause_sends_enable_cmd);
    RUN(test_resume_sends_disable_cmd);
    RUN(test_pause_enable_value_is_0x41);
    RUN(test_resume_disable_value_is_0x40);
    RUN(test_double_pause_keeps_state);
    RUN(test_double_resume_keeps_state);

    // FIFO command parsing tests
    RUN(test_cmd_pause_core_1);
    RUN(test_cmd_pause_core_0);
    RUN(test_cmd_pause_core_toggle_from_unpaused);
    RUN(test_cmd_pause_core_toggle_from_paused);
    RUN(test_cmd_pause_core_extra_whitespace);
    RUN(test_cmd_pause_core_no_match);
    RUN(test_cmd_roundtrip_pause_resume);
    RUN(test_cmd_toggle_sequence);

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
