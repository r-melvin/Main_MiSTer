/*
 * launcher_io.cpp
 * Data loading, state persistence, ROM download, MGL launch, cover cache.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <glob.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <ctype.h>

#include "launcher.h"
#include "launcher_draw.h"
#include "lib/imlib2/Imlib2.h"
#include "cfg.h"
#include "file_io.h"
#include "str_util.h"

/* ─── core map ───────────────────────────────────────────────────────────── */

const CoreMapEntry launcher_core_map[] = {
    { "SNES",          "_Console",  "SNES",          0 },
    { "NES",           "_Console",  "NES",           0 },
    { "GBA",           "_Console",  "GBA",           0 },
    { "GB",            "_Console",  "Gameboy",       0 },
    { "GBC",           "_Console",  "Gameboy",       0 },
    { "Genesis",       "_Console",  "Genesis",       0 },
    { "MegaDrive",     "_Console",  "Genesis",       0 },
    { "Sega32X",       "_Console",  "S32X",          0 },
    { "MasterSystem",  "_Console",  "SMS",           0 },
    { "GameGear",      "_Console",  "SMS",           0 },
    { "PCEngine",      "_Console",  "TurboGrafx16",  0 },
    { "TurboGrafx16",  "_Console",  "TurboGrafx16",  0 },
    { "NeoGeo",        "_Console",  "NeoGeo",        1 },
    { "Arcade",        "_Arcade",   NULL,            0 },  /* uses MRA */
    { "Atari2600",     "_Console",  "Atari7800",     0 },
    { "Atari7800",     "_Console",  "Atari7800",     0 },
    { "AtariLynx",     "_Console",  "AtariLynx",     0 },
    { "ColecoVision",  "_Console",  "ColecoVision",  0 },
    { "Intellivision", "_Console",  "Intellivision", 0 },
    { "PSX",           "_Console",  "PSX",           1 },
    { "N64",           "_Console",  "N64",           0 },
    { "C64",           "_Computer", "C64",           0 },
    { "AmigaOCS",      "_Computer", "Minimig",       0 },
};
const int launcher_core_map_count = (int)(sizeof(launcher_core_map) / sizeof(launcher_core_map[0]));

const CoreMapEntry *launcher_find_core(const char *system)
{
    for (int i = 0; i < launcher_core_map_count; i++)
        if (strcasecmp(launcher_core_map[i].system, system) == 0)
            return &launcher_core_map[i];
    return NULL;
}

/* ─── theme ──────────────────────────────────────────────────────────────── */

LauncherTheme g_theme;

static void theme_set_defaults(LauncherTheme *t)
{
    t->bg            = 0xFF141417u;  /* near-black charcoal */
    t->card          = 0xFF2A2A2Eu;  /* dark grey card */
    t->hi            = 0xFFE8A824u;  /* warm amber highlight */
    t->fav           = 0xFFE89820u;  /* amber favourite */
    t->text          = 0xFFD4D4D8u;  /* light grey text */
    t->dim           = 0xFF6E6E78u;  /* mid-grey secondary */
    t->bar           = 0xFF1E1E22u;  /* dark grey header/footer */
    t->overlay       = 0xD2000000u;
    t->err           = 0xFFDC3C3Cu;
    t->search        = 0xFF262629u;  /* dark grey search bar */
    t->font_sizes[0] = 28;
    t->font_sizes[1] = 22;
    t->font_sizes[2] = 17;
    t->bg_image[0]   = '\0';
    strncpy(t->name, "default", sizeof(t->name) - 1);
}

/* Parse a hex color string.
   6 chars (RRGGBB)  → 0xFFRRGGBB (fully opaque)
   8 chars (AARRGGBB)→ 0xAARRGGBB  */
static uint32_t parse_color(const char *s, uint32_t fallback)
{
    size_t len = strlen(s);
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (!end || end == s) return fallback;
    if (len == 6) return 0xFF000000u | (uint32_t)v;
    if (len == 8) return (uint32_t)v;
    return fallback;
}

static int parse_font_size(const char *s, int fallback)
{
    int v = atoi(s);
    return (v >= 8 && v <= 72) ? v : fallback;
}

static void trim(char *s)
{
    /* trim leading */
    char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    /* trim trailing */
    char *e = s + strlen(s) - 1;
    while (e >= s && isspace((unsigned char)*e)) *e-- = '\0';
}

void launcher_theme_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* strip inline comments */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (!key[0]) continue;

        if      (!strcasecmp(key, "name"))       strncpy(g_theme.name,     val, sizeof(g_theme.name)     - 1);
        else if (!strcasecmp(key, "bg_image"))   strncpy(g_theme.bg_image, val, sizeof(g_theme.bg_image) - 1);
        else if (!strcasecmp(key, "bg"))         g_theme.bg      = parse_color(val, g_theme.bg);
        else if (!strcasecmp(key, "card"))       g_theme.card    = parse_color(val, g_theme.card);
        else if (!strcasecmp(key, "hi"))         g_theme.hi      = parse_color(val, g_theme.hi);
        else if (!strcasecmp(key, "fav"))        g_theme.fav     = parse_color(val, g_theme.fav);
        else if (!strcasecmp(key, "text"))       g_theme.text    = parse_color(val, g_theme.text);
        else if (!strcasecmp(key, "dim"))        g_theme.dim     = parse_color(val, g_theme.dim);
        else if (!strcasecmp(key, "bar"))        g_theme.bar     = parse_color(val, g_theme.bar);
        else if (!strcasecmp(key, "overlay"))    g_theme.overlay = parse_color(val, g_theme.overlay);
        else if (!strcasecmp(key, "err"))        g_theme.err     = parse_color(val, g_theme.err);
        else if (!strcasecmp(key, "search"))     g_theme.search  = parse_color(val, g_theme.search);
        else if (!strcasecmp(key, "font_title")) g_theme.font_sizes[0] = parse_font_size(val, g_theme.font_sizes[0]);
        else if (!strcasecmp(key, "font_big"))   g_theme.font_sizes[1] = parse_font_size(val, g_theme.font_sizes[1]);
        else if (!strcasecmp(key, "font_sm"))    g_theme.font_sizes[2] = parse_font_size(val, g_theme.font_sizes[2]);
    }
    fclose(fp);
    printf("launcher: theme '%s' loaded from %s\n", g_theme.name, path);
}

void launcher_theme_init(const char *launcher_path, const char *theme_name)
{
    theme_set_defaults(&g_theme);
    if (!theme_name || !theme_name[0] || !strcasecmp(theme_name, "default"))
        return;
    char path[600];
    snprintf(path, sizeof(path), "%s/themes/%s/theme.cfg", launcher_path, theme_name);
    launcher_theme_load(path);
}

/* ─── utility: find newest RBF matching a stem pattern ───────────────────── */

static bool find_rbf(const char *dir_name, const char *stem,
                     char *out, size_t out_sz)
{
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "/media/fat/%s", dir_name);

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

    snprintf(out, out_sz, "/media/fat/%s/%s", dir_name, best);
    return true;
}

/* ─── secure SSH helpers ─────────────────────────────────────────────────── */

/* Strip the last file extension from name (in-place) */
static void strip_extension(char *name)
{
    char *dot = strrchr(name, '.');
    if (dot && dot != name)
        *dot = '\0';
}

/* Check if a file exists and its mtime is less than max_age_seconds old */
static bool file_is_fresh(const char *path, int max_age_seconds)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (time(NULL) - st.st_mtime) < max_age_seconds;
}

/* Path to the per-launcher known_hosts file.
   Using a dedicated file means host keys are isolated from the system's
   ~/.ssh/known_hosts and are tied to this launcher's configuration. */
static void get_known_hosts_path(char *out, size_t out_sz)
{
    const char *base = cfg.launcher_path[0] ? cfg.launcher_path : "/media/fat/remote_ui";
    snprintf(out, out_sz, "%s/known_hosts", base);
}

