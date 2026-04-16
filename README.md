# Main_MiSTer Main Binary and Wiki Repo

This repo serves as the home for the MiSTer Main binaries and the Wiki.

For the purposes of getting google to crawl the wiki, here's a link to the (not for humans) [crawlable wiki](https://github-wiki-see.page/m/MiSTer-devel/Wiki_MiSTer/wiki)

If you're a human looking for the wiki, that's [here](https://github.com/MiSTer-devel/Wiki_MiSTer/wiki)

## Launcher Configuration

The game launcher is configured via `MiSTer.ini` (under `[MiSTer]`) or `launcher.cfg`. Key options:

| Key | Default | Description |
|-----|---------|-------------|
| `LAUNCHER` | `1` | Enable the game launcher UI |
| `LAUNCHER_PATH` | `/media/fat/launcher` | Launcher data directory (cache, covers, themes, state) |
| `LAUNCHER_GAMES_PATH` | `/media/fat/games` | Root directory for ROM files — one subdirectory per system, e.g. `games/SNES/` |
| `LAUNCHER_PARTICLES` | `1` | Particle background animation (set to `0` to save ~2 ms/frame) |
| `LAUNCHER_THEME` | `dark` | Theme name; must match a subdirectory under `{LAUNCHER_PATH}/themes/` |

Covers are loaded from `<launcher_path>/covers/<System>/<stem>.jpg` (or `.png`). Drop image files in there to make them show up — there is no auto-download.

## Building

To compile this application, read more about that [here](https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/#general-prerequisites-for-arm-cross-compiling)

### Build Options

**Standard Build:**
```bash
make
```

**Debug Build (no optimizations, with debug symbols):**
```bash
make DEBUG=1
```

**Build with Profiling (responsiveness measurement):**
```bash
make PROFILING=1
```

When built with `PROFILING=1`, the OSD responsiveness can be measured during fast menu navigation. Performance spikes will be reported to stdout showing nested timing information.

## Hardware Optimization

The Makefile includes explicit Cortex-A9 tuning for the DE10-Nano / Cyclone V SoC:
- `-mcpu=cortex-a9` — CPU model scheduling
- `-mfpu=neon-vfpv3` — NEON SIMD support
- `-mfloat-abi=hard` — Hard floating-point ABI
- `-ftree-vectorize` — Auto-vectorization for the actual hardware

These flags enable efficient use of the hardware NEON vector units and are applied to both debug and release builds.
