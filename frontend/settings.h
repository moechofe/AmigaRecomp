#ifndef SETTINGS_H
#define SETTINGS_H

#include "game.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* settings.ini: `key=value` lines. */
int sdl_settings_load(const char *path);        /* 0 (a missing file is empty), -1 unreadable, message printed */
const char *sdl_settings_get(const char *key);  /* NULL when absent */
int sdl_settings_set(const char *key, const char *value);  /* 1 when the stored value changed */
int sdl_settings_save(void);                    /* 0, or -1 after printing "cannot write <path>" */

/* The frontend's own settings, stored under their id; a game's hacks under `hack.<id>`. */
enum SdlSetting { SdlSettingMouseCapture, SdlSettingScale, SdlSettingCount };
enum SdlScale { SdlScaleInteger, SdlScaleFloat };  /* the choice indices of `scale` */
extern const struct GameSetting sdl_settings[SdlSettingCount];

int sdl_setting(enum SdlSetting s);
/* `--scale N`: a fixed multiple for this run, 0 for none; settings.ini is not changed. The menu
 * shows no `scale` choice as chosen while it holds; choosing one (sdl_settings_choose) clears it. */
void sdl_scale_force(int n);
int sdl_scale_forced(void);
/* Reads every frontend setting and, when `hacks` is set, every hack of game_def from the table into
 * the values and the machine. A missing or bad key gives the default. */
void sdl_settings_apply(struct Amiga *m, int hacks);
/* Sets one value (a hack when `hack` is nonzero, else a frontend setting), stores it and saves the
 * file when it changed. An out-of-range index or value does nothing. */
void sdl_settings_choose(struct Amiga *m, int hack, uint32_t index, int value);
int sdl_hack_value(const struct Amiga *m, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
