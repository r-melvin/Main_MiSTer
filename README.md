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

## Remote ROM Library

The launcher can pull ROMs from a remote server (NAS, PC, or any Linux host) over SSH using rsync. Downloads are resumable — if interrupted the next attempt picks up where it left off rather than restarting from scratch.

### How it works

1. At launch the launcher SSHes into the remote host and lists each system directory, caching the results as CSV files locally (refreshed every 24 hours).
2. When you select a game the ROM is downloaded to a local cache on the SD card before loading.
3. The launcher **only ever reads** from the remote server — it never writes, modifies, or deletes remote files.

### Server-side setup

The recommended approach is a dedicated read-only SSH user. The user has no write permission to the games directory at the filesystem level, so even if SSH auth were compromised no files could be altered.

**1. Create a dedicated user on the server:**
```bash
sudo useradd -r -s /bin/bash mister-roms
```

**2. Set up the games directory with read-only access for that user:**
```bash
# Give mister-roms read + execute on the games tree, no write anywhere
sudo setfacl -R -m u:mister-roms:r-x /path/to/games
sudo setfacl -R -d -m u:mister-roms:r-x /path/to/games
```
If your server does not have ACL support, use group permissions instead:
```bash
sudo chown -R root:mister-roms /path/to/games
sudo chmod -R 750 /path/to/games
```

**3. Generate an SSH key pair (run this on your PC or MiSTer):**
```bash
ssh-keygen -t ed25519 -f mister_roms_key -N ""
# Creates: mister_roms_key  (private — goes on MiSTer)
#          mister_roms_key.pub  (public — goes on server)
```

**4. Install the public key on the server with connection restrictions:**

Append to `/home/mister-roms/.ssh/authorized_keys`:
```
no-port-forwarding,no-X11-forwarding,no-agent-forwarding,no-pty ssh-ed25519 AAAA... mister-roms
```
The `no-pty` and forwarding restrictions prevent interactive logins and tunnelling while still allowing SSH commands and rsync.

**5. Copy the private key to the MiSTer:**
```bash
scp mister_roms_key root@mister:/media/fat/launcher/mister_roms_key
chmod 600 /media/fat/launcher/mister_roms_key  # on the MiSTer
```

### Remote directory layout

Organise ROMs on the server by system name matching MiSTer core names (case-sensitive):
```
/path/to/games/
├── SNES/
├── NES/
├── GBA/
├── GB/
├── GBC/
├── Genesis/
├── MegaDrive/
├── PSX/
├── N64/
└── ...
```
See `launcher.cfg.example` for the full list of supported system names.

### MiSTer.ini configuration

Add the following to `/media/fat/MiSTer.ini`. See `launcher.cfg.example` for all available options.

```ini
[MiSTer]
LAUNCHER=1
LAUNCHER_PATH=/media/fat/launcher

; Remote server connection (SSH key auth recommended — avoid SFTP_PASS in plain text)
SFTP_HOST=192.168.1.100
SFTP_USER=mister-roms
SFTP_KEY_PATH=/media/fat/launcher/mister_roms_key
SFTP_BASE_PATH=/path/to/games
SFTP_PORT=22
```

Password auth is supported via `SFTP_PASS` but SSH key auth is strongly recommended — passwords stored in `MiSTer.ini` are plain text.

---

## Hardware Optimization

The Makefile includes explicit Cortex-A9 tuning for the DE10-Nano / Cyclone V SoC:
- `-mcpu=cortex-a9` — CPU model scheduling
- `-mfpu=neon-vfpv3` — NEON SIMD support
- `-mfloat-abi=hard` — Hard floating-point ABI
- `-ftree-vectorize` — Auto-vectorization for the actual hardware

These flags enable efficient use of the hardware NEON vector units and are applied to both debug and release builds.
