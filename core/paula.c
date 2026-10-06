#include "amiga.h"

/* Paula's four DMA audio channels, HRM chapter 5. Zero-order hold, channels 0 + 3 left and
 * 1 + 2 right, no filter, resampled to 48 kHz from the colour clock. */

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
			a->counter = a->per;
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

void paula_audio_run(struct Amiga *m, int cck)
{
	struct Paula *p = &m->paula;
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
			/* Periods below 124 colour clocks exceed what DMA can feed. HRM. */
			a->counter = a->per < 124 ? 124 : a->per;
			a->sample = (int8_t)(a->byte == 0 ? a->word >> 8 : a->word & 0xFF);
			if(++a->byte == 2)
				paula_fetch(m, ch);
		}
		p->audio_write_frac += AMIGA_AUDIO_HZ;
		if(p->audio_write_frac >= AMIGA_CCK_HZ)
		{
			int l = (paula_level(&p->aud[0]) + paula_level(&p->aud[3])) * 2;
			int r = (paula_level(&p->aud[1]) + paula_level(&p->aud[2])) * 2;
			uint32_t w = p->audio_write % AMIGA_AUDIO_FRAMES;
			p->audio_write_frac -= AMIGA_CCK_HZ;
			p->audio[w][0] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
			p->audio[w][1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
			p->audio_write++;
		}
	}
}