/* Called inside a child process after fork() to exec ssh for remote listing.
 *
 * Security properties:
 *  - Key auth (sftp_key_path set): uses ssh -i, no password involved.
 *  - Password auth: password is passed via the SSHPASS environment variable
 *    and sshpass -e, so it is NOT visible in `ps aux` or /proc/pid/cmdline.
 *  - StrictHostKeyChecking=accept-new: trusts new hosts on first connect but
 *    rejects changed keys, protecting against MITM after initial contact.
 *  - Dedicated known_hosts file scoped to this launcher instance.
 *  - BatchMode=yes: no interactive prompts; fails cleanly if auth is wrong.
 *  - LogLevel=ERROR: suppresses SSH banners but keeps real error messages.
 *
 * Parameters:
 *  port_str    – port as a string, e.g. "22".
 *  known_hosts – full path to the known_hosts file.
 *  host_str    – "user@host"
 *  extra_argv  – NULL-terminated list of args appended after the host,
 *                e.g. { "ls -1 /games/SNES/ 2>/dev/null", NULL }
 *
 * Never returns on success (exec replaces the process image).
 * Calls _exit(1) on failure.
 */
static void exec_ssh_child(const char *port_str,
                            const char *known_hosts, const char *host_str,
                            char *const extra_argv[])
{
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

    char kh_opt[580];
    snprintf(kh_opt, sizeof(kh_opt), "UserKnownHostsFile=%s", known_hosts);

    /* sshpass(1) -e(1) ssh(1) -p(2) -o×5(10) -i(2) host(1) extras(~2) NULL = ~21 */
    char *argv[32];
    int i = 0;

    bool use_key = cfg.sftp_key_path[0] != '\0';

    if (!use_key) {
        setenv("SSHPASS", cfg.sftp_pass, 1);
        argv[i++] = (char *)"sshpass";
        argv[i++] = (char *)"-e";
    }

    argv[i++] = (char *)"ssh";
    argv[i++] = (char *)"-p"; argv[i++] = (char *)port_str;
    argv[i++] = (char *)"-o"; argv[i++] = (char *)"StrictHostKeyChecking=accept-new";
    argv[i++] = (char *)"-o"; argv[i++] = kh_opt;
    argv[i++] = (char *)"-o"; argv[i++] = (char *)"ConnectTimeout=10";
    argv[i++] = (char *)"-o"; argv[i++] = (char *)"BatchMode=yes";
    argv[i++] = (char *)"-o"; argv[i++] = (char *)"LogLevel=ERROR";

    if (use_key) {
        struct stat kst;
        if (stat(cfg.sftp_key_path, &kst) == 0 &&
            (kst.st_mode & (S_IRWXG | S_IRWXO))) {
            dprintf(STDERR_FILENO,
                    "launcher: WARNING: key %s has loose permissions\n",
                    cfg.sftp_key_path);
        }
        argv[i++] = (char *)"-i"; argv[i++] = cfg.sftp_key_path;
    }

    argv[i++] = (char *)host_str;

    for (int j = 0; extra_argv[j]; j++)
        argv[i++] = extra_argv[j];

    argv[i] = NULL;

    execvp(argv[0], argv);
    _exit(1);
}

/* Called inside a child process after fork() to exec rsync for file download.
 *
 * Uses rsync over SSH so downloads benefit from delta transfer and --partial
 * resumption: if the transfer is interrupted, the next attempt picks up where
 * it left off rather than restarting from scratch.
 *
 * The SSH command is passed to rsync via -e so all the same security options
 * (StrictHostKeyChecking, known_hosts, BatchMode) apply as for direct SSH.
 *
 * Never returns on success; calls _exit(1) on failure.
 */
static void exec_rsync_child(const char *port_str,
                              const char *known_hosts,
                              const char *remote_src,  /* "user@host:remote_path" */
                              const char *local_dest)
{
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

    bool use_key = cfg.sftp_key_path[0] != '\0';

    /* Build the ssh command string rsync passes to /bin/sh -c via -e.
       Single-quote option values that may contain path characters. */
    char ssh_cmd[1024];
    if (use_key) {
        snprintf(ssh_cmd, sizeof(ssh_cmd),
                 "ssh -p %s"
                 " -o StrictHostKeyChecking=accept-new"
                 " -o 'UserKnownHostsFile=%s'"
                 " -o ConnectTimeout=10"
                 " -o BatchMode=yes"
                 " -o LogLevel=ERROR"
                 " -i '%s'",
                 port_str, known_hosts, cfg.sftp_key_path);
    } else {
        snprintf(ssh_cmd, sizeof(ssh_cmd),
                 "ssh -p %s"
                 " -o StrictHostKeyChecking=accept-new"
                 " -o 'UserKnownHostsFile=%s'"
                 " -o ConnectTimeout=10"
                 " -o BatchMode=yes"
                 " -o LogLevel=ERROR",
                 port_str, known_hosts);
    }

    /* sshpass(1) -e(1) rsync(1) -az(1) --partial(1) -e(2) src(1) dest(1) NULL = ~10 */
    char *argv[16];
    int i = 0;

    if (!use_key) {
        setenv("SSHPASS", cfg.sftp_pass, 1);
        argv[i++] = (char *)"sshpass";
        argv[i++] = (char *)"-e";
    }

    argv[i++] = (char *)"rsync";
    argv[i++] = (char *)"-az";        /* archive mode + compress */
    argv[i++] = (char *)"--partial";  /* keep partial file; resume on retry */
    argv[i++] = (char *)"-e"; argv[i++] = ssh_cmd;
    argv[i++] = (char *)remote_src;
    argv[i++] = (char *)local_dest;
    argv[i]   = NULL;

    execvp(argv[0], argv);
    _exit(1);
}

/* ─── cover art scraping (TheGamesDB) ───────────────────────────────────── */

/* Maps launcher system name → TheGamesDB numeric platform ID.
   Returns 0 if no mapping is known (scraping will be skipped for that system). */
static int tgdb_platform_id(const char *system)
{
    static const struct { const char *sys; int id; } map[] = {
        { "SNES",          6    },
        { "NES",           7    },
        { "GBA",           5    },
        { "GB",            4    },
        { "GBC",           41   },
        { "Genesis",       18   },
        { "MegaDrive",     18   },
        { "Sega32X",       33   },
        { "MasterSystem",  35   },
        { "GameGear",      21   },
        { "PCEngine",      34   },
        { "TurboGrafx16",  34   },
        { "NeoGeo",        24   },
        { "Arcade",        23   },  /* Arcade */
        { "Atari2600",     22   },
        { "Atari7800",     27   },
        { "AtariLynx",     61   },
        { "ColecoVision",  68   },
        { "Intellivision", 32   },
        { "PSX",           10   },
        { "N64",            3   },
        { "C64",           40   },
        { "AmigaOCS",      4787 },
        { NULL, 0 }
    };
    for (int i = 0; map[i].sys; i++)
        if (strcasecmp(map[i].sys, system) == 0)
            return map[i].id;
    return 0;
}

/* Percent-encode a string for use in a URL query parameter.
   Spaces become '+'; everything else that is not unreserved is hex-encoded. */
static void url_encode(const char *src, char *dst, size_t dst_sz)
{
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 4 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = (char)c;
        } else if (c == ' ') {
            dst[di++] = '+';
        } else {
            snprintf(dst + di, 4, "%%%02X", (unsigned)c);
            di += 3;
        }
    }
    dst[di] = '\0';
}

/* Extract the value of a JSON string field: finds "key":"value" and copies value
   into buf.  Returns true on success.  Not a full JSON parser — intended for
   the predictable TGDB response format only. */
static bool json_str(const char *json, const char *key, char *buf, size_t buf_sz)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++; /* skip opening quote */
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < buf_sz) {
        if (*p == '\\') {
            p++;  /* skip escape char, copy the next byte verbatim */
            if (!*p) break;
        }
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return i > 0;
}

