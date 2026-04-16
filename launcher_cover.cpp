/*
 * launcher_cover.cpp
 * Cover art worker thread, request/response queues, LRU cache.
 */

#include "launcher.h"
#include "launcher_cover.h"
#include "cfg.h"
#include "lib/imlib2/Imlib2.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

struct CoverRequest { char path[512]; };
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
        g_req_tail++;
        pthread_cond_signal(&g_req_cond);
    }
    pthread_mutex_unlock(&g_req_mtx);
}

static inline int64_t ts_us_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int cover_flush_impl(int limit, int budget_us)
{
    const int64_t start = (budget_us > 0) ? ts_us_now() : -1;

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

        if (start >= 0) {
            int64_t now = ts_us_now();
            if (now >= 0 && (now - start) >= budget_us) break;
        }
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

int launcher_cover_flush(int limit)
{
    return cover_flush_impl(limit, 0);
}

int launcher_cover_flush_budget_us(int limit, int budget_us)
{
    return cover_flush_impl(limit, budget_us);
}
