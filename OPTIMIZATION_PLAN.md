# MiSTer Optimisation & Launcher UI Plan

## Context

The MiSTer FPGA Linux companion app is a C/C++ project cross-compiled for ARM Cortex-A9 (DE10-Nano). Analysis identified performance issues, build system problems, security defects (world-writable file permissions, plaintext FTP in build scripts, suppressed compiler warnings), launcher UI layout bugs, sluggish transitions, ROM cache management needs, and design improvements. The plan also covers visual polish, toolchain/library evaluation, and documentation updates. Items are ordered strictly by priority — each phase should be completed and tested before moving to the next.

**Key constraint**: All Makefile changes must work both with the vendored cross-compiler locally AND with native ARM GCC on the MiSTer device itself.

**Prioritisation criteria**: Items are ordered by a combination of (1) whether they block other work, (2) user-visible impact, (3) effort required, and (4) logical dependency order. Low-effort/high-impact items are pulled forward; high-effort/low-impact items are pushed back.

---

## Phase 1: Critical — Build, Security & Visible Bugs

*These are broken now, are security defects, or block further work.*

**Status: 1.1-1.8, 1.10, 2.1-2.4 COMPLETED (not exhaustively tested — local build passes, 38/38 unit tests pass, not tested on hardware). 1.9 PENDING.**

> ⚠️ **Testing caveat**: All completed items have been verified to compile cleanly with zero warnings and pass the full unit test suite (`tests/`). They have **not** been run interactively on the preview app or on MiSTer hardware. Functional testing on both is required before merging to master.

### 1.1 Fix Makefile toolchain detection (cross-compile + native)
**Files**: `Makefile` (lines 7-12), `build.sh` (lines 11-14)
**Problem**: Makefile hardcodes `TOOLCHAIN_DIR` to local cross-compiler path, breaking native builds on the MiSTer.
**Fix**: Conditional detection — use vendored toolchain if it exists, else fall back to PATH:
```makefile
BASE    = arm-none-linux-gnueabihf
TOOLCHAIN_BIN = $(wildcard $(CURDIR)/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/$(BASE)-gcc)

ifneq ($(TOOLCHAIN_BIN),)
    TOOLCHAIN_DIR = $(CURDIR)/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin
    CC    = $(TOOLCHAIN_DIR)/$(BASE)-gcc
    LD    = $(TOOLCHAIN_DIR)/$(BASE)-ld
    STRIP = $(TOOLCHAIN_DIR)/$(BASE)-strip
else
    CC    = $(BASE)-gcc
    LD    = $(BASE)-ld
    STRIP = $(BASE)-strip
endif
```
Also revert the `build.sh` PATH export added earlier (lines 11-14: `SCRIPT_DIR`, `TOOLCHAIN_PATH`, `export PATH`) — it's now redundant.

**Testing**:
1. Run `make clean && make` — must compile successfully with vendored toolchain
2. Temporarily rename `gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/` to `gcc-arm-DISABLED/`, run `make clean && make` — must fail with "command not found" (proving it falls back to PATH lookup, not a hardcoded path)
3. Rename back. Run `./build.sh` — must complete without errors
4. Verify binary exists: `file bin/MiSTer` should show "ELF 32-bit LSB executable, ARM"

### 1.2 Fix build.sh: replace plaintext FTP with SFTP and remove hardcoded credentials
**File**: `build.sh` (lines 23, 26-32, 34)
**Problem**: The build/deploy script has three security issues:
1. Line 23: `plink -pw 1` — hardcoded root password in plaintext
2. Lines 26-32: Uses plaintext `ftp` to transfer the binary (unencrypted, credentials visible on network)
3. Line 34: `plink -pw 1` again with hardcoded password

**Fix**: Replace the entire deploy section:
```bash
# Use scp instead of plaintext ftp
KEY="${SCRIPT_DIR}/mister_deploy_key"

# Kill existing process
ssh -i "$KEY" -o ConnectTimeout=5 root@$HOST 'killall MiSTer' 2>/dev/null || true

# Deploy binary via encrypted SCP
scp -i "$KEY" "$BUILDDIR/MiSTer" root@$HOST:/media/fat/MiSTer

# Restart
ssh -i "$KEY" root@$HOST 'sync; PATH=/media/fat:$PATH; MiSTer >/dev/ttyS0 2>/dev/ttyS0 </dev/null &'
```
If SSH key doesn't exist, print instructions: `ssh-keygen -t ed25519 -f mister_deploy_key && ssh-copy-id -i mister_deploy_key root@$HOST`

**Testing**:
1. Generate a deploy key, copy to MiSTer device
2. Run `./build.sh` — binary deploys and launches via SCP/SSH (no FTP, no plaintext password)
3. Capture network traffic with `tcpdump` — confirm no plaintext credentials or file contents visible
4. Verify the old `plink`/`ftp` commands are fully removed

