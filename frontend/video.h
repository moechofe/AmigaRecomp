#ifndef VIDEO_H
#define VIDEO_H

#include "amiga.h"

#include <SDL3/SDL.h>

struct SdlVideoRect
{
	int x, y, w, h;             /* the frame, in window pixels, origin top-left */
	int window_w, window_h;     /* SDL_GetWindowSizeInPixels */
};
/* 0, or -1 when the window has no pixels (minimised). */
int sdl_video_rect(SDL_Window *window, struct SdlVideoRect *r);

/* The frame as one textured quad, centred in the window and scaled by the setting `scale`, OpenGL
 * 3.3 core. Needs the window's context current. */
int sdl_video_init(void);
void sdl_video_draw(SDL_Window *window, const uint16_t frame[AMIGA_FRAME_H][AMIGA_FRAME_W]);
void sdl_video_quit(void);

#endif
