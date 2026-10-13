#include "amiga.h"
#include "game.h"

/* Bitplanes, sprites and the colour registers, HRM chapters 3, 4 and 6. One line at a time
 * into linebuf, indexed by DIW horizontal coordinate (lores pixels); a hires line is drawn one
 * of its two pixels per lores pixel. */

#define DENISE_X_MAX 456
/* The first line of sprite DMA, where every sprite's control-word fetch restarts: the line after
 * PAL vertical blank, during which there is no sprite DMA. Not measured; docs/runtime.md, "Timing". */
#define DENISE_SPRITE_LINE 25
/* Which pixel of each hires pair a lores pixel shows: the first, as the oracle's lores capture
 * does (docs/runtime.md, "Timing"). */
#define DENISE_HIRES_PIXEL 0

enum SpriteDma
{
	SpriteIdle,
	SpriteControl,
	SpriteWaitStart,
	SpriteData
};

static void denise_sprite_control(struct Amiga *m, struct Sprite *s)
{
	s->pos = mem_chip16(m, s->pt);
	s->ctl = mem_chip16(m, s->pt + 2);
	s->pt = (s->pt + 4) & 0x1FFFFE;
	s->armed = 0;
	s->vstart = ((s->pos >> 8) & 0xFF) | ((s->ctl & 4) << 6);
	s->vstop = ((s->ctl >> 8) & 0xFF) | ((s->ctl & 2) << 7);
	s->dma_state = (s->pos == 0 && s->ctl == 0) ? SpriteIdle : SpriteWaitStart;
}

static void denise_sprite_dma(struct Amiga *m, int line)
{
	int i;
	if((m->paula.dmacon & 0x0220) != 0x0220 || line < DENISE_SPRITE_LINE)
		return;
	if(line == DENISE_SPRITE_LINE && game_def.pointer_sprites)
	{
		for(i = 0; !((game_def.pointer_sprites >> i) & 1); i++)
			;
		m->denise.pointer_list = m->denise.spr[i].pt;
	}
	for(i = 0; i < 8; i++)
	{
		struct Sprite *s = &m->denise.spr[i];
		if(line == DENISE_SPRITE_LINE)
			s->dma_state = SpriteControl;
		if(s->dma_state == SpriteControl)
		{
			denise_sprite_control(m, s);
			continue;
		}
		if(s->dma_state == SpriteWaitStart && line == s->vstart)
			s->dma_state = SpriteData;
		if(s->dma_state == SpriteData)
		{
			if(line == s->vstop)
			{
				denise_sprite_control(m, s);
				if(s->dma_state == SpriteWaitStart && line == s->vstart)
					s->dma_state = SpriteData;
				else
					continue;
			}
			s->data = mem_chip16(m, s->pt);
			s->datb = mem_chip16(m, s->pt + 2);
			s->pt = (s->pt + 4) & 0x1FFFFE;
			s->armed = 1;
		}
	}
}

static int denise_vstop(const struct Denise *d)
{
	/* OCS: DIWSTOP's V8 is the complement of its V7. HRM. */
	int v = (d->diwstop >> 8) & 0xFF;
	return (v & 0x80) ? v : v | 0x100;
}

static int denise_in_vwindow(const struct Denise *d)
{
	return d->line >= ((d->diwstrt >> 8) & 0xFF) && d->line < denise_vstop(d);
}

/* The game's pointer sprite, the lowest in `game_def.pointer_sprites`. The frame pixel at the centre
 * of the pixels its first control words and image describe on the lines the display window
 * shows, read from chip RAM where this frame's sprite DMA started, so a position the game
 * wrote after the fetch counts. Image bit 15 - c of a row is column c at DIW x HSTART + 1 + c,
 * as denise_sprite() draws it; row r is line VSTART + r. A VSTOP below VSTART is never met,
 * so the image runs on to the end of the frame, as denise_sprite_dma() fetches it.
 * Returns 0 with no pointer sprite, sprite DMA off or no pixel shown. */
int denise_pointer(struct Amiga *m, int *x, int *y)
{
	const struct Denise *d = &m->denise;
	uint32_t pt = d->pointer_list;
	uint16_t pos, ctl;
	int vstart, vstop, first, last, h, row, c, x0 = 16, x1 = -1, y0 = -1, y1 = -1;
	if(!game_def.pointer_sprites || (m->paula.dmacon & 0x0220) != 0x0220)
		return 0;
	pos = mem_chip16(m, pt);
	ctl = mem_chip16(m, pt + 2);
	if(pos == 0 && ctl == 0)
		return 0;
	vstart = ((pos >> 8) & 0xFF) | ((ctl & 4) << 6);
	vstop = ((ctl >> 8) & 0xFF) | ((ctl & 2) << 7);
	h = ((pos & 0xFF) << 1) | (ctl & 1);
	first = (d->diwstrt >> 8) & 0xFF;
	if(first < vstart)
		first = vstart;
	last = vstop < vstart ? AMIGA_LINES : vstop;
	if(last > denise_vstop(d))
		last = denise_vstop(d);
	for(row = first - vstart; row < last - vstart; row++)
	{
		uint32_t at = (pt + 4 + 4 * (uint32_t)row) & 0x1FFFFE;
		uint16_t bits = mem_chip16(m, at) | mem_chip16(m, at + 2);
		if(!bits)
			continue;
		if(y0 < 0)
			y0 = row;
		y1 = row;
		for(c = 0; c < 16; c++)
		{
			if(!((bits >> (15 - c)) & 1))
				continue;
			if(c < x0)
				x0 = c;
			if(c > x1)
				x1 = c;
		}
	}
	if(y0 < 0)
		return 0;
	*x = h + 1 + (x0 + x1) / 2 - AMIGA_FRAME_DIW_X0;
	*y = vstart + (y0 + y1) / 2 - AMIGA_FRAME_LINE0;
	return 1;
}

