#ifndef SDL_PNG_H
#define SDL_PNG_H

#include "amiga.h"
#include "video.h"

/* The rectangle v of the frame, each pixel as the oracle's lores capture shows it
 * (denise_capture). v must lie inside the frame buffer. */
int sdl_png_write(const char *path, const struct Amiga *m, const struct SdlView *v);

#endif
