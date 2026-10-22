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
extern const struct SdlAsset sdl_video_shaders[];
enum SdlMenuAction { SdlMenuOpenDisk, SdlMenuImportDisk, SdlMenuStateSave, SdlMenuStateLoad, SdlMenuReset };
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
/* A notification at the window's top right, shown for a few seconds whether or not the menu is
 * open; `error` marks a failure. */
void sdl_menu_notify(const char *text, int error);
void sdl_menu_refresh(int booted);
/* While the Controls page waits for an input (after "Add key…"), key and gamepad button presses
 * go to sdl_menu_bind: an input (bindings.h) binds it to the waiting command, -1 cancels. */
int sdl_menu_binding(void);
void sdl_menu_bind(int input);
/* The welcome card, at the window's top left while the menu is closed, shows from launch until
 * its Close button, and at every launch until a Close with Remember ticked stores `welcome=0` in
 * settings.ini. While the mouse is not captured, the mouse goes to the card when it is over it. */
int sdl_menu_welcome_hover(void);
void sdl_menu_render(SDL_Window *window);

#ifdef __cplusplus
}
#endif

#endif
