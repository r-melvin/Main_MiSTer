#ifndef LAUNCHER_SCAN_H
#define LAUNCHER_SCAN_H

#include "launcher.h"

/* Returns a NULL-terminated array of lowercase extensions accepted for this
   system, e.g. for "SNES" returns {"sfc", "smc", NULL}. Returns NULL for
   unknown systems. Case-insensitive match; caller owns nothing. */
const char * const *launcher_rom_extensions(const char *system);

/* Walk games_dir/<system>/ recursively and fill *out with every ROM whose
   extension is in the allowlist. Derives cover_path from covers_dir/<system>/
   <stem>.jpg|png if present, empty string otherwise.

   Returns the number of games found (0 on empty dir, -1 if games_dir/<system>
   does not exist or cannot be opened). Caller must free(*out).

   Sort order: alphabetical by name. */
int launcher_scan_system(const char *system,
                         const char *games_dir,
                         const char *covers_dir,
                         LauncherGame **out);

/* Load a previously saved library snapshot. Returns true and fills
   *systems_out / *count_out only if:
     - the file exists and has our magic + version
     - the stored games_dir matches the current games_dir
     - every system directory's mtime matches the stored mtime

   Returns false (and sets *systems_out = NULL, *count_out = 0) on any
   mismatch or I/O error so the caller can rescan. Caller owns the
   returned systems array and must free via launcher_free_library. */
bool launcher_scan_cache_load(const char *cache_path,
                              const char *games_dir,
                              LauncherSystem **systems_out, int *count_out);

/* Write the current library state so the next boot can skip the scan.
   Writes atomically via a .tmp + rename. Non-fatal on failure. */
bool launcher_scan_cache_save(const char *cache_path,
                              const char *games_dir,
                              const LauncherSystem *systems, int count);

#endif /* LAUNCHER_SCAN_H */
