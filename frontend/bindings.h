#ifndef BINDINGS_H
#define BINDINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host keys and gamepad buttons bound to Amiga commands, keys in settings.ini under `bind`,
 * buttons under `bind_pad`. Only a game that sets game_def.joystick uses them. An input has at
 * most one command, several inputs may share one, and a bound key never reaches the Amiga as a
 * key. An input is an SDL scancode, or SDL_BIND_PAD(button) for an SDL gamepad button, which
 * every gamepad shares. */
enum SdlCommand { SdlCommandJoyUp, SdlCommandJoyDown, SdlCommandJoyLeft, SdlCommandJoyRight, SdlCommandJoyFire, SdlCommandJoyFire2, SdlCommandCount };
#define SDL_BINDINGS_MAX 32
#define SDL_BIND_PAD(button) (512 + (button)) /* past every SDL scancode (SDL_SCANCODE_COUNT) */
struct SdlBinding { int input; enum SdlCommand command; };
extern const char *const sdl_command_labels[SdlCommandCount];

/* Reads `bind` and `bind_pad`; a missing one gives its defaults, a bad one too, after printing
 * "settings.ini: ignoring <key>=<value>". */
void sdl_bindings_apply(void);
int sdl_bindings_count(void);
const struct SdlBinding *sdl_bindings_get(int i);  /* in the order they were bound */
/* Each change is stored and saved at once. Binding a bound input moves it to `command`; a new
 * input beyond SDL_BINDINGS_MAX, or one that cannot be bound, is refused (-1). */
int sdl_bindings_add(int input, enum SdlCommand command);
void sdl_bindings_remove(int input);
void sdl_bindings_reset(void);
/* The name the Controls page shows for an input: "Pad A" style for a gamepad button. */
void sdl_bindings_name(int input, char *out, int size);
/* An SDL gamepad button that may be bound: not the D-pad, which steers, nor Guide. */
int sdl_bindings_pad_button(int button);

/* The host key that opens the menu, an SDL scancode: game_def.menu_key, a USB HID usage ID,
 * which SDL's scancodes are; F1 when the game names none. */
int sdl_menu_key(void);

/* A key event while the game runs: 1 when the key is bound and so taken from the Amiga. */
int sdl_bindings_key(int scancode, int down);
void sdl_bindings_release(void);  /* every bound key up */
uint8_t sdl_bindings_joystick(void);  /* enum AmigaJoy bits of the bound keys held */
/* enum AmigaJoy bits of the bound buttons of `pad` held now. */
struct SDL_Gamepad;
uint8_t sdl_bindings_gamepad(struct SDL_Gamepad *pad);

#ifdef __cplusplus
}
#endif

#endif