### 1.3 Fix world-writable file and directory permissions (0777)
**Files**: `file_io.cpp` (lines 458, 708, 836-847)
**Problem**: All files, directories, and shared memory are created with `S_IRWXU | S_IRWXG | S_IRWXO` (0777) — world-readable, writable, and executable.

**Fix**:
1. `file_io.cpp:708` (`FileSave`): Change to `S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH` (0644)
2. `file_io.cpp:836-847` (`MakeDir`/`PathCreate`): Change to `S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH` (0755)
3. `file_io.cpp:458` (`shm_open`): Change to `S_IRUSR | S_IWUSR` (0600)

**Testing**:
1. `make clean && make` — compiles without errors
2. Run on device — save a setting, verify `-rw-r--r--` (0644)
3. Create a new directory — verify `drwxr-xr-x` (0755)
4. Launch a core (triggers shm_open) — verify `/dev/shm/vdsk` shows `-rw-------` (0600)
5. Verify all existing functionality still works

### 1.4 Fix "Sort: A-Z" overlapping game tiles
**File**: `launcher.cpp:804` and `launcher.h:152`
**Problem**: Sort label at y=45 with ~16px font extends to y≈61, overlapping first tile row at y=60 (`LAUNCHER_HEADER_H`).
**Fix**: Increase `LAUNCHER_HEADER_H` from 60 to 75 in `launcher.h:152`. Move sort label to y=50.

**Testing**:
1. Build and run `preview/`
2. Navigate to any system's game list
3. Visually confirm "Sort: A-Z" text has clear space below it before the first row of cover tiles

### 1.5 Fix game name truncation under tiles ✅ COMPLETED (not exhaustively tested)
**File**: `launcher.cpp:765`
**Problem**: Text clipped to `LAUNCHER_COVER_W` (160px); names like "Metroid Fusion" show as "Metroid Fusio".
**Fix**: Change max_w from `LAUNCHER_COVER_W` to `LAUNCHER_CELL_W - 4`:
```cpp
launcher_draw_text_clipped(fb, x, y + LAUNCHER_COVER_H + 4,
    g_filtered[i].name, FONT_SM, LC_TEXT, LAUNCHER_CELL_W - 4);
```

**Testing**:
1. Build and run `preview/`
2. Confirm: short names render fully, medium names use clean "..." truncation, long names don't bleed into adjacent cells

### 1.6 Fix right-side metadata clipping ("Never played" cut off) ✅ COMPLETED (not exhaustively tested)
**Files**: `launcher.cpp:793-797`, `launcher_draw.cpp:475-560`, `launcher_draw.h:47`
**Problem**: `tx` computed from game name width, but metadata below can be wider, extending past screen edge.
**Fix**: Added `launcher_game_metadata_width()` helper (shared logic via `build_metadata_string()`), then use `max(tw, mw)` to set `tx`:
```cpp
int tw = launcher_text_width(g_filtered[g_game_sel].name, FONT_SM);
int mw = launcher_game_metadata_width(&g_filtered[g_game_sel]);
int max_w = (tw > mw) ? tw : mw;
int tx = g_fb_w - LAUNCHER_PAD - max_w;
```

**Testing**:
1. Build and run `preview/`
2. Select a game with a short name but long metadata — all text visible within screen bounds

### 1.7 Fix system card text clipping at screen edges ✅ COMPLETED (not exhaustively tested)
**File**: `launcher.cpp:596-607`
**Problem**: System cards partially off-screen render text that overflows the screen boundary.
**Fix**: Guard text drawing with a bounds check — only draw name/count text when the card is fully within screen bounds:

**Testing**:
1. Navigate system carousel so cards are partially visible at edges
2. Confirm text is cleanly clipped, no overflow

### 1.8 Add search result feedback ✅ COMPLETED (not exhaustively tested)
**File**: `launcher.cpp:822-841`
**Problem**: Type-to-search shows no match count or "No results" message.
**Fix**: Search bar now shows `"Search: mario|  (3 / 47)"` with total from `g_all_sys[g_sys_sel].game_count`. Shows `"No matches"` in `LC_ERR` (red) when `g_filtered_cnt == 0`.

**Testing**:
1. Type a search term — result count appears
2. Type a term with no matches — "No matches" shown in red
3. Clear search — count disappears, full list restored

### 1.9 Improve error messages with specific context
**Files**: `launcher.cpp:1896,1900,1624`, `launcher_io.cpp:740-743`
**Problem**: Generic error messages ("Failed to launch game", "Download failed", "SSH error") give users no way to troubleshoot.
**Fix**: Include errno, core path, network state, and auth failure details. Redirect child stderr to a pipe instead of `/dev/null` (at `launcher_io.cpp:263`) to capture SSH error output.

**Testing**:
1. Misconfigure SFTP host — error says "Host unreachable: {host}"
2. Wrong SSH key — "Authentication failed"
3. Disk full — "Disk full"
4. Missing core — "Core not found: {path}"

