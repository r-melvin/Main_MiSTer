# Main_MiSTer Main Binary and Wiki Repo

This repo serves as the home for the MiSTer Main binaries and the Wiki.

For the purposes of getting google to crawl the wiki, here's a link to the (not for humans) [crawlable wiki](https://github-wiki-see.page/m/MiSTer-devel/Wiki_MiSTer/wiki)

If you're a human looking for the wiki, that's [here](https://github.com/MiSTer-devel/Wiki_MiSTer/wiki)

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