/* Extract the value of a JSON integer field: finds "key":NUMBER.
   Returns the value, or -1 if not found. */
static int json_int(const char *json, const char *key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (!isdigit((unsigned char)*p)) return -1;
    return atoi(p);
}

/* Run curl in a child process to download url → output_path.
   Returns true if curl exited successfully and the file has content. */
static bool exec_curl(const char *url, const char *output_path)
{
    pid_t pid = fork();
    if (pid == 0) {
        /* Redirect stderr to /dev/null so curl banners don't pollute console */
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, STDERR_FILENO); close(dn); }

        char *args[] = {
            (char *)"curl",
            (char *)"-s",                  /* silent */
            (char *)"--max-time", (char *)"20",
            (char *)"--retry",    (char *)"2",
            (char *)"-o", (char *)output_path,
            (char *)url,
            NULL
        };
        execvp("curl", args);
        _exit(1);
    }
    if (pid < 0) return false;

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;

    struct stat st;
    return stat(output_path, &st) == 0 && st.st_size > 0;
}

/* Query TheGamesDB for cover art for game_name on system, download the image
   to covers_dir/system/<sanitized_name>.jpg, and copy the filename into
   out_cover.  Returns true on success.
   Skips silently if TGDB_API_KEY is not configured or the platform is unknown.
   Idempotent: if the cover file already exists it is returned immediately. */