### 1.10 Fix splash screen bottom clipping ✅ COMPLETED (not exhaustively tested)
**File**: `launcher.cpp` — `draw_splash()` lines 550 and 557
**Problem**: Version string baseline at `g_fb_h - 18` (702px) clips at the 720px boundary — descenders overflow the framebuffer edge. Progress bar at `g_fb_h - 40` (680px) leaves only a 12px gap to the text.
**Fix**: 2-line change:
```cpp
int ty = g_fb_h - 55;  // was: g_fb_h - 40  (progress bar: 665px, 55px bottom margin)

launcher_draw_text_centred(fb, cx, g_fb_h - 32, "v2 | MiSTer FPGA", FONT_SM, LC_DIM);
                                           // was: g_fb_h - 18  (version string: 688px, 32px bottom margin)
```

**Testing**:
1. Run `./preview/launcher_preview` — version string fully visible with clear bottom margin
2. Progress bar and version string no longer touch the window edge
3. `make -C preview` — zero warnings
4. `make -C tests && ./tests/test_*` — 38/38 pass

---

## Phase 2: High Priority — Performance & Core Infrastructure

*Performance fixes, build optimisations, and infrastructure that later phases depend on. Ordered so that quick wins come first, complex items after.*

### 2.1 Change default theme to neutral dark grey ✅ COMPLETED (not exhaustively tested)
**Files**: `launcher_io.cpp:68-85`, `launcher.cpp:584-602`
**Impact**: High (every user sees the theme). **Effort**: Low (constant swap).
**Problem**: Purple/indigo tint across all colours. Hardcoded `0xFF505069u` for selected system card.
**Fix**: Replace palette in `theme_set_defaults()`:
```cpp
t->bg            = 0xFF141417u;  /* near-black charcoal */
t->card          = 0xFF2A2A2Eu;  /* dark grey card */
t->hi            = 0xFFE8A824u;  /* warm amber highlight */
t->fav           = 0xFFE89820u;  /* amber favourite */
t->text          = 0xFFD4D4D8u;  /* light grey text */
t->dim           = 0xFF6E6E78u;  /* mid-grey secondary */
t->bar           = 0xFF1E1E22u;  /* dark grey header/footer */
t->overlay       = 0xD2000000u;  /* unchanged */
t->err           = 0xFFDC3C3Cu;  /* unchanged */
t->search        = 0xFF262629u;  /* dark grey search bar */
```
At `launcher.cpp:591`, derive selected card colour from theme:
```cpp
uint32_t sel_card = LC_CARD;
uint32_t r = ((sel_card >> 16) & 0xFF) + 0x18;
uint32_t g = ((sel_card >> 8)  & 0xFF) + 0x18;
uint32_t b = ( sel_card        & 0xFF) + 0x18;
if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
uint32_t card_sel_col = 0xFF000000u | (r << 16) | (g << 8) | b;
```

**Testing**:
1. Visually confirm: dark charcoal background, neutral grey cards, warm amber highlights
2. Custom theme files still override defaults
3. All screens: no purple remnants

### 2.2 Speed up transitions (increase fade speed)
**File**: `launcher.h:156`
**Impact**: High (user-reported sluggishness). **Effort**: Trivial (one constant).
**Fix**: Change `LAUNCHER_FADE_SPEED` from 10 to 40. ~234ms total transition.

**Testing**:
1. Switch between system selector and game grid — feels nearly instant but still smooth

### 2.3 Remove O_SYNC from FileSave()
**File**: `file_io.cpp:708`
**Impact**: High (removes I/O stalls). **Effort**: Trivial (one flag).
**Fix**: Remove `O_SYNC` from `O_WRONLY | O_CREAT | O_TRUNC | O_SYNC`.

**Testing**:
1. Save a setting — no noticeable lag
2. Power cycle — setting persists
3. Rapidly save 5 times — no stall

### 2.4 Remove debug printf() from hot paths
**File**: `file_io.cpp` — lines 195-196, 711, 720, 921
**Impact**: Medium. **Effort**: Trivial.
**Fix**: Wrap in `#ifdef DEBUG` or remove.

**Testing**:
1. `make clean && make` — no warnings
2. No visible behaviour change on device

### 2.5 Fix O(n²) string padding in OSD menu rendering
**Files**: `menu.cpp` — lines 2147, 2858, 3124, 3135, 3145, 3155, 3166, 3176
**Impact**: Medium (causes timing spikes in menus). **Effort**: Low.
**Fix**: Replace `while (strlen(s) < N) strcat(s, " ");` with `memset()`.

**Testing**:
1. Navigate OSD menus — text correctly padded, no timing spikes

### 2.6 Re-enable suppressed compiler warnings and fix underlying issues
**File**: `Makefile` (line 55)
**Impact**: Medium (code quality gate). **Effort**: Medium (must fix each warning). **Dependency**: Do after 2.5 which fixes the main strcat warnings.
**Fix**: Remove `-Wno-stringop-overflow`, `-Wno-stringop-truncation`, `-Wno-format-truncation`. Fix each resulting warning.

**Testing**:
1. `make clean && make` — zero warnings
2. All menus, file browser, launcher render correctly

