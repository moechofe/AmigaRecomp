#ifndef SETTINGS_H
#define SETTINGS_H

/* settings.ini: `key=value` lines. */
int sdl_settings_load(const char *path);        /* 0 (a missing file is empty), -1 unreadable, message printed */
const char *sdl_settings_get(const char *key);  /* NULL when absent */
int sdl_settings_set(const char *key, const char *value);  /* 1 when the stored value changed */
int sdl_settings_save(void);                    /* 0, or -1 after printing "cannot write <path>" */

#endif