static bool scrape_tgdb_cover(const char *game_name, const char *system,
                               const char *covers_dir,
                               char *out_cover, size_t out_sz)
{
    if (!cfg.tgdb_api_key[0]) return false;

    int plat_id = tgdb_platform_id(system);
    if (!plat_id) return false;

    /* --- build local cover path ------------------------------------------ */
    /* Sanitize game_name to a safe filename */
    char safe_name[256];
    strncpy(safe_name, game_name, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';
    for (char *p = safe_name; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
            *p = '_';

    char cover_file[280];
    snprintf(cover_file, sizeof(cover_file), "%s.jpg", safe_name);

    /* Ensure system covers directory exists */
    char sys_covers[600];
    snprintf(sys_covers, sizeof(sys_covers), "%s/%s", covers_dir, system);
    mkdir(sys_covers, 0755);

    char cover_path[880];
    snprintf(cover_path, sizeof(cover_path), "%s/%s", sys_covers, cover_file);

    /* Already cached — return immediately */
    struct stat cst;
    if (stat(cover_path, &cst) == 0 && cst.st_size > 200) {
        strncpy(out_cover, cover_file, out_sz - 1);
        out_cover[out_sz - 1] = '\0';
        return true;
    }

    /* --- build search URL ------------------------------------------------- */
    char encoded_name[512];
    url_encode(game_name, encoded_name, sizeof(encoded_name));

    char search_url[1024];
    snprintf(search_url, sizeof(search_url),
             "https://api.thegamesdb.net/v1/Games/ByGameName"
             "?apikey=%s&name=%s&filter[platform]=%d"
             "&fields=game_title&include=boxart&page=1",
             cfg.tgdb_api_key, encoded_name, plat_id);

    /* --- download the JSON response --------------------------------------- */
    char json_path[64];
    snprintf(json_path, sizeof(json_path), "/tmp/tgdb_%d.json", (int)getpid());

    if (!exec_curl(search_url, json_path)) {
        remove(json_path);
        return false;
    }

    FILE *fp = fopen(json_path, "r");
    if (!fp) { remove(json_path); return false; }

    fseek(fp, 0, SEEK_END);
    long json_sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (json_sz <= 0 || json_sz > 512 * 1024) {
        fclose(fp); remove(json_path); return false;
    }

    char *json = (char *)malloc((size_t)json_sz + 1);
    if (!json) { fclose(fp); remove(json_path); return false; }
    fread(json, 1, (size_t)json_sz, fp);
    json[json_sz] = '\0';
    fclose(fp);
    remove(json_path);

    /* --- parse: game ID --------------------------------------------------- */
    const char *games_arr = strstr(json, "\"games\":");
    int game_id = games_arr ? json_int(games_arr, "id") : -1;
    if (game_id <= 0) { free(json); return false; }

    /* --- parse: boxart base URL ------------------------------------------- */
    char base_url[256] = "https://cdn.thegamesdb.net/images/original/";
    const char *boxart_sec = strstr(json, "\"boxart\":");
    if (boxart_sec)
        json_str(boxart_sec, "original", base_url, sizeof(base_url));

    /* --- parse: front boxart filename for this game ----------------------- */
    char img_filename[256] = {};
    if (boxart_sec) {
        char id_key[40];
        snprintf(id_key, sizeof(id_key), "\"%d\":[", game_id);
        const char *game_ba = strstr(boxart_sec, id_key);
        if (game_ba) {
            game_ba += strlen(id_key);  /* now at the first { of the array */
            /* Scan objects in the array for the "front" side */
            const char *p = game_ba;
            while (*p && *p != ']') {
                const char *obj_s = strchr(p, '{');
                if (!obj_s || obj_s >= p + 2048) break;
                const char *obj_e = strchr(obj_s, '}');
                if (!obj_e) break;
                size_t olen = (size_t)(obj_e - obj_s + 1);
                if (olen < 2048) {
                    char obj[2048];
                    memcpy(obj, obj_s, olen);
                    obj[olen] = '\0';
                    if (strstr(obj, "\"front\"")) {
                        json_str(obj, "filename", img_filename, sizeof(img_filename));
                        break;
                    }
                }
                p = obj_e + 1;
            }
            /* Fallback: use first filename in the array (often the front anyway) */
            if (!img_filename[0])
                json_str(game_ba, "filename", img_filename, sizeof(img_filename));
        }
    }

    free(json);
    if (!img_filename[0]) return false;

    /* --- download the image ---------------------------------------------- */
    char img_url[512];
    snprintf(img_url, sizeof(img_url), "%s%s", base_url, img_filename);

    /* Download to a .tmp first so a failed download doesn't leave a corrupt
       stub that would be treated as "already cached" on the next run. */
    char tmp_cover[900];
    snprintf(tmp_cover, sizeof(tmp_cover), "%s.tmp", cover_path);

    if (!exec_curl(img_url, tmp_cover)) {
        remove(tmp_cover);
        return false;
    }

    struct stat ist;
    if (stat(tmp_cover, &ist) != 0 || ist.st_size < 200) {
        remove(tmp_cover);
        return false;
    }

    rename(tmp_cover, cover_path);

    strncpy(out_cover, cover_file, out_sz - 1);
    out_cover[out_sz - 1] = '\0';
    printf("launcher: scraped cover for '%s' (%s)\n", game_name, system);
    return true;
}

/* ─── remote library scan ────────────────────────────────────────────────── */

/* SSH to the remote host and list files in sftp_base_path/system/.
   Writes a pipe-delimited CSV to csv_path.  When cfg.tgdb_api_key is set, also
   downloads cover art to covers_dir/system/ for each game not yet cached.
   Returns number of game entries written, 0 if directory empty/missing, -1 on error. */
static int scan_remote_system(const char *system, const char *csv_path,
                               const char *sftp_base_path,
                               const char *covers_dir)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", cfg.sftp_port ? cfg.sftp_port : 22);

    char host_str[600];
    snprintf(host_str, sizeof(host_str), "%s@%s", cfg.sftp_user, cfg.sftp_host);

    char known_hosts[600];
    get_known_hosts_path(known_hosts, sizeof(known_hosts));

    /* Remote command run on the server: list plain files only, suppress errors
       for missing directories (system not present on this server). */
    char ls_cmd[700];
    snprintf(ls_cmd, sizeof(ls_cmd),
             "ls -1p %s/%s/ 2>/dev/null | grep -v /$",
             sftp_base_path, system);

    /* Pipe to capture stdout */
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        char *extras[] = { ls_cmd, NULL };
        exec_ssh_child(port_str, known_hosts, host_str, extras);
        /* exec_ssh_child never returns */
    }
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    close(pipefd[1]);
    FILE *pipe_rd = fdopen(pipefd[0], "r");
    if (!pipe_rd) { waitpid(pid, NULL, 0); close(pipefd[0]); return -1; }

    /* Write CSV to a .tmp file first, rename atomically on success */
    char tmp_path[608];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", csv_path);
    FILE *csv = fopen(tmp_path, "w");
    if (!csv) { fclose(pipe_rd); waitpid(pid, NULL, 0); return -1; }

    int count = 0;
    char filename[512];
    while (fgets(filename, sizeof(filename), pipe_rd)) {
        int ln = (int)strlen(filename);
        while (ln > 0 && (filename[ln-1] == '\n' || filename[ln-1] == '\r'))
            filename[--ln] = '\0';
        if (!ln || filename[0] == '.') continue;
        /* ls -1p appends '/' to directories; grep -v /$ removed them on the
           server side, but double-check locally as a safety net */
        if (filename[ln-1] == '/') continue;
        if (!strrchr(filename, '.')) continue; /* no extension → skip */

        char display_name[256];
        strncpy(display_name, filename, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
        strip_extension(display_name);

        char remote_path[768];
        snprintf(remote_path, sizeof(remote_path), "%s/%s/%s",
                 sftp_base_path, system, filename);

        /* CSV format: name|remote_path|cover_filename.
           Cover filename is intentionally left empty here; scraping is lazy
           and happens in the cover worker thread when the image is first needed. */
        fprintf(csv, "%s|%s|\n", display_name, remote_path);
        count++;
    }

    fclose(csv);
    fclose(pipe_rd);

    int status = 0;
    waitpid(pid, &status, 0);

    if (count == 0) {
        remove(tmp_path);
        return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
    }

    rename(tmp_path, csv_path);
    return count;
}

/* Scan the remote SFTP server for all known systems and write CSV files to
   base_dir/lists/<System>.csv.  CSVs younger than 24 hours are skipped.
   Returns true if at least one system has games. */
bool launcher_scan_remote(const char *base_dir)
{
    if (!cfg.sftp_host[0] || !cfg.sftp_user[0]) {
        printf("launcher: SFTP not configured, skipping remote scan\n");
        return false;
    }

    const char *sftp_base = cfg.sftp_base_path[0] ? cfg.sftp_base_path : "/games";

    char lists_dir[512];
    snprintf(lists_dir, sizeof(lists_dir), "%s/lists", base_dir);
    mkdir(lists_dir, 0755);

    char covers_dir[512];
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", base_dir);
    mkdir(covers_dir, 0755);

    int found = 0;
    for (int i = 0; i < launcher_core_map_count; i++) {
        const char *sys = launcher_core_map[i].system;

        char csv_path[600];
        snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", lists_dir, sys);

        /* reuse existing CSV if it is less than 24 hours old */
        if (file_is_fresh(csv_path, 86400)) {
            printf("launcher: CSV fresh, skipping scan for %s\n", sys);
            found++;
            continue;
        }

        printf("launcher: scanning remote %s/%s ...\n", sftp_base, sys);
        int count = scan_remote_system(sys, csv_path, sftp_base, covers_dir);
        if (count > 0) {
            printf("launcher: %d games found for %s\n", count, sys);
            found++;
        } else if (count == 0) {
            printf("launcher: no games for %s (directory empty or missing)\n", sys);
        } else {
            printf("launcher: SSH error scanning %s\n", sys);
        }
    }

    return found > 0;
}

/* ─── MGL generation & launch ────────────────────────────────────────────── */

bool launcher_write_mgl(const LauncherGame *game)
{
    const CoreMapEntry *ce = launcher_find_core(game->system);
    if (!ce) {
        printf("launcher: no core map for system '%s'\n", game->system);
        return false;
    }

    char rbf_path[512] = {};
    if (ce->stem) {
        if (!find_rbf(ce->dir, ce->stem, rbf_path, sizeof(rbf_path))) {
            printf("launcher: RBF not found dir=%s stem=%s\n", ce->dir, ce->stem);
            return false;
        }
    }

    const char *mgl_path = "/media/fat/launch.mgl";
    FILE *fp = fopen(mgl_path, "w");
    if (!fp) { perror("launcher: fopen mgl"); return false; }

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

static pid_t g_download_pid = -1;

/* Start asynchronous download. Returns true if download kicked off (or already cached). */
bool launcher_start_download(const LauncherGame *game, const char *base_dir)
{
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

    /* local-only mode: if SFTP host not configured, game->path is local */
    if (!cfg.sftp_host[0]) {
        /* symlink or just update local_path to point at game->path */
        /* If the game path exists locally, we can just use it directly */
        if (stat(game->path, &st) == 0) {
            /* create symlink so cache path is valid */
            symlink(game->path, local_path);
            return true;
        }
        printf("launcher: no SFTP configured and '%s' not found locally\n", game->path);
        return false;
    }

    /* Secure file download via rsync over SSH.
       --partial keeps the .part file if interrupted so the next attempt resumes
       rather than restarting from scratch. */
    char part_path[608];
    snprintf(part_path, sizeof(part_path), "%s.part", local_path);

    g_download_pid = fork();
    if (g_download_pid == 0) {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", cfg.sftp_port ? cfg.sftp_port : 22);

        char known_hosts[600];
        get_known_hosts_path(known_hosts, sizeof(known_hosts));

        char remote_src[1024];
        snprintf(remote_src, sizeof(remote_src), "%s@%s:%s",
                 cfg.sftp_user, cfg.sftp_host, game->path);

        exec_rsync_child(port_str, known_hosts, remote_src, part_path);
        /* exec_rsync_child never returns */
    }
    return g_download_pid > 0;
}

/* Poll the download process. Returns: 1=done ok, -1=error, 0=still running */
int launcher_poll_download(const LauncherGame *game, const char *base_dir)
{
    if (g_download_pid <= 0) {
        /* check if already cached (no download needed) */
        char local_path[600];
        launcher_cache_path(game, base_dir, local_path, sizeof(local_path));
        struct stat st;
        if (stat(local_path, &st) == 0 && st.st_size > 0) return 1;
        return -1;
    }

    int status = 0;
    pid_t ret = waitpid(g_download_pid, &status, WNOHANG);
    if (ret == 0) return 0;  /* still running */

    g_download_pid = -1;

    if (ret < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1; /* error */

    /* rename .part → final */
    char local_path[600];
    launcher_cache_path(game, base_dir, local_path, sizeof(local_path));
    char part_path[608];
    snprintf(part_path, sizeof(part_path), "%s.part", local_path);
    rename(part_path, local_path);
    return 1;
}

void launcher_cancel_download(void)
{
    if (g_download_pid > 0) {
        kill(g_download_pid, SIGTERM);
        waitpid(g_download_pid, NULL, 0);
        g_download_pid = -1;
    }
}

/* ─── CSV parsing ────────────────────────────────────────────────────────── */

static int cmp_game_name(const void *a, const void *b)
{
    return strcasecmp(((const LauncherGame*)a)->name,
                      ((const LauncherGame*)b)->name);
}

static int cmp_game_name_desc(const void *a, const void *b)
{
    return -strcasecmp(((const LauncherGame*)a)->name,
                       ((const LauncherGame*)b)->name);
}

static int cmp_game_recent(const void *a, const void *b)
{
    uint32_t ta = ((const LauncherGame*)a)->last_played;
    uint32_t tb = ((const LauncherGame*)b)->last_played;
    if (ta != tb) return (ta > tb) ? -1 : 1;
    uint16_t pa = ((const LauncherGame*)a)->play_count;
    uint16_t pb = ((const LauncherGame*)b)->play_count;
    return (pa > pb) ? -1 : (pa < pb) ? 1 : 0;
}

static int cmp_game_most_played(const void *a, const void *b)
{
    uint16_t pa = ((const LauncherGame*)a)->play_count;
    uint16_t pb = ((const LauncherGame*)b)->play_count;
    if (pa != pb) return (pa > pb) ? -1 : 1;
    uint32_t ta = ((const LauncherGame*)a)->last_played;
    uint32_t tb = ((const LauncherGame*)b)->last_played;
    return (ta > tb) ? -1 : (ta < tb) ? 1 : 0;
}

static int cmp_game_rated(const void *a, const void *b)
{
    uint8_t ra = ((const LauncherGame*)a)->user_rating;
    uint8_t rb = ((const LauncherGame*)b)->user_rating;
    if (ra != rb) return (ra > rb) ? -1 : 1;
    /* tiebreak: more played first */
    uint16_t pa = ((const LauncherGame*)a)->play_count;
    uint16_t pb = ((const LauncherGame*)b)->play_count;
    return (pa > pb) ? -1 : (pa < pb) ? 1 : 0;
}

static int cmp_system_name(const void *a, const void *b)
{
    return strcasecmp(((const LauncherSystem*)a)->name,
                      ((const LauncherSystem*)b)->name);
}

/* Public sort function for game lists */
void launcher_sort_games(LauncherGame *games, int count, int sort_order)
{
    if (count <= 0) return;

    int (*cmp_func)(const void*, const void*) = cmp_game_name;  /* default */
    switch (sort_order) {
        case SORT_NAME_DESC:    cmp_func = cmp_game_name_desc; break;
        case SORT_RECENT:       cmp_func = cmp_game_recent; break;
        case SORT_MOST_PLAYED:  cmp_func = cmp_game_most_played; break;
        case SORT_HIGHEST_RATED: cmp_func = cmp_game_rated; break;
        case SORT_NAME_ASC:
        default:                cmp_func = cmp_game_name; break;
    }
    qsort(games, count, sizeof(LauncherGame), cmp_func);
}

/* Parse one CSV file; returns game count or -1 on error */
static int parse_csv(const char *path, const char *system_name,
                     const char *covers_dir,
                     LauncherGame **games_out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    int capacity = 256;
    int count = 0;
    LauncherGame *games = (LauncherGame*)malloc(capacity * sizeof(LauncherGame));

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* strip newline */
        int ln = (int)strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r'))
            line[--ln] = '\0';
        if (!ln) continue;

        /* pipe-delimited: name|path|cover_filename */
        char *name = line;
        char *path_col = strchr(name, '|');
        if (!path_col) continue;
        *path_col++ = '\0';
        char *cover_col = strchr(path_col, '|');
        if (!cover_col) continue;
        *cover_col++ = '\0';

        if (count == capacity) {
            capacity *= 2;
            games = (LauncherGame*)realloc(games, capacity * sizeof(LauncherGame));
        }

        LauncherGame *g = &games[count++];
        strncpy(g->name, name, sizeof(g->name) - 1);
        g->name[sizeof(g->name) - 1] = '\0';
        strncpy(g->path, path_col, sizeof(g->path) - 1);
        g->path[sizeof(g->path) - 1] = '\0';
        if (cover_col[0]) {
            snprintf(g->cover_path, sizeof(g->cover_path),
                     "%s/%s/%s", covers_dir, system_name, cover_col);
        } else {
            /* CSV has no cover filename — derive deterministic path from game name.
               Sanitization must match scrape_tgdb_cover() so the cover worker
               finds the file after scraping it. */
            char safe[256];
            strncpy(safe, name, sizeof(safe) - 1);
            safe[sizeof(safe) - 1] = '\0';
            for (char *p = safe; *p; p++)
                if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
                    *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
                    *p = '_';
            snprintf(g->cover_path, sizeof(g->cover_path),
                     "%s/%s/%s.jpg", covers_dir, system_name, safe);
        }
        strncpy(g->system, system_name, sizeof(g->system) - 1);
        g->system[sizeof(g->system) - 1] = '\0';

        /* calculate file size for local files */
        g->file_size = 0;
        if (g->path[0] == '/' && g->path[1] != '/') {  /* local absolute path, not SFTP */
            struct stat st;
            if (stat(g->path, &st) == 0) {
                g->file_size = (uint32_t)st.st_size;
            }
        }
    }
    fclose(fp);

    if (count > 0) qsort(games, count, sizeof(LauncherGame), cmp_game_name);

    *games_out = games;
    return count;
}

bool launcher_load_library(const char *base_dir,
                            LauncherSystem **out, int *count_out)
{
    char lists_dir[512];
    snprintf(lists_dir, sizeof(lists_dir), "%s/lists", base_dir);
    char covers_dir[512];
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", base_dir);

    DIR *d = opendir(lists_dir);
    if (!d) {
        printf("launcher: cannot open lists dir %s\n", lists_dir);
        *out = NULL; *count_out = 0;
        return false;
    }

    int cap = 32, count = 0;
    LauncherSystem *systems = (LauncherSystem*)malloc(cap * sizeof(LauncherSystem));

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        int ln = (int)strlen(ent->d_name);
        if (ln < 5 || strcasecmp(ent->d_name + ln - 4, ".csv") != 0) continue;

        char sys_name[64];
        strncpy(sys_name, ent->d_name, sizeof(sys_name) - 1);
        sys_name[sizeof(sys_name) - 1] = '\0';
        sys_name[strlen(sys_name) - 4] = '\0';  /* strip .csv */

        char csv_path[1024];
        snprintf(csv_path, sizeof(csv_path), "%s/%s", lists_dir, ent->d_name);

        LauncherGame *games = NULL;
        int game_count = parse_csv(csv_path, sys_name, covers_dir, &games);
        if (game_count < 0) continue;

        if (count == cap) {
            cap *= 2;
            systems = (LauncherSystem*)realloc(systems, cap * sizeof(LauncherSystem));
        }

        LauncherSystem *sys = &systems[count++];
        strncpy(sys->name, sys_name, sizeof(sys->name) - 1);
        sys->name[sizeof(sys->name) - 1] = '\0';
        sys->games      = games;
        sys->game_count = game_count;
        sys->is_virtual = 0;
    }
    closedir(d);

    if (count > 1)
        qsort(systems, count, sizeof(LauncherSystem), cmp_system_name);

    *out = systems; *count_out = count;
    printf("launcher: loaded %d systems\n", count);
    return true;
}

void launcher_free_library(LauncherSystem *systems, int count)
{
    for (int i = 0; i < count; i++)
        free(systems[i].games);
    free(systems);
}

/* ─── state persistence ──────────────────────────────────────────────────── */

bool launcher_load_state(const char *path, LauncherState *st)
{
    memset(st, 0, sizeof(*st));

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[1024];
    int section = 0; /* 0=none, 1=favs, 2=history, 3=positions */

    while (fgets(line, sizeof(line), fp)) {
        int ln = (int)strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r'))
            line[--ln] = '\0';
        if (!ln) continue;

        if (strcmp(line, "[favourites]") == 0) { section = 1; continue; }
        if (strcmp(line, "[history]") == 0)    { section = 2; continue; }
        if (strcmp(line, "[positions]") == 0)  { section = 3; continue; }

        if (section == 1 && st->fav_count < LAUNCHER_MAX_FAVS) {
            /* format: SYSTEM=path */
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                FavEntry *f = &st->favs[st->fav_count++];
                strncpy(f->system, line, sizeof(f->system) - 1);
                strncpy(f->path,   eq+1, sizeof(f->path)   - 1);
            }
        } else if (section == 2 && st->history_count < LAUNCHER_MAX_HISTORY) {
            /* format: ts|system|path|name|cover_path|count[|play_time][|user_rating] */
            char *p = line;
            char *fields[8]; int fi = 0;
            fields[fi++] = p;
            while (*p && fi < 8) {
                if (*p == '|') { *p = '\0'; fields[fi++] = p + 1; }
                p++;
            }
            if (fi >= 6) {
                HistoryEntry *h = &st->history[st->history_count++];
                h->ts    = (uint32_t)atol(fields[0]);
                strncpy(h->system,     fields[1], sizeof(h->system) - 1);
                strncpy(h->path,       fields[2], sizeof(h->path)   - 1);
                strncpy(h->name,       fields[3], sizeof(h->name)   - 1);
                strncpy(h->cover_path, fields[4], sizeof(h->cover_path) - 1);
                h->count     = atoi(fields[5]);
                h->play_time = (fi >= 7) ? (uint32_t)atol(fields[6]) : 0;  /* backward compat */
                h->user_rating = (fi >= 8) ? (uint8_t)atoi(fields[7]) : 0;  /* backward compat */
            }
        } else if (section == 3) {
            /* format: SYS_INDEX=selected|scroll */
            char *eq = strchr(line, '=');
            if (eq) {
                int sys_idx = atoi(line);
                if (sys_idx >= 0 && sys_idx < LAUNCHER_MAX_SYSTEMS) {
                    char *pipe = strchr(eq + 1, '|');
                    if (pipe) {
                        *pipe = '\0';
                        st->per_system[sys_idx].selected_game = atoi(eq + 1);
                        st->per_system[sys_idx].scroll_offset = atoi(pipe + 1);
                    }
                }
            }
        }
    }
    fclose(fp);
    return true;
}

