#ifndef SDL_DESKTOP_H
#define SDL_DESKTOP_H

#include <SDL3/SDL.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's packaging/<id>.png and <id>.desktop, compiled into every executable but Windows'
 * (which carries <id>.ico as a resource instead). docs/runtime.md, "Desktop integration". */
void sdl_desktop_window_icon(SDL_Window *window);

/* The applications-menu entry: offered only on Linux, when the game runs from an AppImage. */
int sdl_desktop_offered(void);
int sdl_desktop_installed(void);
/* 0 or -1; `msg` gets the sentence to show either way. */
int sdl_desktop_install(char *msg, size_t n);
int sdl_desktop_remove(char *msg, size_t n);

#ifdef __cplusplus
}
#endif

#endif
