#ifndef LAUNCHER_IO_H
#define LAUNCHER_IO_H

#include "launcher.h"

/* core map lookup */
const CoreMapEntry *launcher_find_core(const char *system);

/* library */
void           launcher_free_library(LauncherSystem *systems, int count);
/* Progress of a library load in flight. Best-effort — values update from
   the loader thread, read from the UI thread for a splash progress bar.
   `total` is 0 until the CSV directory has been enumerated. */
void           launcher_load_progress(int *done_out, int *total_out);
/* launcher_load_progress_set: called ONLY from the loader thread
   (lib_loader_thread in launcher.cpp). The setter and getter read/write
   volatile ints without a lock — best-effort snapshots only. Do not call
   from other threads. */
void           launcher_load_progress_set(int done, int total);

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


#endif /* LAUNCHER_IO_H */