bool launcher_save_state(const char *path, const LauncherState *st)
{
    FILE *fp = fopen(path, "w");
    if (!fp) { perror("launcher: save state"); return false; }

    fprintf(fp, "[favourites]\n");
    for (int i = 0; i < st->fav_count; i++)
        fprintf(fp, "%s=%s\n", st->favs[i].system, st->favs[i].path);

    fprintf(fp, "[history]\n");
    for (int i = 0; i < st->history_count; i++) {
        const HistoryEntry *h = &st->history[i];
        fprintf(fp, "%u|%s|%s|%s|%s|%d|%u|%u\n",
                h->ts, h->system, h->path, h->name, h->cover_path, h->count, h->play_time, (unsigned)h->user_rating);
    }

    fprintf(fp, "[positions]\n");
    for (int i = 0; i < LAUNCHER_MAX_SYSTEMS; i++) {
        if (st->per_system[i].selected_game > 0 || st->per_system[i].scroll_offset > 0)
            fprintf(fp, "%d=%d|%d\n", i, st->per_system[i].selected_game, st->per_system[i].scroll_offset);
    }

    fclose(fp);
    return true;
}

void launcher_state_record_played(LauncherState *st, const LauncherGame *g,
                                   const char *state_path)
{
    /* update or insert into history */
    uint32_t now = (uint32_t)time(NULL);
    for (int i = 0; i < st->history_count; i++) {
        if (strcmp(st->history[i].path, g->path) == 0 &&
            strcmp(st->history[i].system, g->system) == 0) {
            st->history[i].ts = now;
            st->history[i].count++;
            /* move to front */
            HistoryEntry tmp = st->history[i];
            memmove(&st->history[1], &st->history[0], i * sizeof(HistoryEntry));
            st->history[0] = tmp;
            launcher_save_state(state_path, st);
            return;
        }
    }
    /* new entry */
    if (st->history_count == LAUNCHER_MAX_HISTORY)
        st->history_count--;
    memmove(&st->history[1], &st->history[0],
            st->history_count * sizeof(HistoryEntry));
    HistoryEntry *h = &st->history[0];
    h->ts    = now;
    h->count = 1;
    strncpy(h->system,     g->system,     sizeof(h->system) - 1);
    strncpy(h->path,       g->path,       sizeof(h->path)   - 1);
    strncpy(h->name,       g->name,       sizeof(h->name)   - 1);
    strncpy(h->cover_path, g->cover_path, sizeof(h->cover_path) - 1);
    st->history_count++;
    launcher_save_state(state_path, st);
}

