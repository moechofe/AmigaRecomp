#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SdlAsset { const char *name; const uint8_t *data; uint32_t size; };
extern const struct SdlAsset sdl_menu_shared[];
extern const struct SdlAsset sdl_menu_game[];
extern uint32_t sdl_menu_path_event;  /* SDL event type; data1 = SDL_strdup'd path from Open… */

int sdl_menu_init(SDL_Window *window);  /* after sdl_video_init; 0, or -1 with "menu: …" printed */
void sdl_menu_quit(void);
int sdl_menu_is_open(void);
void sdl_menu_show(int open);
void sdl_menu_event(SDL_Event *e);
void sdl_menu_message(const char *text);
void sdl_menu_banner(const char *text);
void sdl_menu_refresh(int booted);
void sdl_menu_render(SDL_Window *window);

#ifdef __cplusplus
}
#endif

#endif