### 2.7 Enable LTO and dead-code elimination
**File**: `Makefile` (lines 55-58, 63)
**Impact**: Medium (5-10% binary size reduction). **Effort**: Low (flag change).
**Fix**: Add `-ffunction-sections -fdata-sections -flto` to CFLAGS, `-flto -Wl,--gc-sections` to LFLAGS.

**Testing**:
1. `make clean && make` — compiles and links without errors
2. Binary size reduced
3. All functionality works (LTO can expose aliasing bugs)

### 2.8 Dirty flag to skip redundant redraws
**File**: `launcher.cpp`
**Impact**: High (major CPU reduction when idle). **Effort**: Medium.
**Fix**: Add `static bool g_dirty = true;`. Set on input, scroll, cover load, fade, state change. Skip full redraw when clean.

**Testing**:
1. With profiling overlay, frame time drops to near-zero when idle
2. Keypresses still respond immediately
3. Smooth scrolling unaffected

### 2.9 Add frame-rate limiting to launcher main loop
**File**: `launcher.cpp:1565+`
**Impact**: High (CPU/power savings). **Effort**: Low. **Complements**: 2.8 (dirty flag skips redraws, frame limiter caps polling).
**Fix**: Sleep for remainder of 16.6ms budget after each frame via `clock_nanosleep()`.

**Testing**:
1. Frame rate capped at ~60fps during scrolling
2. CPU usage drops significantly when idle
3. Input responsiveness unchanged

### 2.10 Add remote library sync toggle (disabled by default)
**Files**: `cfg.h`, `cfg.cpp`, `launcher_io.cpp:703-747`, settings screen
**Impact**: High (security — no unexpected outbound connections). **Effort**: Medium.
**Problem**: Launcher attempts SSH/SFTP on every startup. Should be opt-in.
**Fix**: Add `cfg.launcher_remote_enabled` (default `false`). Guard `launcher_scan_remote()`, `launcher_start_download()`. Add toggle to settings.

**Testing**:
1. Fresh install — no SSH connections attempted
2. Enable in settings — remote scan triggers
3. Disable — systems from last scan remain, no new connections
4. Cover art scraping from TheGamesDB still works regardless

### 2.11 Automatic ROM cache management with LRU eviction
**Files**: `launcher_io.cpp` (new function), `launcher_io.h`, `launcher.cpp`
**Impact**: High (prevents disk-full). **Effort**: High. **Dependency**: 2.10 (remote sync toggle).

**Critical constraint**: Automatic eviction ONLY runs when remote sync is enabled. When remote sync is off, cached ROMs are permanent local files — not a disposable cache.

**Fix**: Add `launcher_cache_evict()`:
```cpp
int launcher_cache_evict(const char *base_dir, const LauncherState *state, int max_age_days);
```

**Algorithm**:
1. **Guard**: If `!cfg.launcher_remote_enabled` → return 0. No automatic eviction when remote sync is off.
2. Check disk usage with `statvfs()` — if < 90% → return 0
3. Scan `{base_dir}/cache/` recursively, build LRU list by `last_played`
4. Filter: only files not played in `max_age_days`
5. Delete oldest first until usage < 85% (5% hysteresis)
6. Log each deletion

**Safety**: Never touch files outside cache/. Never delete active game. Never delete `.part` files. When user disables remote sync, show notice about manual cache management.

**Configurable**: `CACHE_EVICT_PCT` (90), `CACHE_EVICT_TARGET_PCT` (85), `CACHE_MAX_AGE_DAYS` (30).

**Testing**:
1. Remote sync OFF, disk 95% — no files deleted
2. Remote sync ON, disk 92% — LRU eviction works
3. Never-played files evicted first
4. Age filtering respected
5. Files outside cache/ untouched
6. Active game protected
7. Toggle off mid-session — eviction stops, notice shown

### 2.12 Dynamic system discovery from installed cores + remote ROM directories
**Files**: `launcher_io.cpp:29-54`, `launcher_io.cpp:703-747`, `launcher_io.cpp:1048-1104`
**Impact**: High (removes 23-system limit). **Effort**: High. **Dependency**: 2.10 (remote sync guard).

**Fix — three-phase discovery**:
- **Phase A**: SSH directory listing of remote base path (replaces hardcoded iteration)
- **Phase B**: Scan `/media/fat/_Console/`, `_Computer/`, `_Arcade/` for `*.rbf` files. Match systems to cores (hint table + case-insensitive stem match). Systems with ROMs but no core show "No core installed" badge.
- **Phase C**: Optional `{base_dir}/core_map.cfg` for user-defined mappings

**Testing**:
1. Known remote systems appear in carousel
2. Unknown system (e.g., WonderSwan) appears with "No core installed" badge
3. Custom `core_map.cfg` overrides
4. No SFTP configured — CSV-based systems still work
5. All 23 hardcoded systems backward-compatible

