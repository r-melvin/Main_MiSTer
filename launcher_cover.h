#ifndef LAUNCHER_COVER_H_
#define LAUNCHER_COVER_H_

#include "launcher.h"  /* LauncherGame, LAUNCHER_MAX_COVERS */

#ifdef __cplusplus
#include "lib/imlib2/Imlib2.h"
#endif

void  launcher_cover_worker_start(void);
void  launcher_cover_worker_stop(void);
void  launcher_cover_request(const LauncherGame *game);
Imlib_Image launcher_cover_get(const char *path);
Imlib_Image launcher_cover_get_ex(const char *path, uint32_t *fade_out);
int   launcher_cover_flush(int limit);
int   launcher_cover_flush_budget_us(int limit, int budget_us);
uint32_t launcher_cover_fade_alpha(const char *path);
void  launcher_cover_cache_tick(void);

#endif /* LAUNCHER_COVER_H_ */
