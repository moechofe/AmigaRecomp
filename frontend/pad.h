#ifndef PAD_H
#define PAD_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* Host game controllers working the Amiga's joysticks, beside the bound keys (bindings.h); only in
 * a game that sets game_def.joystick. Each controller takes the first free player as it is
 * connected: player 1 is port 2 and, when game_def.joystick is 2, player 2 is port 1. One that
 * finds no free player joins player 1, as every controller does in a one-joystick game. */
void sdl_pad_init(void);
/* SDL_EVENT_JOYSTICK_ADDED or _REMOVED: opens or closes the device; 1 when `e` was one of them. */
int sdl_pad_event(const SDL_Event *e);
uint8_t sdl_pad_joystick(int port);  /* enum AmigaJoy bits of the controllers on game port `port` (1: port 2, 0: port 1), as held now */

#endif