### 2.13 Expand settings menu
**Files**: `launcher.cpp:2041-2083`, `launcher_draw.cpp:548-610`
**Impact**: Medium (framework for all future settings). **Effort**: Medium.
**Fix**: Scrollable list with categories: Display (resolution, theme, font size, particles, perf overlay), Remote Library (sync toggle, host/user display), Cache (age, threshold), Back.

**Testing**:
1. All categories visible, scrollable
2. Each setting cycles correctly
3. Changes persist across restart

### 2.14 Show network status and disk space indicators
**Files**: `launcher.cpp` (footer), `launcher_draw.cpp`
**Impact**: Medium (situational awareness). **Effort**: Low.
**Fix**: Footer bar: network status circle (read `/sys/class/net/eth0/operstate`, update 1/sec), disk space text (update every 10sec, warning colour below 10%).

**Testing**:
1. Network indicator reflects actual state
2. Disk space matches `df -h`
3. Warning colour at low disk

---

## Phase 3: Medium Priority — UI Redesign & Visual Polish

*Major UX improvements and visual polish that make the launcher feel premium. Grouped together because visual items should be designed as a cohesive pass, not piecemeal.*

### 3.1 Default to 720p framebuffer with resolution setting
**Files**: `launcher.cpp:174-177`, settings screen, `launcher.h`
**Impact**: High (covers too small at 1080p for TV). **Effort**: Medium.
**Fix**: Default to 1280x720. Add Resolution option to settings (720p, 1080p, 480p). Persist choice.

**Testing**:
1. Default window is 1280x720
2. Each resolution takes effect, grid columns adjust
3. Setting persists across restart

### 3.2 Scale layout constants to resolution
**Files**: `launcher.h:146-160`, `launcher.cpp`
**Impact**: High (makes multi-resolution work properly). **Effort**: Medium. **Dependency**: 3.1.
**Fix**: `ui_scale() = g_fb_h / 720.0f`. Apply to cover/cell dimensions, padding, header/footer, font sizes. `LayoutMetrics` struct.

**Testing**:
1. 720p: identical to current (scale=1.0)
2. 1080p: proportionally larger, ~7 columns
3. 480p: smaller, ~5 columns, still readable

### 3.3 Consistent spacing grid and header/footer separators
**Files**: `launcher.h` (constants), `launcher.cpp` (draw functions)
**Impact**: Medium (visual coherence). **Effort**: Low.
**Fix**:
1. Align all padding/margin values to an 8px base grid (scaled): `PAD=16, HEADER_H=64, FOOTER_H=48, CELL_GAP=8`
2. Add 1px separator lines between header/content and content/footer using `LC_DIM` at 30% alpha
3. Standardise: all vertical spacing between text elements is a multiple of 4px (scaled)

**Testing**:
1. Visual inspection: consistent gutters between all elements
2. Header and footer visually separated from content without being heavy
3. Spacing holds at all three resolutions

### 3.4 Cover art drop shadows
**File**: `launcher.cpp` (tile draw, ~line 760)
**Impact**: Medium (depth and visual quality). **Effort**: Low.
**Fix**: Before drawing each cover, draw a semi-transparent dark rounded rect offset 3px down and 2px right:
```cpp
launcher_fill_rect_rounded(fb, x + 2, y + 3, LAUNCHER_COVER_W, LAUNCHER_COVER_H, 4, 0x40000000u);
// then draw cover on top at (x, y)
```

**Testing**:
1. Covers appear to float above background
2. Shadows don't overlap adjacent tiles
3. Placeholder covers also have shadows
4. No visual artefacts at grid edges

### 3.5 Selected tile scale and highlight animation
**File**: `launcher.cpp` (tile draw), `launcher_draw.cpp`
**Impact**: High (selection feel — most console UIs do this). **Effort**: Medium.
**Fix**: When a tile is selected:
1. Draw cover at 108% size (centred on original position) using `launcher_blit_image()` with larger target dimensions
2. Draw 2px border in highlight colour around the scaled cover
3. Animate scale from 100%→108% over ~100ms using lerp (track per-tile `g_sel_scale` float)
4. Existing glow effect (`launcher_draw_glow`) surrounds the scaled cover

**Testing**:
1. Navigate game grid — selected tile visibly larger than neighbours
2. Transition between tiles has smooth scale animation
3. Edge tiles don't overflow screen when scaled
4. Scale works correctly at all resolutions

### 3.6 Smooth cover art fade-in on load
**File**: `launcher.cpp` (cover draw), `launcher.h` (CoverEntry struct)
**Impact**: Medium (removes jarring pop-in). **Effort**: Low.
**Fix**: Add `uint8_t alpha` to `CoverEntry` (init to 0). Each frame, increase alpha by 20 (0→255 in ~13 frames / ~200ms). Draw cover with alpha using `launcher_draw_fade()` pattern or pre-multiply alpha on blit.

**Testing**:
1. Navigate to a system — covers fade in smoothly as they load from disk
2. Already-cached covers appear instantly (alpha already 255)
3. Placeholder → real cover transition is a crossfade, not a hard swap

