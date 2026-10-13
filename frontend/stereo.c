#include "stereo.h"

#include "amiga.h"

#include <math.h>
#include <string.h>

/* Bauer stereophonic-to-binaural crossfeed (bs2b), computed and applied as libbs2b 3.1.0's
 * src/bs2b.c does (init, cross_feed_d) at its default level BS2B_DEFAULT_CLEVEL: cut frequency
 * 700 Hz, feed 4.5 dB. Each side keeps its own signal through a high-boost stage and adds the
 * other side's through a one-pole low-pass, which at low frequencies arrives 4.5 dB down and
 * about 260 us late (bs2b_level_delay), as a speaker's sound reaches the far ear. */
#define STEREO_FCUT 700.0
#define STEREO_FEED 4.5

static struct
{
	double a0_lo, b1_lo, a0_hi, a1_hi, b1_hi, gain;
	double asis[2], lo[2], hi[2];
	int ready, on;
} stereo;

static int16_t stereo_out[AMIGA_AUDIO_FRAMES * 2];

static void stereo_init(void)
{
	const double pi = 3.14159265358979323846;
	const double gb_lo = STEREO_FEED * -5.0 / 6.0 - 3.0, gb_hi = STEREO_FEED / 6.0 - 3.0;
	const double g_lo = pow(10.0, gb_lo / 20.0), g_hi = 1.0 - pow(10.0, gb_hi / 20.0);
	const double fc_hi = STEREO_FCUT * pow(2.0, (gb_lo - 20.0 * log10(g_hi)) / 12.0);
	double x = exp(-2.0 * pi * STEREO_FCUT / AMIGA_AUDIO_HZ);
	stereo.b1_lo = x;
	stereo.a0_lo = g_lo * (1.0 - x);
	x = exp(-2.0 * pi * fc_hi / AMIGA_AUDIO_HZ);
	stereo.b1_hi = x;
	stereo.a0_hi = 1.0 - g_hi * (1.0 - x);
	stereo.a1_hi = -x;
	stereo.gain = 1.0 / (1.0 - g_hi + g_lo);
	stereo.ready = 1;
}

const int16_t *sdl_stereo_mix(const int16_t *in, uint32_t n, int modern)
{
	uint32_t i;
	int c;
	if(!modern)
	{
		stereo.on = 0;
		return in;
	}
	if(!stereo.ready)
		stereo_init();
	if(!stereo.on)
	{
		memset(stereo.asis, 0, sizeof stereo.asis);
		memset(stereo.lo, 0, sizeof stereo.lo);
		memset(stereo.hi, 0, sizeof stereo.hi);
		stereo.on = 1;
	}
	for(i = 0; i < n * 2; i += 2)
	{
		for(c = 0; c < 2; c++)
		{
			stereo.lo[c] = stereo.a0_lo * in[i + c] + stereo.b1_lo * stereo.lo[c];
			stereo.hi[c] = stereo.a0_hi * in[i + c] + stereo.a1_hi * stereo.asis[c] + stereo.b1_hi * stereo.hi[c];
			stereo.asis[c] = in[i + c];
		}
		for(c = 0; c < 2; c++)
		{
			double v = (stereo.hi[c] + stereo.lo[c ^ 1]) * stereo.gain;
			stereo_out[i + c] = (int16_t)(v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v);
		}
	}
	return stereo_out;
}