void launcher_state_apply_play_time(LauncherState *st, const char *state_path)
{
    /* check if /tmp/launcher_play_start exists (from previous game launch) */
    FILE *fp = fopen("/tmp/launcher_play_start", "r");
    if (!fp) return;

    uint32_t start_ts = 0;
    fscanf(fp, "%u", &start_ts);
    fclose(fp);
    remove("/tmp/launcher_play_start");

    if (!start_ts || !st->history_count) return;

    /* calculate duration in seconds, cap at 12 hours for safety */
    uint32_t now = (uint32_t)time(NULL);
    uint32_t dur = (now > start_ts) ? (now - start_ts) : 0;
    const uint32_t MAX_DURATION = 12 * 3600;
    if (dur > MAX_DURATION) dur = MAX_DURATION;
    if (dur == 0) return;

    /* add to the most recent history entry (index 0) */
    st->history[0].play_time += dur;
    launcher_save_state(state_path, st);
}

bool launcher_state_is_favourite(const LauncherState *st, const LauncherGame *g)
{
    for (int i = 0; i < st->fav_count; i++)
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0)
            return true;
    return false;
}

void launcher_state_toggle_fav(LauncherState *st, const LauncherGame *g,
                                const char *state_path)
{
    for (int i = 0; i < st->fav_count; i++) {
        if (strcmp(st->favs[i].path, g->path) == 0 &&
            strcmp(st->favs[i].system, g->system) == 0) {
            /* remove */
            memmove(&st->favs[i], &st->favs[i+1],
                    (st->fav_count - i - 1) * sizeof(FavEntry));
            st->fav_count--;
            launcher_save_state(state_path, st);
            return;
        }
    }
    if (st->fav_count < LAUNCHER_MAX_FAVS) {
        FavEntry *f = &st->favs[st->fav_count++];
        strncpy(f->system, g->system, sizeof(f->system) - 1);
        strncpy(f->path,   g->path,   sizeof(f->path)   - 1);
        launcher_save_state(state_path, st);
    }
}

/* ─── virtual system builder ─────────────────────────────────────────────── */

/* Build virtual Recent and Favourites systems, prepend to real systems.
   Returns new array (caller frees virtual game arrays, NOT the real ones). */
LauncherSystem *launcher_build_all_systems(
    const LauncherSystem *real, int real_count,
    const LauncherState  *st,
    int *total_out)
{
    int extra = 0;
    if (st->history_count > 0) extra++;
    if (st->fav_count > 0) extra++;

    int total = extra + real_count;
    LauncherSystem *all = (LauncherSystem*)calloc(total, sizeof(LauncherSystem));
    int out = 0;

    /* Recent */
    if (st->history_count > 0) {
        LauncherSystem *sys = &all[out++];
        strncpy(sys->name, "Recent", sizeof(sys->name) - 1);
        sys->is_virtual = 1;
        sys->game_count = st->history_count;
        sys->games = (LauncherGame*)malloc(st->history_count * sizeof(LauncherGame));
        for (int i = 0; i < st->history_count; i++) {
            LauncherGame *g = &sys->games[i];
            strncpy(g->name,       st->history[i].name,       sizeof(g->name) - 1);
            strncpy(g->path,       st->history[i].path,       sizeof(g->path) - 1);
            strncpy(g->cover_path, st->history[i].cover_path, sizeof(g->cover_path) - 1);
            strncpy(g->system,     st->history[i].system,     sizeof(g->system) - 1);
        }
    }

    /* Favourites */
    if (st->fav_count > 0) {
        LauncherSystem *sys = &all[out++];
        strncpy(sys->name, "Favourites", sizeof(sys->name) - 1);
        sys->is_virtual = 1;
        /* collect matching games from real systems */
        int cap = st->fav_count, cnt = 0;
        LauncherGame *fgames = (LauncherGame*)malloc(cap * sizeof(LauncherGame));
        for (int f = 0; f < st->fav_count; f++) {
            for (int s = 0; s < real_count; s++) {
                for (int gi = 0; gi < real[s].game_count; gi++) {
                    if (strcmp(real[s].games[gi].path, st->favs[f].path) == 0) {
                        fgames[cnt++] = real[s].games[gi];
                        goto next_fav;
                    }
                }
            }
            next_fav:;
        }
        qsort(fgames, cnt, sizeof(LauncherGame), [](const void *a, const void *b) {
            return strcasecmp(((const LauncherGame*)a)->name,
                              ((const LauncherGame*)b)->name);
        });
        sys->games      = fgames;
        sys->game_count = cnt;
    }

    /* real systems */
    for (int i = 0; i < real_count; i++)
        all[out++] = real[i];

    *total_out = total;
    return all;
}

