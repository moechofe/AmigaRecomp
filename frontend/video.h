#ifndef VIDEO_H
#define VIDEO_H

#include "amiga.h"

#include <SDL3/SDL.h>

struct SdlView { int x, y, w, h; }; /* the shown part of the frame, in frame pixels */
/* game_def.view in frame pixels, or the whole capture window when the game sets none.
 * -1 when it does not lie inside the frame buffer. */
int sdl_video_view(struct SdlView *v);

struct SdlVideoRect
{
	int x, y, w, h;             /* the view, in window pixels, origin top-left */
	int window_w, window_h;     /* SDL_GetWindowSizeInPixels */
	struct SdlView view;
};
/* 0, or -1 when the window has no pixels (minimised) or the game's view is outside the frame. */
int sdl_video_rect(SDL_Window *window, struct SdlVideoRect *r);

/* The game's view of the frame as one textured quad, centred in the window and scaled by the setting
 * `scale`, OpenGL 3.3 core. Needs the window's context current. */
int sdl_video_init(void);
void sdl_video_draw(SDL_Window *window, const uint32_t frame[AMIGA_FRAME_ROWS][AMIGA_FRAME_STRIDE]);
void sdl_video_quit(void);

#endif
