/*
 * launcher_launch.cpp
 * MGL generation, core resolution, and the local ROM cache path.
 */

#include "launcher.h"
#include "launcher_io.h"
#include "launcher_launch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *g_mister_root = "/media/fat";

/* Test hook: override the root used by find_rbf and launcher_write_mgl.
   Pass NULL to restore the production default. */
extern "C" void launcher_launch_set_root(const char *root)
{
    g_mister_root = root ? root : "/media/fat";
}

/* ─── utility: find newest RBF matching a stem pattern ───────────────────── */

static bool find_rbf(const char *dir_name, const char *stem,
                     char *out, size_t out_sz)
{
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", g_mister_root, dir_name);

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s*.rbf", stem ? stem : "");

    DIR *d = opendir(dir_path);
    if (!d) return false;

    char best[256] = {};
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (fnmatch(pattern, ent->d_name, FNM_CASEFOLD) == 0) {
            if (strcmp(ent->d_name, best) > 0)
                strncpy(best, ent->d_name, sizeof(best) - 1);
        }
    }
    closedir(d);

    if (!best[0]) return false;

    /* strip .rbf extension for MGL */
    int ln = (int)strlen(best);
    if (ln > 4 && strcasecmp(best + ln - 4, ".rbf") == 0)
        best[ln - 4] = '\0';

    snprintf(out, out_sz, "%s/%s/%s", g_mister_root, dir_name, best);
    return true;
}

/* ─── MGL generation & launch ────────────────────────────────────────────── */

static char g_mgl_error[256] = {};

bool launcher_write_mgl(const LauncherGame *game)
{
    g_mgl_error[0] = '\0';

    const CoreMapEntry *ce = launcher_find_core(game->system);
    if (!ce) {
        snprintf(g_mgl_error, sizeof(g_mgl_error),
                 "No core is configured for the '%s' system.", game->system);
        return false;
    }

    char rbf_path[512] = {};
    if (ce->stem) {
        if (!find_rbf(ce->dir, ce->stem, rbf_path, sizeof(rbf_path))) {
            snprintf(g_mgl_error, sizeof(g_mgl_error),
                     "Core '%s' not found on SD card (looked in %s/%s/).",
                     ce->stem, g_mister_root, ce->dir);
            return false;
        }
    }

    char mgl_path[1024];
    snprintf(mgl_path, sizeof(mgl_path), "%s/launch.mgl", g_mister_root);
    FILE *fp = fopen(mgl_path, "w");
    if (!fp) {
        snprintf(g_mgl_error, sizeof(g_mgl_error),
                 "Cannot write launch file: %s", strerror(errno));
        return false;
    }

    fprintf(fp, "<mistergamedescription>\n");
    if (rbf_path[0])
        fprintf(fp, "\t<rbf>%s</rbf>\n", rbf_path);
    fprintf(fp, "\t<file delay=\"2\" type=\"f\" index=\"%d\" path=\"%s\"/>\n",
            ce->file_index, game->path);
    fprintf(fp, "\t<reset delay=\"3\"/>\n");
    fprintf(fp, "</mistergamedescription>\n");
    fclose(fp);

    printf("launcher: wrote MGL to %s\n", mgl_path);
    return true;
}

void launcher_load_core(const char *mgl_path)
{
    int fd = open("/dev/MiSTer_cmd", O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[600];
        int n = snprintf(cmd, sizeof(cmd), "load_core %s\n", mgl_path);
        write(fd, cmd, n);
        close(fd);
    }
}

/* ─── ROM download ───────────────────────────────────────────────────────── */

/* Returns the local cache path for this game */
void launcher_cache_path(const LauncherGame *game, const char *base_dir,
                          char *out, size_t out_sz)
{
    const char *rom_name = strrchr(game->path, '/');
    rom_name = rom_name ? rom_name + 1 : game->path;
    snprintf(out, out_sz, "%s/cache/%s/%s", base_dir, game->system, rom_name);
}

static char g_download_error[256] = {};

const char *launcher_download_error(void) { return g_download_error; }
const char *launcher_mgl_error(void)      { return g_mgl_error; }

/* Prepare the local cache path for this game.
   ROM files live on the SD card, so this is a fast local operation:
   we just make sure the per-system cache directory exists and, if the
   game is not already in the cache, create a symlink pointing at the
   original on-disk ROM so downstream code can use the cache path.
   Returns true if the cache path is valid for launch. */
bool launcher_start_download(const LauncherGame *game, const char *base_dir)
{
    g_download_error[0] = '\0';

    char local_path[600];
    launcher_cache_path(game, base_dir, local_path, sizeof(local_path));

    /* already cached? */
    struct stat st;
    if (stat(local_path, &st) == 0 && st.st_size > 0)
        return true;

    /* ensure directory exists */
    char dir_path[600];
    snprintf(dir_path, sizeof(dir_path), "%s/cache/%s", base_dir, game->system);
    mkdir(dir_path, 0755);

    if (stat(game->path, &st) == 0) {
        /* Use a symlink so the cache-path abstraction stays intact without
           copying the ROM bytes on every first launch. */
        symlink(game->path, local_path);
        return true;
    }

    snprintf(g_download_error, sizeof(g_download_error),
             "ROM file is missing: %s", game->path);
    printf("launcher: %s\n", g_download_error);
    return false;
}

/* Poll "download" progress. Always completes immediately now — either the
   file is cached / symlinked, or it isn't. Kept as a function so the
   existing caller loop in launcher.cpp doesn't have to change shape. */
int launcher_poll_download(const LauncherGame *game, const char *base_dir)
{
    char local_path[600];
    launcher_cache_path(game, base_dir, local_path, sizeof(local_path));
    struct stat st;
    if (lstat(local_path, &st) == 0 && (S_ISLNK(st.st_mode) || st.st_size > 0))
        return 1;
    if (!g_download_error[0])
        snprintf(g_download_error, sizeof(g_download_error),
                 "ROM file is missing: %s", game->path);
    return -1;
}

void launcher_cancel_download(void)
{
    /* No async work in progress — kept for API compatibility. */
}
