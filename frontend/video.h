#ifndef VIDEO_H
#define VIDEO_H

#include "amiga.h"

#include <SDL3/SDL.h>

/* The frame as one textured quad over the whole window, OpenGL 3.3 core. Needs the window's
 * context current. */
int sdl_video_init(void);
void sdl_video_draw(SDL_Window *window, const uint16_t frame[AMIGA_FRAME_H][AMIGA_FRAME_W]);
void sdl_video_quit(void);

#endif