### 3.7 Scroll position indicator
**File**: `launcher.cpp` (draw_games)
**Impact**: Medium (spatial awareness in long lists). **Effort**: Low.
**Fix**: Draw a thin vertical scrollbar (4px wide, right edge) showing current position in the game list:
```cpp
int bar_h = max(20, (visible_rows * track_h) / total_rows);
int bar_y = (g_scroll_y * track_h) / (total_rows * LAUNCHER_CELL_H);
launcher_fill_rect_rounded(fb, g_fb_w - 6, header_h + bar_y, 4, bar_h, 2, LC_DIM);
```
Fade out after 2 seconds of no scrolling (reduce alpha each frame).

**Testing**:
1. Short list (fits on screen) — no scrollbar shown
2. Long list — scrollbar visible during scroll, fades out when idle
3. Scrollbar position accurately reflects current position
4. Works at all resolutions

### 3.8 System carousel depth effect
**File**: `launcher.cpp:562-610` (system card draw)
**Impact**: Medium (premium carousel feel). **Effort**: Medium.
**Fix**: Centre card at full size and opacity. Adjacent cards: scale to 90%, dim to 70% opacity. Cards two positions away: scale to 80%, dim to 50%. Interpolate during scroll animation for smooth transitions.

**Testing**:
1. Centre card is clearly the focus (larger, brighter)
2. Scrolling between systems: smooth size/opacity transitions
3. Edge cards are smaller and dimmer — creates 3D depth
4. Cards scale correctly at all resolutions

### 3.9 Empty state designs
**Files**: `launcher.cpp` (draw_games, draw_systems)
**Impact**: Medium (first-run experience, intentional design). **Effort**: Low.
**Fix**: Design dedicated empty states:
- **No games in system**: Centred icon (simple geometric shape using rects/ellipses) + "No games found" + hint text ("Enable Remote Library in Settings" or "Add ROMs to {path}")
- **No search results**: "No matches for '{query}'" + "Try a shorter search term"
- **No favourites**: Star icon + "No favourites yet" + "Press F on a game to add it"
- **No systems**: "No systems found" + setup instructions

**Testing**:
1. System with no games — empty state with helpful text shown (not blank screen)
2. Search with no matches — empty state shown
3. Favourites filter with none — empty state shown
4. Fresh install with no CSVs — no systems empty state

### 3.10 Retro FPGA chip loading screen
**File**: `launcher.cpp:526-558` (`draw_splash()`)
**Impact**: Medium (first impression, brand identity). **Effort**: Medium.
**Fix**: Procedurally drawn retro chip graphic with animated circuit traces. Uses existing primitives.

**Testing**:
1. Chip centred, pins visible on all sides, traces extend outward
2. Animation: dots pulse along traces smoothly
3. Smooth fade to system selector after ~1800ms

### 3.11 Info sidebar for selected game
**File**: `launcher.cpp:685-831`
**Impact**: High (replaces cramped header metadata). **Effort**: High.
**Fix**: Right-side panel (240px scaled): large cover, game name, system, metadata (play time, last played, rating stars, file size, favourite status). Grid width reduces by panel width.

**Testing**:
1. Sidebar appears with selected game details
2. All metadata fields populated
3. No cover → placeholder shown
4. Never played → "Never played" in metadata
5. Grid columns adjust correctly

### 3.12 Display user ratings on tiles and sidebar + sort by rating
**Files**: `launcher.cpp:760-768`, `launcher_draw.cpp:979-1036`, `launcher.cpp:387-431`
**Impact**: Medium (completes an existing half-visible feature). **Effort**: Low.
**Fix**:
1. Tiles: small star dots below name (right-aligned) if rating > 0
2. Sidebar: full 5-star display with filled/empty stars
3. Add "Rating" to sort cycle — descending by rating, unrated last

**Testing**:
1. Rated games show stars on tiles
2. Sidebar shows full star display
3. Sort by Rating works, unrated games sort last

### 3.13 Breadcrumb navigation header
**File**: `launcher.cpp:770-804`
**Impact**: Medium (navigation clarity). **Effort**: Low.
**Fix**: `[← B] Systems › GBA          3 / 47    Sort: A-Z`

**Testing**:
1. Breadcrumb shows current system with back hint
2. With favourites filter: `[← B] Systems › GBA [★ Favourites]`

### 3.14 Slide transition (systems ↔ games)
**File**: `launcher.cpp:199-226`
**Impact**: Medium (UI feel). **Effort**: Medium. **Dependency**: 2.2 (fade speed) provides fallback.
**Fix**: Systems slide up/off, games slide in from below. ~200ms lerp. Fade as fallback for other transitions.

**Testing**:
1. Enter system — carousel slides up, grid slides in
2. Press B — reverse animation
3. No glitches during transition
4. Settings/help/error still use fade

### 3.15 Smooth scroll with momentum/inertia
**File**: `launcher.cpp:1601-1605`
**Impact**: Medium (natural scroll feel). **Effort**: Medium.
**Fix**: Velocity tracking + friction deceleration (`g_scroll_vel *= 0.92` per frame). Fast flicking scrolls further.

