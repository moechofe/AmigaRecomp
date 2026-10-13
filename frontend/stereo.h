#ifndef STEREO_H
#define STEREO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The setting `stereo` applied to Paula's 48 kHz frames, interleaved left and right, on their way
 * to the audio device. With `modern` 0 it returns `in`; else the `n` frames after a crossfeed, in
 * a buffer that holds until the next call. `n` is at most AMIGA_AUDIO_FRAMES. */
const int16_t *sdl_stereo_mix(const int16_t *in, uint32_t n, int modern);

#ifdef __cplusplus
}
#endif

#endif
