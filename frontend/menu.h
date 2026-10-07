#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>
#include <stdint.h>

struct Amiga;

#ifdef __cplusplus
extern "C" {
#endif

struct SdlAsset { const char *name; const uint8_t *data; uint32_t size; };
extern const struct SdlAsset sdl_menu_shared[];
extern const struct SdlAsset sdl_menu_game[];
enum SdlMenuAction { SdlMenuOpenDisk, SdlMenuImportDisk, SdlMenuStateSave, SdlMenuStateLoad };
/* SDL event type. user.code = enum SdlMenuAction; data1 = SDL_strdup'd path, or NULL for the
 * default state file (the main loop SDL_frees it); data2 = the role index (SdlMenuImportDisk). */
extern uint32_t sdl_menu_action_event;

int sdl_menu_init(SDL_Window *window, struct Amiga *m);  /* after sdl_video_init; 0, or -1 with "menu: …" printed */
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
