#ifndef SDL_PNG_H
#define SDL_PNG_H

#include "amiga.h"

int sdl_png_write(const char *path, const uint16_t frame[AMIGA_FRAME_ROWS][AMIGA_FRAME_STRIDE]);

#endif
