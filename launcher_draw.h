#ifndef LAUNCHER_DRAW_H
#define LAUNCHER_DRAW_H

#include "launcher.h"

/* init / shutdown */
void launcher_draw_init(void);
void launcher_draw_shutdown(void);

/* clear & primitives */
void launcher_clear(Imlib_Image img, uint32_t argb);
void launcher_fill_rect(Imlib_Image img, int x, int y, int w, int h, uint32_t argb);
void launcher_fill_rect_rounded(Imlib_Image img, int x, int y, int w, int h, int r, uint32_t argb);

/* text */
void launcher_draw_text(Imlib_Image img, int x, int y, const char *text, int font_id, uint32_t argb);
int  launcher_draw_text_centred(Imlib_Image img, int cx, int y, const char *text, int font_id, uint32_t argb);
int  launcher_text_width(const char *text, int font_id);
int  launcher_text_height(int font_id);
void launcher_draw_text_clipped(Imlib_Image img, int x, int y, const char *text, int font_id, uint32_t argb, int max_w);

/* images */
void launcher_blit_image(Imlib_Image dst, Imlib_Image src, int dx, int dy, int dw, int dh);

/* effects */
void launcher_draw_glow(Imlib_Image img, int x, int y, int w, int h, uint32_t argb, int rings);
void launcher_draw_fade(Imlib_Image img, int alpha);
void launcher_draw_spinner(Imlib_Image img, int cx, int cy, int radius, float angle, uint32_t argb);
void launcher_draw_fav_badge(Imlib_Image img, int x, int y);

/* particles */
void launcher_particles_init(int sw, int sh);
void launcher_particles_update(int sh);
void launcher_particles_draw(Imlib_Image img);

/* placeholder cover (caller calls imlib_context_set_image+imlib_free_image) */
Imlib_Image launcher_make_placeholder(float pulse);

/* performance overlay (FPS counter) */
void launcher_draw_performance(Imlib_Image img, const uint32_t *frame_times, int frame_count, int screen_w, int screen_h);

/* help screen */
void launcher_draw_help(Imlib_Image img, int screen_w, int screen_h);

/* game metadata display */
void launcher_draw_game_metadata(Imlib_Image img, const LauncherGame *game, int x, int y);
int  launcher_game_metadata_width(const LauncherGame *game);

/* settings menu */
void launcher_draw_settings(Imlib_Image img, int screen_w, int screen_h, int selected,
                            uint8_t particles, const char *theme, uint8_t show_perf);

/* cover batch download progress modal */
void launcher_draw_cover_dl(Imlib_Image img, int sw, int sh, int done, int total);

/* bulk select badge */
void launcher_draw_sel_badge(Imlib_Image img, int x, int y);

/* version/region selector modal */
void launcher_draw_version_select(Imlib_Image img, int sw, int sh,
                                   const LauncherGame *games,
                                   const int *indices, int count, int sel);

/* variant badge */
void launcher_draw_variant_badge(Imlib_Image img, int x, int y, const char *label);

/* game description overlay */
void launcher_draw_description(Imlib_Image img, int sw, int sh,
                                const char *game_name, const char *system,
                                const char *text, int state, int scroll, uint32_t frame_time,
                                const char *error_msg);

/* rating modal */
void launcher_draw_rating_modal(Imlib_Image img, int sw, int sh,
                                 const char *game_name, int sel, int current);

#endif /* LAUNCHER_DRAW_H */