void launcher_free_virtual_systems(LauncherSystem *all, int total,
                                    int real_count)
{
    int extra = total - real_count;
    for (int i = 0; i < extra; i++)
        free(all[i].games); /* free virtual game arrays */
    free(all);              /* free the outer array */
}

/* ─── cover cache ────────────────────────────────────────────────────────── */

static CoverEntry g_cover_cache[LAUNCHER_MAX_COVERS];
static uint32_t   g_cover_frame = 0; /* incremented each frame */
static uint32_t   g_cover_fade[LAUNCHER_MAX_COVERS]; /* fade-in alpha 0-255 */

void launcher_cover_cache_tick(void) { g_cover_frame++; }

static inline uint32_t path_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static int cover_find_slot(const char *path)
{
    uint32_t h = path_hash(path);
    for (int i = 0; i < LAUNCHER_MAX_COVERS; i++)
        if (g_cover_cache[i].last_used && g_cover_cache[i].hash == h &&
            strcmp(g_cover_cache[i].path, path) == 0)
            return i;
    return -1;
}

static int cover_lru_evict(void)
{
    uint32_t oldest = UINT32_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < LAUNCHER_MAX_COVERS; i++) {
        if (!g_cover_cache[i].last_used) return i; /* empty slot */
        if (g_cover_cache[i].last_used < oldest) {
            oldest = g_cover_cache[i].last_used;
            oldest_idx = i;
        }
    }
    /* evict */
    if (g_cover_cache[oldest_idx].img) {
        imlib_context_set_image(g_cover_cache[oldest_idx].img);
        imlib_free_image();
        g_cover_cache[oldest_idx].img = NULL;
    }
    g_cover_cache[oldest_idx].last_used = 0;
    return oldest_idx;
}

Imlib_Image launcher_cover_get(const char *path)
{
    int idx = cover_find_slot(path);
    if (idx < 0) return NULL;
    g_cover_cache[idx].last_used = g_cover_frame;
    return g_cover_cache[idx].img;
}

Imlib_Image launcher_cover_get_ex(const char *path, uint32_t *fade_out)
{
    int idx = cover_find_slot(path);
    if (idx < 0) { *fade_out = 0; return NULL; }
    g_cover_cache[idx].last_used = g_cover_frame;
    *fade_out = g_cover_fade[idx];
    return g_cover_cache[idx].img;
}

uint32_t launcher_cover_fade_alpha(const char *path)
{
    int idx = cover_find_slot(path);
    if (idx < 0) return 0;
    return g_cover_fade[idx];
}

/* ─── cover background worker ────────────────────────────────────────────── */

#define COVER_QUEUE_SIZE 64

struct CoverRequest { char path[512]; char game_name[256]; char system[64]; };
struct CoverResult  { char path[512]; Imlib_Image img; }; /* already loaded */

static CoverRequest  g_req_queue[COVER_QUEUE_SIZE];
static int           g_req_head = 0, g_req_tail = 0;
static CoverResult   g_res_queue[COVER_QUEUE_SIZE];
static int           g_res_head = 0, g_res_tail = 0;

static pthread_mutex_t  g_req_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  g_res_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_req_cond = PTHREAD_COND_INITIALIZER;
static pthread_t        g_cover_thread;
static volatile bool    g_cover_running = false;

/* set of requested paths to avoid duplicates */
static char g_pending[COVER_QUEUE_SIZE][512];
static int  g_pending_count = 0;

static void *cover_worker(void *)
{
    Imlib_Context ctx = imlib_context_new();
    imlib_context_push(ctx);
    imlib_context_set_anti_alias(1);
    imlib_context_set_blend(0);

    while (g_cover_running) {
        pthread_mutex_lock(&g_req_mtx);
        while (g_req_head == g_req_tail && g_cover_running)
            pthread_cond_wait(&g_req_cond, &g_req_mtx);

        if (!g_cover_running) { pthread_mutex_unlock(&g_req_mtx); break; }

        CoverRequest req = g_req_queue[g_req_head % COVER_QUEUE_SIZE];
        g_req_head++;
        pthread_mutex_unlock(&g_req_mtx);

        /* load image with per-thread context */
        Imlib_Load_Error err = IMLIB_LOAD_ERROR_NONE;
        Imlib_Image src = imlib_load_image_with_error_return(req.path, &err);

        /* on cache-miss: attempt lazy scrape, then retry load */
        if (!src && req.game_name[0] && cfg.tgdb_api_key[0]) {
            char covers_dir[2048];
            snprintf(covers_dir, sizeof(covers_dir), "%s/covers", cfg.launcher_path);
            char dummy[280];
            if (scrape_tgdb_cover(req.game_name, req.system, covers_dir, dummy, sizeof(dummy))) {
                err = IMLIB_LOAD_ERROR_NONE;
                src = imlib_load_image_with_error_return(req.path, &err);
            }
        }

        Imlib_Image scaled = NULL;
        if (src) {
            imlib_context_set_image(src);
            int sw = imlib_image_get_width();
            int sh = imlib_image_get_height();
            scaled = imlib_create_cropped_scaled_image(0, 0, sw, sh,
                                                       LAUNCHER_COVER_W,
                                                       LAUNCHER_COVER_H);
            imlib_free_image();
        }

        /* push result */
        pthread_mutex_lock(&g_res_mtx);
        if ((g_res_tail - g_res_head) < COVER_QUEUE_SIZE) {
            CoverResult *r = &g_res_queue[g_res_tail % COVER_QUEUE_SIZE];
            strncpy(r->path, req.path, sizeof(r->path) - 1);
            r->img = scaled;
            g_res_tail++;
        } else if (scaled) {
            imlib_context_set_image(scaled);
            imlib_free_image();
        }
        pthread_mutex_unlock(&g_res_mtx);
    }

    imlib_context_pop();
    imlib_context_free(ctx);
    return NULL;
}

void launcher_cover_worker_start(void)
{
    if (g_cover_running) return;
    g_cover_running = true;
    pthread_create(&g_cover_thread, NULL, cover_worker, NULL);
}

void launcher_cover_worker_stop(void)
{
    if (!g_cover_running) return;
    g_cover_running = false;
    pthread_mutex_lock(&g_req_mtx);
    pthread_cond_signal(&g_req_cond);
    pthread_mutex_unlock(&g_req_mtx);
    pthread_join(g_cover_thread, NULL);
}

void launcher_cover_request(const LauncherGame *game)
{
    if (!game || !game->cover_path[0]) return;
    const char *path = game->cover_path;

    /* already cached? */
    if (cover_find_slot(path) >= 0) return;

    /* already pending? */
    pthread_mutex_lock(&g_req_mtx);
    for (int i = 0; i < g_pending_count; i++) {
        if (strcmp(g_pending[i], path) == 0) {
            pthread_mutex_unlock(&g_req_mtx);
            return;
        }
    }
    if (g_pending_count < COVER_QUEUE_SIZE) {
        strncpy(g_pending[g_pending_count++], path, 511);
    }

    if ((g_req_tail - g_req_head) < COVER_QUEUE_SIZE) {
        CoverRequest *r = &g_req_queue[g_req_tail % COVER_QUEUE_SIZE];
        strncpy(r->path,      path,        sizeof(r->path)      - 1);
        strncpy(r->game_name, game->name,  sizeof(r->game_name) - 1);
        strncpy(r->system,    game->system,sizeof(r->system)    - 1);
        g_req_tail++;
        pthread_cond_signal(&g_req_cond);
    }
    pthread_mutex_unlock(&g_req_mtx);
}

