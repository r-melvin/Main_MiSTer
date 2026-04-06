#ifndef CHEATS_H
#define CHEATS_H

void cheats_init(const char *rom_path, uint32_t romcrc);
int cheats_available();
void cheats_scan(int mode);
void cheats_scroll_name();
void cheats_print();
void cheats_toggle();
int cheats_loaded();

/* Per-entry accessors for framebuffer-based UIs (e.g. launcher IGM) */
const char *cheats_get_name(int idx);     /* display name, .gg suffix stripped */
int         cheats_is_enabled(int idx);   /* 1 if enabled, 0 if not */
int         cheats_get_selected(void);    /* current cursor index */
void        cheats_set_selected(int idx); /* move cursor without wrapping */

void cheats_init_arcade(int unit_size, int max_active);
void cheats_add_arcade(const char *name, const char *cheatData, int cheatSize);
void cheats_finalize_arcade();

#endif