void denise_begin_line(struct Amiga *m, int line)
{
	struct Denise *d = &m->denise;
	d->line = line;
	d->fetched = 0;
	d->drawn_x = 0;
	denise_sprite_dma(m, line);
}

static void denise_fetch(struct Amiga *m)
{
	struct Denise *d = &m->denise;
	int bpu = (d->bplcon0 >> 12) & 7;
	int hires = (d->bplcon0 & 0x8000) != 0;
	int p, w, start = d->ddfstrt & 0xFC, stop = d->ddfstop & 0xFC;
	d->fetched = -1;
	if(d->bplcon0 & 0x0C00)
		amiga_log_once(m, 0x80000000u | (d->bplcon0 & 0x0C00), "BPLCON0 0x%04X: HAM and dual playfield render as a single playfield", d->bplcon0);
	if(bpu > 6)
		bpu = 6;
	if((m->paula.dmacon & 0x0300) != 0x0300 || bpu == 0 || !denise_in_vwindow(d) || stop < start)
		return;
	/* Words per plane: lores (DDFSTOP - DDFSTRT) / 8 + 1, hires / 4 + 2. HRM. */
	d->words = hires ? ((stop - start) >> 2) + 2 : ((stop - start) >> 3) + 1;
	if(d->words > 64)
		d->words = 64;
	for(p = 0; p < bpu; p++)
	{
		for(w = 0; w < d->words; w++)
		{
			d->plane[p][w] = mem_chip16(m, d->bplpt[p]);
			d->bplpt[p] = (d->bplpt[p] + 2) & 0x1FFFFE;
		}
		d->bplpt[p] = (d->bplpt[p] + (uint32_t)(int32_t)((p & 1) ? d->bpl2mod : d->bpl1mod)) & 0x1FFFFE;
	}
	for(; p < 6; p++)
		for(w = 0; w < d->words; w++)
			d->plane[p][w] = 0;
	/* First pixel at DIW DDFSTRT * 2 + 17 lores, + 9 hires, HRM; 0x38 and 0x3C give 0x81. */
	d->first_x = start * 2 + (hires ? 9 : 17);
	d->fetched = 1;
}

static int denise_playfield(const struct Denise *d, int x)
{
	int p, idx = 0;
	int bpu = (d->bplcon0 >> 12) & 7;
	int hires = (d->bplcon0 & 0x8000) != 0;
	if(d->fetched != 1)
		return 0;
	if(bpu > 6)
		bpu = 6;
	for(p = 0; p < bpu; p++)
	{
		/* BPLCON1 delays in lores pixels in both resolutions. HRM. */
		int delay = (p & 1) ? (d->bplcon1 >> 4) & 15 : d->bplcon1 & 15;
		int px = x - d->first_x - delay;
		if(hires)
			px = px * 2 + DENISE_HIRES_PIXEL;
		if(px < 0 || px >= d->words * 16)
			continue;
		idx |= ((d->plane[p][px >> 4] >> (15 - (px & 15))) & 1) << p;
	}
	return idx;
}

/* Highest-priority sprite pixel at x: colour register index, or 0. Pair number in *pair.
 * The sprites in mask `hide` draw nothing; when one is half of an attached pair, the whole
 * pair draws nothing. */