int launcher_cover_flush(int limit)
{
    pthread_mutex_lock(&g_res_mtx);
    int processed = 0;
    while (g_res_head < g_res_tail && processed < limit) {
        CoverResult *r = &g_res_queue[g_res_head % COVER_QUEUE_SIZE];
        g_res_head++;

        if (r->img) {
            int slot = cover_lru_evict();
            strncpy(g_cover_cache[slot].path, r->path, sizeof(g_cover_cache[slot].path) - 1);
            g_cover_cache[slot].hash       = path_hash(r->path);
            g_cover_cache[slot].img        = r->img;
            g_cover_cache[slot].last_used  = g_cover_frame;
            g_cover_fade[slot]             = 0;  /* start fade-in */
        }

        /* remove from pending set */
        for (int i = 0; i < g_pending_count; i++) {
            if (strcmp(g_pending[i], r->path) == 0) {
                strncpy(g_pending[i], g_pending[g_pending_count - 1], 511);
                g_pending_count--;
                break;
            }
        }
        processed++;
    }
    pthread_mutex_unlock(&g_res_mtx);

    /* advance fade-in alpha */
    for (int i = 0; i < LAUNCHER_MAX_COVERS; i++) {
        if (g_cover_cache[i].last_used && g_cover_fade[i] < 255) {
            g_cover_fade[i] += 22;
            if (g_cover_fade[i] > 255) g_cover_fade[i] = 255;
        }
    }
    return processed;
}

/* ─── game description fetch (background thread) ────────────────────────── */

static volatile int g_desc_state = LAUNCHER_DESC_IDLE;
static char        g_desc_text[4096] = {};
static char        g_desc_error[256] = {};
static pthread_t   g_desc_thread = 0;

struct DescReq {
    char name[128];
    char system[64];
    char base_dir[512];
};

/* Fetch game description from TGDB overview field, with disk caching.
   Returns true on success, writes text to out_text. Populates err_msg on failure. */
static bool scrape_tgdb_description(const char *game_name, const char *system,
                                     const char *base_dir,
                                     char *out_text, size_t out_sz,
                                     char *err_msg, size_t err_sz)
{
    if (!cfg.tgdb_api_key[0]) {
        strncpy(err_msg, "API key not configured", err_sz - 1);
        return false;
    }

    int plat_id = tgdb_platform_id(system);
    if (!plat_id) {
        strncpy(err_msg, "System not supported", err_sz - 1);
        return false;
    }

    /* build safe filename (identical to cover sanitisation) */
    char safe_name[256];
    strncpy(safe_name, game_name, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';
    for (char *p = safe_name; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
            *p = '_';

    /* create descriptions directory */
    char desc_dir[600];
    snprintf(desc_dir, sizeof(desc_dir), "%s/descriptions", base_dir);
    mkdir(desc_dir, 0755);

    char sys_desc_dir[1024];
    snprintf(sys_desc_dir, sizeof(sys_desc_dir), "%s/%s", desc_dir, system);
    mkdir(sys_desc_dir, 0755);

    /* cache file path */
    char cache_path[2048];
    snprintf(cache_path, sizeof(cache_path), "%s/%s.txt", sys_desc_dir, safe_name);

    /* check cache */
    struct stat cst;
    if (stat(cache_path, &cst) == 0 && cst.st_size > 0) {
        /* read from cache */
        FILE *fp = fopen(cache_path, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (sz > 0 && sz < (long)out_sz - 1) {
                fread(out_text, 1, (size_t)sz, fp);
                out_text[sz] = '\0';
                fclose(fp);
                return true;
            }
            fclose(fp);
        }
    }

    /* build search URL */
    char encoded_name[512];
    url_encode(game_name, encoded_name, sizeof(encoded_name));

    char search_url[1024];
    snprintf(search_url, sizeof(search_url),
             "https://api.thegamesdb.net/v1/Games/ByGameName"
             "?apikey=%s&name=%s&filter[platform]=%d"
             "&fields=overview&page=1",
             cfg.tgdb_api_key, encoded_name, plat_id);

    /* download JSON */
    char json_path[64];
    snprintf(json_path, sizeof(json_path), "/tmp/tgdb_desc_%d.json", (int)getpid());

    if (!exec_curl(search_url, json_path)) {
        remove(json_path);
        strncpy(err_msg, "Failed to download from TGDB", err_sz - 1);
        return false;
    }

    /* read and parse JSON */
    FILE *fp = fopen(json_path, "r");
    if (!fp) {
        remove(json_path);
        strncpy(err_msg, "Failed to read response file", err_sz - 1);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long json_sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (json_sz <= 0 || json_sz > 512 * 1024) {
        fclose(fp);
        remove(json_path);
        strncpy(err_msg, "Invalid response size", err_sz - 1);
        return false;
    }

    char *json = (char *)malloc((size_t)json_sz + 1);
    if (!json) {
        fclose(fp);
        remove(json_path);
        strncpy(err_msg, "Memory allocation failed", err_sz - 1);
        return false;
    }
    fread(json, 1, (size_t)json_sz, fp);
    json[json_sz] = '\0';
    fclose(fp);
    remove(json_path);

    /* parse overview field */
    const char *games_arr = strstr(json, "\"games\":");
    char overview[4096] = {};
    if (games_arr) {
        json_str(games_arr, "overview", overview, sizeof(overview));
    }
    free(json);

    if (!overview[0]) {
        strncpy(err_msg, "No description found for this game", err_sz - 1);
        return false;
    }

    /* cache the description */
    FILE *cf = fopen(cache_path, "w");
    if (cf) {
        fputs(overview, cf);
        fclose(cf);
    }

    strncpy(out_text, overview, out_sz - 1);
    out_text[out_sz - 1] = '\0';
    return true;
}

/* Background thread worker for description fetch */
static void *desc_worker(void *arg)
{
    DescReq *req = (DescReq *)arg;
    char text[4096] = {};
    char error[256] = {};
    bool ok = scrape_tgdb_description(req->name, req->system, req->base_dir, text, sizeof(text), error, sizeof(error));

    if (ok && text[0]) {
        strncpy(g_desc_text, text, sizeof(g_desc_text) - 1);
        g_desc_text[sizeof(g_desc_text) - 1] = '\0';
        g_desc_error[0] = '\0';
        g_desc_state = LAUNCHER_DESC_READY;
    } else {
        if (error[0]) {
            strncpy(g_desc_error, error, sizeof(g_desc_error) - 1);
            g_desc_error[sizeof(g_desc_error) - 1] = '\0';
        } else {
            strncpy(g_desc_error, "No description available.", sizeof(g_desc_error) - 1);
        }
        g_desc_state = LAUNCHER_DESC_NODATA;
    }

    free(req);
    return NULL;
}

/* Public description API */
void launcher_desc_request(const LauncherGame *game, const char *base_dir)
{
    g_desc_state = LAUNCHER_DESC_LOADING;
    g_desc_text[0] = '\0';

    DescReq *r = (DescReq *)malloc(sizeof(DescReq));
    if (!r) return;

    strncpy(r->name, game->name, sizeof(r->name) - 1);
    strncpy(r->system, game->system, sizeof(r->system) - 1);
    strncpy(r->base_dir, base_dir, sizeof(r->base_dir) - 1);

    pthread_t t;
    pthread_create(&t, NULL, desc_worker, r);
    pthread_detach(t);
}

int launcher_desc_state(void)
{
    return g_desc_state;
}

const char *launcher_desc_text(void)
{
    return g_desc_text;
}

const char *launcher_desc_error(void)
{
    return g_desc_error;
}
