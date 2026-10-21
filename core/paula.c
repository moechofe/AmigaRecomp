#include "amiga.h"

/* Paula's four DMA audio channels, HRM chapter 5. Zero-order hold, channels 0 + 3 left and
 * 1 + 2 right. Each 48 kHz frame is the mean of the colour clocks it covers, then passes the
 * A500's fixed low-pass, as WinUAE's audio.cpp models it ("Emulated (A500)", LED filter off):
 * one-pole RC stages at 6200 Hz and 20 000 Hz. */

#if AMIGA_AUDIO_HZ != 48000u
#error "PAULA_RC_6200 and PAULA_RC_20000 are for 48 kHz"
#endif
/* A one-pole stage's a0 in Q16: 1 / (1 + 1 / (2 tan(pi fc / 48000))), WinUAE's rc_calculate_a0. */
#define PAULA_RC_6200 30288
#define PAULA_RC_20000 57793

/* A period in colour clocks; AUDxPER 0 is 65 536, as in WinUAE's AUDxPER. DMA gives a channel one
 * word, two samples, a line, so no period runs faster than half a line; ProTracker's lowest, 113,
 * is not raised. */
static uint32_t paula_period(const struct AudioChannel *a)
{
	uint32_t per = a->per ? a->per : 0x10000u;
	return per < AMIGA_CCK_PER_LINE / 2 ? AMIGA_CCK_PER_LINE / 2 : per;
}

static void paula_reload(struct Amiga *m, int ch)
{
	struct AudioChannel *a = &m->paula.aud[ch];
	a->ptr = a->lc;
	a->remain = a->len ? a->len : 0x10000u;
	custom_set_intreq(m, (uint16_t)(0x0080 << ch));
}

static void paula_fetch(struct Amiga *m, int ch)
{
	struct AudioChannel *a = &m->paula.aud[ch];
	if(a->remain == 0)
		paula_reload(m, ch);
	a->word = mem_chip16(m, a->ptr);
	a->ptr = (a->ptr + 2) & 0x1FFFFE;
	a->remain--;
	a->byte = 0;
}

void paula_dmacon_changed(struct Amiga *m, uint16_t old)
{
	int ch;
	for(ch = 0; ch < 4; ch++)
	{
		int was = (old & 0x0200) && (old & (1 << ch));
		int now = (m->paula.dmacon & 0x0200) && (m->paula.dmacon & (1 << ch));
		struct AudioChannel *a = &m->paula.aud[ch];
		if(now && !was)
		{
			paula_reload(m, ch);
			paula_fetch(m, ch);
			a->counter = paula_period(a);
			a->active = 1;
		}
		else if(!now && was)
		{
			a->active = 0;
			a->sample = 0;
		}
	}
}

void paula_audio_write(struct Amiga *m, int ch, int reg, uint16_t v)
{
	struct AudioChannel *a = &m->paula.aud[ch];
	switch(reg)
	{
	case 0x0: a->lc = (a->lc & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); break;
	case 0x2: a->lc = (a->lc & 0xFFFF0000u) | (v & 0xFFFE); break;
	case 0x4: a->len = v; break;
	case 0x6: a->per = v; break;
	case 0x8: a->vol = v & 0x7F; break;
	case 0xA:
		a->dat = v;
		if(!a->active)
			amiga_log_once(m, 0xB0000000u, "audio written by the CPU (AUDxDAT), not emulated");
		break;
	default:
		break;
	}
}

static int paula_level(const struct AudioChannel *a)
{
	int vol = a->vol > 64 ? 64 : a->vol;
	return a->sample * vol;
}

/* One RC stage; the state is Q8. */
static int32_t paula_rc(int32_t *state, int32_t in_q8, int32_t a0)
{
	*state += (int32_t)(((int64_t)a0 * (in_q8 - *state)) >> 16);
	return *state;
}

static int16_t paula_out(struct PaulaOutput *o, int side)
{
	int32_t v = o->sum[side] * 2 / (int32_t)o->cck;
	v = paula_rc(&o->rc[side][1], paula_rc(&o->rc[side][0], v * 256, PAULA_RC_6200), PAULA_RC_20000) / 256;
	return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

void paula_audio_run(struct Amiga *m, int cck)
{
	struct Paula *p = &m->paula;
	struct PaulaOutput *o = &m->audio_out;
	int i, ch;
	for(i = 0; i < cck; i++)
	{
		for(ch = 0; ch < 4; ch++)
		{
			struct AudioChannel *a = &p->aud[ch];
			if(!a->active)
				continue;
			if(a->counter > 1)
			{
				a->counter--;
				continue;
			}
			a->counter = paula_period(a);
			a->sample = (int8_t)(a->byte == 0 ? a->word >> 8 : a->word & 0xFF);
			if(++a->byte == 2)
				paula_fetch(m, ch);
		}
		o->sum[0] += paula_level(&p->aud[0]) + paula_level(&p->aud[3]);
		o->sum[1] += paula_level(&p->aud[1]) + paula_level(&p->aud[2]);
		o->cck++;
		p->audio_write_frac += AMIGA_AUDIO_HZ;
		if(p->audio_write_frac >= AMIGA_CCK_HZ)
		{
			uint32_t w = p->audio_write % AMIGA_AUDIO_FRAMES;
			p->audio_write_frac -= AMIGA_CCK_HZ;
			p->audio[w][0] = paula_out(o, 0);
			p->audio[w][1] = paula_out(o, 1);
			o->sum[0] = o->sum[1] = 0;
			o->cck = 0;
			p->audio_write++;
		}
	}
}
