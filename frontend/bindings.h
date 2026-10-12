#ifndef BINDINGS_H
#define BINDINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host keys bound to Amiga commands, in settings.ini under `bind`. Only a game that sets
 * game_def.joystick uses them. A key has at most one command, several keys may share one, and
 * a bound key never reaches the Amiga as a key. Keys are SDL scancodes. */
enum SdlCommand { SdlCommandJoyUp, SdlCommandJoyDown, SdlCommandJoyLeft, SdlCommandJoyRight, SdlCommandJoyFire, SdlCommandCount };
#define SDL_BINDINGS_MAX 32
struct SdlBinding { int scancode; enum SdlCommand command; };
extern const char *const sdl_command_labels[SdlCommandCount];

/* Reads `bind`; a missing key gives the defaults, a bad one too, after printing
 * "settings.ini: ignoring bind=<value>". */
void sdl_bindings_apply(void);
int sdl_bindings_count(void);
const struct SdlBinding *sdl_bindings_get(int i);  /* in the order they were bound */
/* Each change is stored and saved at once. Binding a bound key moves it to `command`; a new key
 * beyond SDL_BINDINGS_MAX is refused (-1). */
int sdl_bindings_add(int scancode, enum SdlCommand command);
void sdl_bindings_remove(int scancode);
void sdl_bindings_reset(void);

/* A key event while the game runs: 1 when the key is bound and so taken from the Amiga. */
int sdl_bindings_key(int scancode, int down);
void sdl_bindings_release(void);  /* every bound key up */
uint8_t sdl_bindings_joystick(void);  /* enum AmigaJoy bits of the bound keys held */

#ifdef __cplusplus
}
#endif

#endif
