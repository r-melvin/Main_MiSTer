#ifndef LAUNCHER_IO_H
#define LAUNCHER_IO_H

#include "launcher.h"

/* core map lookup */
const CoreMapEntry *launcher_find_core(const char *system);

/* library */
/* Scan the remote SFTP server and generate/refresh CSV files in base_dir/lists/.
   Skips systems whose CSV is less than 24 hours old.  No-op if SFTP not configured.
   Called automatically from lib_loader_thread before launcher_load_library(). */
bool           launcher_scan_remote(const char *base_dir);
bool           launcher_load_library(const char *base_dir, LauncherSystem **out, int *count_out);
void           launcher_free_library(LauncherSystem *systems, int count);

/* state */
bool           launcher_load_state(const char *path, LauncherState *st);
bool           launcher_save_state(const char *path, const LauncherState *st);
void           launcher_state_record_played(LauncherState *st, const LauncherGame *g, const char *state_path);
bool           launcher_state_is_favourite(const LauncherState *st, const LauncherGame *g);
void           launcher_state_toggle_fav(LauncherState *st, const LauncherGame *g, const char *state_path);

/* virtual systems */
LauncherSystem *launcher_build_all_systems(const LauncherSystem *real, int real_count,
                                            const LauncherState *st, int *total_out);
void            launcher_free_virtual_systems(LauncherSystem *all, int total, int real_count);

/* ROM download */
void           launcher_cache_path(const LauncherGame *game, const char *base_dir, char *out, size_t out_sz);
bool           launcher_start_download(const LauncherGame *game, const char *base_dir);
int            launcher_poll_download(const LauncherGame *game, const char *base_dir);
void           launcher_cancel_download(void);

/* MGL / launch */
bool           launcher_write_mgl(const LauncherGame *game);
void           launcher_load_core(const char *mgl_path);

/* cover cache */
void           launcher_cover_cache_tick(void);
Imlib_Image    launcher_cover_get(const char *path);
uint32_t       launcher_cover_fade_alpha(const char *path);

#endif /* LAUNCHER_IO_H */