static int denise_sprite(const struct Denise *d, int x, unsigned hide, int *pair)
{
	int i;
	for(i = 0; i < 8; i += 2)
	{
		const struct Sprite *e = &d->spr[i], *o = &d->spr[i + 1];
		int ve = 0, vo = 0;
		int he = ((e->pos & 0xFF) << 1) | (e->ctl & 1);
		int ho = ((o->pos & 0xFF) << 1) | (o->ctl & 1);
		int be = x - he - 1, bo = x - ho - 1;
		int hidden_e = (hide >> i) & 1, hidden_o = (hide >> (i + 1)) & 1, attached = (o->ctl & 0x80) != 0;
		if(attached)
			hidden_e = hidden_o = hidden_e | hidden_o;
		if(e->armed && !hidden_e && be >= 0 && be < 16)
			ve = ((e->data >> (15 - be)) & 1) | (((e->datb >> (15 - be)) & 1) << 1);
		if(o->armed && !hidden_o && bo >= 0 && bo < 16)
			vo = ((o->data >> (15 - bo)) & 1) | (((o->datb >> (15 - bo)) & 1) << 1);
		if(attached)
		{
			if(ve | vo)
			{
				*pair = i >> 1;
				return 16 + ((vo << 2) | ve);
			}
			continue;
		}
		if(ve)
		{
			*pair = i >> 1;
			return 16 + (i >> 1) * 4 + ve;
		}
		if(vo)
		{
			*pair = i >> 1;
			return 16 + (i >> 1) * 4 + vo;
		}
	}
	return 0;
}

void denise_render_to(struct Amiga *m, int hpos)
{
	struct Denise *d = &m->denise;
	int to = hpos * 2 + 1, x;
	int hstart = d->diwstrt & 0xFF, hstop = (d->diwstop & 0xFF) | 0x100;
	int inv;
	if(!d->fetched && hpos >= (d->ddfstrt & 0xFC))
		denise_fetch(m);
	if(to > DENISE_X_MAX)
		to = DENISE_X_MAX;
	if(d->drawn_x < AMIGA_FRAME_DIW_X0)
		d->drawn_x = AMIGA_FRAME_DIW_X0;
	if(d->drawn_x >= to)
		return;
	inv = denise_in_vwindow(d);
	for(x = d->drawn_x; x < to; x++)
	{
		int idx = 0;
		if(inv && x >= hstart && x < hstop)
		{
			int pair = 0, sp = m->sprites_enabled ? denise_sprite(d, x, m->pointer_hidden ? game_def.pointer_sprites : 0u, &pair) : 0;
			idx = denise_playfield(d, x);
			/* Single playfield: PF2P is the playfield's priority code. HRM. */
			if(sp && (idx == 0 || pair < ((d->bplcon2 >> 3) & 7)))
				idx = sp;
		}
		/* Six planes in single playfield are extra half-brite: plane 6 halves the colour the
		 * other five select. HRM, "Extra Half Brite". */
		d->linebuf[x] = idx < 32 ? d->color[idx] : (uint16_t)((d->color[idx & 31] >> 1) & 0x777);
	}
	d->drawn_x = to;
}

void denise_end_line(struct Amiga *m)
{
	struct Denise *d = &m->denise;
	int row = d->line - AMIGA_FRAME_LINE0, x;
	denise_render_to(m, AMIGA_CCK_PER_LINE);
	if(row < 0 || row >= AMIGA_FRAME_H)
		return;
	for(x = 0; x < AMIGA_FRAME_STRIDE; x++)
		m->frame[row][x] = d->linebuf[AMIGA_FRAME_DIW_X0 + x];
}

void denise_write(struct Amiga *m, uint32_t reg, uint16_t v)
{
	struct Denise *d = &m->denise;
	if(reg >= 0x180 && reg <= 0x1BE)
	{
		d->color[(reg - 0x180) >> 1] = v & 0x0FFF;
		return;
	}
	if(reg >= 0x0E0 && reg <= 0x0F6)
	{
		int p = (int)((reg - 0x0E0) >> 2);
		if(reg & 2)
			d->bplpt[p] = (d->bplpt[p] & 0xFFFF0000u) | (v & 0xFFFE);
		else
			d->bplpt[p] = (d->bplpt[p] & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16);
		return;
	}
	if(reg >= 0x120 && reg <= 0x13E)
	{
		struct Sprite *s = &d->spr[(reg - 0x120) >> 2];
		if(reg & 2)
			s->pt = (s->pt & 0xFFFF0000u) | (v & 0xFFFE);
		else
			s->pt = (s->pt & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16);
		return;
	}
	if(reg >= 0x140 && reg <= 0x17E)
	{
		struct Sprite *s = &d->spr[(reg - 0x140) >> 3];
		switch(reg & 6)
		{
		case 0: s->pos = v; break;
		case 2: s->ctl = v; s->armed = 0; break;
		case 4: s->data = v; s->armed = 1; break;
		case 6: s->datb = v; break;
		}
		return;
	}
	switch(reg)
	{
	case 0x08E: d->diwstrt = v; return;
	case 0x090: d->diwstop = v; return;
	case 0x092: d->ddfstrt = v; return;
	case 0x094: d->ddfstop = v; return;
	case 0x100: d->bplcon0 = v; return;
	case 0x102: d->bplcon1 = v; return;
	case 0x104: d->bplcon2 = v; return;
	case 0x108: d->bpl1mod = (int16_t)v; return;
	case 0x10A: d->bpl2mod = (int16_t)v; return;
	default:
		amiga_log_once(m, 0x90000000u | reg, "display register 0x%03X write ignored", (unsigned)reg);
		return;
	}
}
