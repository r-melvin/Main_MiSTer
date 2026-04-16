#ifndef LAUNCHER_LAUNCH_H
#define LAUNCHER_LAUNCH_H

#include "launcher.h"

void  launcher_cache_path(const LauncherGame *game, const char *base_dir,
                          char *out, size_t out_sz);
bool  launcher_start_download(const LauncherGame *game, const char *base_dir);
int   launcher_poll_download(const LauncherGame *game, const char *base_dir);
void  launcher_cancel_download(void);
const char *launcher_download_error(void);
const char *launcher_mgl_error(void);

bool  launcher_write_mgl(const LauncherGame *game);
void  launcher_load_core(const char *mgl_path);

/* Test-only: override /media/fat root used by find_rbf and MGL writer. */
extern "C" void launcher_launch_set_root(const char *root);

#endif