**Testing**:
1. Hold D-pad, release — list decelerates naturally
2. Quick tap — moves one row, no overshoot
3. Stops cleanly at boundaries

### 3.16 Audio feedback for navigation
**Files**: `launcher.cpp` (input handlers), new `launcher_audio.cpp`
**Impact**: Medium (tactile feel — all console UIs have this). **Effort**: Medium.
**Fix**:
1. Generate short PCM samples procedurally (sine wave blips) — no external audio files needed
2. Navigation tick: ~5ms 1kHz blip on cursor move
3. Select confirm: ~20ms rising tone on A press
4. Back: ~15ms falling tone on B press
5. Error: ~50ms low buzz on invalid action
6. Play via ALSA `snd_pcm_writei()` on a background thread (non-blocking)
7. Master volume setting in settings menu (Off / Low / Medium / High)

**Testing**:
1. Navigate game grid — soft tick on each move
2. Select a game — confirm sound plays
3. Press B — back sound plays
4. Set volume to Off — no sounds
5. Audio doesn't cause frame drops (non-blocking playback)

---

## Phase 4: Lower Priority — Further Performance, Features & Polish

### 4.1 NEON SIMD for pixel alpha blending
**File**: `launcher_draw.cpp:37-52`
**Fix**: `blend_row_neon()` processing 4 pixels/iteration.

**Testing**:
1. All alpha-blended elements render correctly, no colour artefacts

### 4.2 Multi-entry ZIP file cache
**File**: `file_io.cpp:47-62`
**Fix**: 4-entry LRU array.

**Testing**:
1. No lag spikes switching between ZIP directories
2. Oldest evicted correctly, no leaks

### 4.3 Cache stat() results during directory scans
**File**: `file_io.cpp` (lines 174-179, 228-236, 500-506)
**Fix**: Hash map of path→stat, invalidated on directory change.

**Testing**:
1. 1000+ file directory navigates faster
2. External file changes reflected on re-enter

### 4.4 System card pixel-art icons
**File**: `launcher.cpp:562-610`
**Fix**: Procedural icons via rect/ellipse lookup table. Generic chip for unknowns.

**Testing**:
1. Each known system has distinct icon
2. Unknown systems show generic icon
3. Icons scale with resolution

### 4.5 "Recently Played" row
**File**: `launcher.cpp:685-831`
**Fix**: Pinned row at top of grid (when sort=A-Z) showing last `cols` played games.

**Testing**:
1. Row appears after playing 3+ games
2. Most recent first
3. Row hidden when sort is "Recent" (redundant)

### 4.6 Complete multi-select with bulk operations
**Files**: `launcher.cpp:1730-1768`, `launcher.cpp:117`
**Impact**: Medium (power users). **Effort**: Medium.
**Fix**: Context menu after selection: Bulk Favourite, Bulk Download Covers, Bulk Delete from Cache.

**Testing**:
1. Multi-select mode visual indicator shown
2. Selected games show checkmark
3. Bulk Favourite toggles all
4. Bulk Delete removes cached ROMs

### 4.7 Screensaver / idle dimming
**File**: `launcher.cpp`, `launcher_draw.cpp`
**Fix**: Track last input. 5min → dim (30% overlay). 10min → black. Any input restores. Configurable in settings.

**Testing**:
1. Screen dims after idle timeout
2. Goes black after extended idle
3. Any button restores immediately
4. "Off" setting disables

### 4.8 Analog stick navigation in launcher
**Files**: `launcher.cpp`, `input.h`
**Fix**: Map stick deflection (past deadzone 0.45) to D-pad directions with same repeat timing.

**Testing**:
1. Analog stick navigates all screens
2. Deadzone prevents ghost inputs
3. Works alongside D-pad

### 4.9 Description fetch retry
**Files**: `launcher.cpp:1964-1978`, `launcher_draw.cpp:909`
**Fix**: Show "Press A to retry" on error. Reset and re-trigger on A press.

**Testing**:
1. Disconnect, open description — shows retry prompt
2. Reconnect, press A — loads successfully

### 4.10 Help screen pagination
**File**: `launcher_draw.cpp:405-471`
**Fix**: Scrollable when content overflows. Scroll indicators. L/R for page up/down.

**Testing**:
1. 720p: fits on one screen
2. 480p: scrollable with indicators
3. All keybindings accurate

---

## Phase 5: Low Priority — Marginal / Risky

### 5.1 Throttle FPGA busy-wait loops
**File**: `fpga_io.cpp:71-79` (and lines 124, 194, 209, 269, 305, 329)
**Fix**: `usleep(1)` every 1024 iterations.
**Risk**: FPGA timing-sensitive. **Must test on real hardware only.**

### 5.2 Move launcher network ops to offload thread
**File**: `launcher_io.cpp`
**Fix**: Use `offload_add_work()` for SFTP/curl. Add completion callback.

### 5.3 Convert logo.h binary data to linked object
**File**: `logo.h`, `Makefile`
**Fix**: `ld -r -b binary` pattern.

### 5.4 Evaluate GCC 13.x ARM cross-compiler
**Risk**: libc/glibc ABI compatibility with DE10-Nano.
**Fix**: Download, build, deploy, compare. Stay on 10.2 if incompatible.

### 5.5 Audit and update vendored library sources
**Files**: `lib/miniz/`, `lib/lzma/`, `lib/zstd/`, `lib/libchdr/`, `lib/md5/`, `lib/imlib2/`
**Fix**: Check upstream for CVEs, update where safe, add `lib/VERSIONS.md`.

### 5.6 Document SFTP trust-on-first-use (TOFU) model
**File**: `launcher_io.cpp:288`
**Fix**: Code comment + README documentation + optional strict mode setting.

### 5.7 Add high-contrast accessibility theme
**Fix**: Pure black/white/yellow theme variant. Add to built-in list.

### 5.8 Backup and restore settings, favourites, and play history
**Fix**: Export to JSON, import with additive merge. Settings menu options.

---

## Not Recommended

- **Splitting menu.cpp/input.cpp**: High risk of regressions, no runtime benefit
- **Replacing std::string in file browser**: Extensive rewrite for marginal gain
- **Adding -ffast-math**: Changes float precision; input deadzone calculations depend on it

---

## Phase 8: Documentation

*Documentation should be written after features land, not before. Update as each phase is completed.*

### 8.1 Update README.md with new features
**File**: `README.md`

**Fix**: Add sections for:
1. **Launcher Overview**: System carousel, game browser, search, favourites, ratings, play time, stats, multi-select
2. **Remote Library** (opt-in): Enable in settings, SFTP config in `MiSTer.ini`, SSH key setup, TOFU model
3. **ROM Cache Management**: LRU eviction (remote sync only), configurable parameters, manual deletion
4. **Dynamic System Discovery**: Auto-discovery, `core_map.cfg`, "No core installed" badge
5. **Theme System**: `themes/` directory, colour format, built-in themes (including high-contrast)
6. **Display Settings**: Resolution, font size, particles, perf overlay, screensaver
7. **In-Game Menu**: Savestates, cheats, video settings, Select+Start chord
8. **Input & Controls**: D-pad, analog stick, keyboard, gamepad, help screen, multi-select
9. **Audio**: Navigation sounds, volume settings
10. **Accessibility**: High-contrast theme, font size, screensaver
11. **Backup & Restore**: Export/import settings, favourites, ratings, play history
12. **Network Status**: Footer indicators
13. **Error Messages**: Common errors and troubleshooting
14. **Preview App**: `cd preview && make && ./launcher_preview`
15. **Deploy Script**: `build.sh` with SSH key setup

### 8.2 Add CHANGELOG.md
Use [Keep a Changelog](https://keepachangelog.com/) format.

---

## General Testing Strategy

### Build verification (run after every phase):
```bash
make clean && make
file bin/MiSTer  # expect: "ELF 32-bit LSB executable, ARM, EABI5"
size bin/MiSTer  # record text/data/bss for size tracking
cd preview && make clean && make && ./launcher_preview
```

### Preview app testing checklist:
1. **Splash screen**: Loads, animates, transitions to system selector
2. **System selector**: Cards render with depth effect, scroll smoothly, edge cards clipped
3. **Game browser**: Covers fade in, grid renders with shadows, scroll is smooth with momentum, labels not truncated
4. **Selection**: Selected tile scales up, glow visible, sidebar shows details
5. **Header/footer**: Separators visible, breadcrumb navigation, network/disk indicators
6. **Settings**: All options toggle, resolution changes take effect, theme applies, audio volume works
7. **Help screen**: Renders, scrollable at small resolutions
8. **Search**: Results filter, match count shown, empty state on no matches
9. **Favourites**: Toggle, filter, empty state when none
10. **ROM download & cache**: Download (if remote enabled), verify cache, verify eviction
11. **Empty states**: All empty states show helpful text and icons
12. **Audio**: Navigation ticks, confirm/back sounds at correct volume
13. **Performance**: FPS overlay — idle near-zero, scrolling under 16ms

### Hardware testing checklist (for phases 2+):
1. Deploy via `build.sh` (SCP, not FTP)
2. Boot MiSTer — OSD menu works
3. Enter launcher — all screens functional
4. Download a ROM (if remote enabled) — verify cache
5. Load a core — game runs
6. Return to launcher from core
7. Save settings — persist across reboot
8. Test with gamepad (D-pad + analog stick)
9. Audio output works (navigation sounds)
10. Idle for 5+ minutes — screensaver activates
11. Fill disk to 92% — verify cache eviction (remote sync on) or warning (remote sync off)

### Regression guard:
- Run existing test suite: `cd tests && make && ./run_tests.sh` (if available)
- After each phase, run the full preview app checklist
- Track binary size across phases — unexpected growth indicates a problem
