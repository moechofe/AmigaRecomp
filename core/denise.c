#include "amiga.h"
#include "game.h"

/* Bitplanes, sprites and the colour registers, HRM chapters 3, 4 and 6, and on the A1200 the AGA
 * registers of Commodore's AA documentation (AGA.guide). One line at a time into linebuf, as
 * 0x00RRGGBB, indexed by DIW horizontal coordinate (lores pixels); a hires line is drawn one of
 * its two pixels per lores pixel. docs/runtime.md, "Timing". */

#define DENISE_X_MAX 456
/* The first line of sprite DMA, where every sprite's control-word fetch restarts: the line after
 * PAL vertical blank, during which there is no sprite DMA. */
#define DENISE_SPRITE_LINE AMIGA_DISPLAY_LINE0
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

/* A 12-bit 0xRGB as 0x00RRGGBB, each gun's nibble repeated in its low nibble: what an AGA colour
 * write with LOCT clear stores (AGA.guide, COLORxx), and an OCS colour at 8 bits a gun. */
static uint32_t denise_rgb(uint16_t v)
{
	return (((uint32_t)(v >> 8) & 15) << 16 | ((uint32_t)(v >> 4) & 15) << 8 | (v & 15)) * 0x11u;
}

/* The fetch width as a power of two of 16 bits from FMODE's BPL32/BPAGEM (shift 0) or
 * SPR32/SPAGEM (shift 2): 4 bytes with either bit, 8 with both (AGA.guide, FMODE). */
static int denise_fm(const struct Denise *d, int shift)
{
	int bits = AMIGA_A1200 ? (d->fmode >> shift) & 3 : 0;
	return bits == 3 ? 2 : bits ? 1 : 0;
}

static int denise_sprite_words(const struct Denise *d)
{
	return 1 << denise_fm(d, 2);
}

/* AGA adds SV9 and EV9 (AGA.guide, SPRxCTL). */
static int denise_sprite_vstart(uint16_t pos, uint16_t ctl)
{
	int v = ((pos >> 8) & 0xFF) | ((ctl & 4) << 6);
	if(AMIGA_A1200)
		v |= (ctl & 0x40) << 3;
	return v;
}

static int denise_sprite_vstop(uint16_t ctl)
{
	int v = ((ctl >> 8) & 0xFF) | ((ctl & 2) << 7);
	if(AMIGA_A1200)
		v |= (ctl & 0x20) << 4;
	return v;
}

/* w consecutive chip words from at, the first in the high bits. */
static uint64_t denise_chip_words(struct Amiga *m, uint32_t at, int w)
{
	uint64_t v = 0;
	int i;
	for(i = 0; i < w; i++)
		v = (v << 16) | mem_chip16(m, at + 2 * (uint32_t)i);
	return v;
}

/* A wide fetch reads an aligned 4 or 8 bytes (WinUAE custom.cpp, fetch32_bpl, fetch32_spr and
 * fetch64); what it returns for a pointer that is not aligned is not modelled. */
static void denise_check_align(struct Amiga *m, uint32_t pt, int w, uint32_t key, const char *what)
{
	if(w > 1 && (pt & (2 * (uint32_t)w - 1)))
		amiga_log_once(m, key, "%s pointer 0x%06X is not aligned to its %d-byte fetch: read as consecutive words",
			what, (unsigned)pt, 2 * w);
}

/* A wide sprite's control words are the first words of its first two fetches (WinUAE
 * custom.cpp, the sprite DMA slots). */
static void denise_sprite_control(struct Amiga *m, struct Sprite *s, int w)
{
	denise_check_align(m, s->pt, w, 0x91000000u, "sprite");
	s->pos = mem_chip16(m, s->pt);
	s->ctl = mem_chip16(m, s->pt + 2 * (uint32_t)w);
	s->pt = (s->pt + 4 * (uint32_t)w) & 0x1FFFFE;
	s->armed = 0;
	s->vstart = denise_sprite_vstart(s->pos, s->ctl);
	s->vstop = denise_sprite_vstop(s->ctl);
	s->dma_state = (s->pos == 0 && s->ctl == 0) ? SpriteIdle : SpriteWaitStart;
}

static void denise_sprite_dma(struct Amiga *m, int line)
{
	int i, w = denise_sprite_words(&m->denise);
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
			denise_sprite_control(m, s, w);
			continue;
		}
		if(s->dma_state == SpriteWaitStart && line == s->vstart)
			s->dma_state = SpriteData;
		if(s->dma_state == SpriteData)
		{
			if(line == s->vstop)
			{
				denise_sprite_control(m, s, w);
				if(s->dma_state == SpriteWaitStart && line == s->vstart)
					s->dma_state = SpriteData;
				else
					continue;
			}
			denise_check_align(m, s->pt, w, 0x91000000u, "sprite");
			s->data = denise_chip_words(m, s->pt, w);
			s->datb = denise_chip_words(m, s->pt + 2 * (uint32_t)w, w);
			s->pt = (s->pt + 4 * (uint32_t)w) & 0x1FFFFE;
			s->armed = 1;
		}
	}
}

/* The display window. With DIWHIGH written since the last DIWSTRT or DIWSTOP write its V10-V8
 * and H10 bits complete the positions; otherwise the OCS rules hold: start V8 and H8 0, stop V8
 * the complement of stop V7, stop H8 1 (HRM; AGA.guide, DIWHIGH and "Compatibility"). DIWHIGH's
 * H1 and H0, 70 and 35 ns steps, lie below the frame's lores pixel. */
static int denise_vstart(const struct Denise *d)
{
	int v = (d->diwstrt >> 8) & 0xFF;
	if(AMIGA_A1200 && d->diwhigh_written)
		v |= (d->diwhigh & 7) << 8;
	return v;
}

static int denise_vstop(const struct Denise *d)
{
	int v = (d->diwstop >> 8) & 0xFF;
	if(AMIGA_A1200 && d->diwhigh_written)
		return v | ((d->diwhigh >> 8) & 7) << 8;
	return (v & 0x80) ? v : v | 0x100;
}

static int denise_hstart(const struct Denise *d)
{
	int h = d->diwstrt & 0xFF;
	if(AMIGA_A1200 && d->diwhigh_written)
		h |= ((d->diwhigh >> 5) & 1) << 8;
	return h;
}

static int denise_hstop(const struct Denise *d)
{
	int h = d->diwstop & 0xFF;
	if(AMIGA_A1200 && d->diwhigh_written)
		return h | ((d->diwhigh >> 13) & 1) << 8;
	return h | 0x100;
}

static int denise_in_vwindow(const struct Denise *d)
{
	return d->line >= denise_vstart(d) && d->line < denise_vstop(d);
}

/* The pointer the frontend steers: on the A1200, while an EasyRequestArgs requester is up, the
 * HLE's own pointer's hot spot (hle_con_pointer), whether drawn or not. Otherwise the game's
 * pointer sprite, the lowest in `game_def.pointer_sprites`: the frame pixel at the centre
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
	int w = denise_sprite_words(d), width = 16 * w;
	int vstart, vstop, first, last, h, row, c, x0 = width, x1 = -1, y0 = -1, y1 = -1;
#if AMIGA_A1200
	if(hle_con_pointer(m, x, y))
	{
		*x -= AMIGA_FRAME_DIW_X0;
		*y -= AMIGA_DISPLAY_LINE0;
		return 1;
	}
#endif
	if(!game_def.pointer_sprites || (m->paula.dmacon & 0x0220) != 0x0220)
		return 0;
	pos = mem_chip16(m, pt);
	ctl = mem_chip16(m, pt + 2 * (uint32_t)w);
	if(pos == 0 && ctl == 0)
		return 0;
	vstart = denise_sprite_vstart(pos, ctl);
	vstop = denise_sprite_vstop(ctl);
	h = ((pos & 0xFF) << 1) | (ctl & 1);
	first = denise_vstart(d);
	if(first < vstart)
		first = vstart;
	last = vstop < vstart ? AMIGA_LINES : vstop;
	if(last > denise_vstop(d))
		last = denise_vstop(d);
	for(row = first - vstart; row < last - vstart; row++)
	{
		uint32_t at = (pt + 4 * (uint32_t)w * (uint32_t)(row + 1)) & 0x1FFFFE;
		uint64_t bits = denise_chip_words(m, at, w) | denise_chip_words(m, at + 2 * (uint32_t)w, w);
		if(!bits)
			continue;
		if(y0 < 0)
			y0 = row;
		y1 = row;
		for(c = 0; c < width; c++)
		{
			if(!((bits >> (width - 1 - c)) & 1))
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
	*y = vstart + (y0 + y1) / 2 - AMIGA_DISPLAY_LINE0;
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

/* A fetch block in colour clocks: 8 with 16-bit fetches; 16 and 32 in lores and 8 and 16 in hires
 * with 32- and 64-bit ones (WinUAE custom.cpp, fetchunits). */
static int denise_block(int fm, int hires)
{
	return hires ? (fm == 2 ? 16 : 8) : 8 << fm;
}

/* The colour clocks from one fetch of a plane to its next: 8, 16, 32 in lores and 4, 8, 16 in
 * hires (WinUAE custom.cpp, fetchstarts). */
static int denise_fetch_period(int fm, int hires)
{
	return (hires ? 4 : 8) << fm;
}

/* The colour clock of the first fetch block. 16-bit fetches: in lores the block starts at DDFSTRT,
 * in hires blocks fall on multiples of 8 and DDFSTRT starts the next one (docs/runtime.md,
 * "Timing"). Wider fetches start at DDFSTRT [INFERENCE: WinUAE custom.cpp starts the AGA bitplane
 * sequencer at the DDFSTRT match]. */
static int denise_ddf_start(const struct Denise *d)
{
	if(denise_fm(d, 0))
		return d->ddfstrt & 0xFC;
	return (d->bplcon0 & 0x8000) ? (d->ddfstrt + 7) & 0xF8 : d->ddfstrt & 0xFC;
}

/* The planes BPLCON0 fetches and shows. OCS shows at most 6. AGA adds BPU3 for 8; a BPU code above
 * 8 is undefined (AGA.guide, BPLCON0) and fetches none, as do more planes than the fetch period
 * has slots, 4 in hires with 16-bit fetches (WinUAE custom.h, GET_PLANES; custom.cpp,
 * real_bitplane_number). */
static int denise_planes(const struct Denise *d)
{
	int bpu = (d->bplcon0 >> 12) & 7;
	if(!AMIGA_A1200)
		return bpu > 6 ? 6 : bpu;
	if(d->bplcon0 & 0x0010)
		bpu = bpu ? 0 : 8;
	if((d->bplcon0 & 0x8000) && !denise_fm(d, 0) && bpu > 4)
		bpu = 0;
	return bpu;
}

/* Whether this line's bitplane DMA takes colour clock `hpos`, one the copper fetches in (it
 * fetches at even colour clocks, docs/runtime.md, "Timing"). A 16-bit fetch block gives its
 * even colour clocks 2 and 6 to planes 6 and 5 in lores, and 0 and 4 to plane 4 and 2 and 6 to
 * plane 3 in hires (HRM, Figures 6-11 and 6-12, "Time Slots Used by a Six Bitplane Display" and
 * "by a High Resolution Display"); its odd ones go to the other planes. Wider AGA fetches are not
 * modelled and take none. */
int denise_copper_blocked(const struct Amiga *m, int hpos)
{
	const struct Denise *d = &m->denise;
	int bpu = denise_planes(d), start = denise_ddf_start(d), stop = d->ddfstop & 0xFC, k;
	if(denise_fm(d, 0) || (m->paula.dmacon & 0x0300) != 0x0300 || bpu == 0 || !denise_in_vwindow(d) || stop < start)
		return 0;
	if(hpos < start || hpos >= start + ((stop - start + 7) / 8 + 1) * 8)
		return 0;
	k = (hpos - start) & 7;
	if(d->bplcon0 & 0x8000)
		return ((k == 0 || k == 4) && bpu >= 4) || ((k == 2 || k == 6) && bpu >= 3);
	return (k == 2 && bpu >= 6) || (k == 6 && bpu >= 5);
}

static void denise_fetch(struct Amiga *m)
{
	struct Denise *d = &m->denise;
	int bpu = denise_planes(d);
	int hires = (d->bplcon0 & 0x8000) != 0;
	int fm = denise_fm(d, 0), block = denise_block(fm, hires), per_block = hires ? block / 4 : block / 8;
	int p, w, start = denise_ddf_start(d), stop = d->ddfstop & 0xFC;
	d->fetched = -1;
	if(d->bplcon0 & 0x0C00)
		amiga_log_once(m, 0x80000000u | (d->bplcon0 & 0x0C00), "BPLCON0 0x%04X: HAM and dual playfield render as a single playfield", d->bplcon0);
	if(AMIGA_A1200 && (d->bplcon0 & 0x0040))
	{
		amiga_log_once(m, 0x92000000u, "BPLCON0 0x%04X: super-hires bitplanes are not drawn", d->bplcon0);
		return;
	}
	if((m->paula.dmacon & 0x0300) != 0x0300 || bpu == 0 || !denise_in_vwindow(d) || stop < start)
		return;
	/* The fetch runs in blocks up to and including the first block that starts at or after
	 * DDFSTOP; a block fetches one word per plane per 8 colour clocks in lores, two in hires.
	 * With 16-bit fetches and DDFSTOP on a block's start that is the HRM's (DDFSTOP - DDFSTRT) / 8
	 * + 1 lores and / 4 + 2 hires words (docs/runtime.md, "Timing"); wider fetches count blocks the
	 * same way (WinUAE custom.cpp, the line's bpllen). */
	d->words = ((stop - start + block - 1) / block + 1) * per_block;
	if(d->words > 64)
		d->words = 64;
	for(p = 0; p < bpu; p++)
	{
		denise_check_align(m, d->bplpt[p], 1 << fm, 0x93000000u, "bitplane");
		for(w = 0; w < d->words; w++)
			d->plane[p][w] = mem_chip16(m, d->bplpt[p] + 2 * (uint32_t)w);
		d->bplpt[p] = (d->bplpt[p] + 2 * (uint32_t)d->words) & 0x1FFFFE;
	}
	/* The modulos are added once the line's last fetch has ended, so a modulo written during the
	 * line's fetch applies to that line (docs/runtime.md, "Timing"); AGA's last fetch ends 8 colour
	 * clocks into the last block (WinUAE custom.cpp, islastbplseq). */
	d->mod_at = start + (d->words / per_block - 1) * block + 8;
	for(; p < AMIGA_PLANES; p++)
		for(w = 0; w < d->words; w++)
			d->plane[p][w] = 0;
	/* First pixel at DIW block * 2 + 17 in lores, so DDFSTRT 0x38 gives 0x81; in hires with 16-bit
	 * fetches at DDFSTRT * 2 + 9, in steps of 4 colour clocks, so 0x3C gives 0x81 and 0x38 0x79
	 * (HRM; docs/runtime.md, "Timing"). Wider hires fetches load plane 1 in the eighth colour clock
	 * of the block, as lores does, so at block * 2 + 17 too (WinUAE custom.cpp, cycle_sequences). */
	d->first_x = hires && !fm ? (d->ddfstrt & 0xFC) * 2 + 9 : start * 2 + 17;
	/* Denise loads the planes every fetch period counted from DIW 0x81 (colour clock 0x18): the
	 * delay is counted from that grid (WinUAE drawing.cpp, delayoffset). */
	d->load_lead = 2 * (((d->ddfstrt & 0xFC) - 0x18) & (denise_fetch_period(fm, hires) - 1));
	d->fetched = 1;
}

/* A playfield's BPLCON1 delay in lores pixels: 4 bits on OCS, 6 on AGA (PFxH7-PFxH2); PFxH1 and
 * PFxH0 lie below the lores frame's pixel, as in the oracle's lores output (AGA.guide, BPLCON1;
 * WinUAE drawing.cpp, subpixel handling). */
static int denise_delay(const struct Denise *d, int pf2)
{
	int v = d->bplcon1;
	if(!AMIGA_A1200)
		return (pf2 ? v >> 4 : v) & 15;
	return pf2 ? ((v >> 4) & 15) | ((v >> 10) & 0x30) : (v & 15) | ((v >> 6) & 0x30);
}

static int denise_playfield(const struct Denise *d, int x)
{
	int p, idx = 0;
	int bpu = denise_planes(d);
	int hires = (d->bplcon0 & 0x8000) != 0;
	int period = 2 * denise_fetch_period(denise_fm(d, 0), hires);
	if(d->fetched != 1)
		return 0;
	for(p = 0; p < bpu; p++)
	{
		/* BPLCON1 delays in lores pixels in both resolutions, modulo the lores pixels between two
		 * loads of a plane: 16 in lores and 8 in hires with 16-bit fetches. In lores the planes start
		 * on that grid from DIW 0x81 plus the delay, at the first such start at or after the first
		 * block's pixel (docs/runtime.md, "Timing"). */
		int off = (denise_delay(d, p & 1) - d->load_lead) & (period - 1);
		int px = hires ? (x - d->first_x - off) * 2 + DENISE_HIRES_PIXEL : x - d->first_x - off;
		if(px < 0 || px >= d->words * 16)
			continue;
		idx |= ((d->plane[p][px >> 4] >> (15 - (px & 15))) & 1) << p;
	}
	return idx;
}

/* The first colour of the sprites' 16: 16 on OCS; on AGA BPLCON4's ESPRM for even sprites and
 * OSPRM for odd and attached ones (AGA.guide, BPLCON4 and "Sprites"). */
static int denise_sprite_base(const struct Denise *d, int odd)
{
	if(!AMIGA_A1200)
		return 16;
	return ((odd ? d->bplcon4 : d->bplcon4 >> 4) & 15) << 4;
}

/* Highest-priority sprite pixel at x: colour register index, or 0. Pair number in *pair.
 * The sprites in mask `hide` draw nothing; when one is half of an attached pair, the whole
 * pair draws nothing. */
static int denise_sprite(const struct Denise *d, int x, unsigned hide, int *pair)
{
	int i, width = 16 * denise_sprite_words(d);
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
		if(e->armed && !hidden_e && be >= 0 && be < width)
			ve = (int)((e->data >> (width - 1 - be)) & 1) | (int)(((e->datb >> (width - 1 - be)) & 1) << 1);
		if(o->armed && !hidden_o && bo >= 0 && bo < width)
			vo = (int)((o->data >> (width - 1 - bo)) & 1) | (int)(((o->datb >> (width - 1 - bo)) & 1) << 1);
		if(attached)
		{
			if(ve | vo)
			{
				*pair = i >> 1;
				return denise_sprite_base(d, 1) + ((vo << 2) | ve);
			}
			continue;
		}
		if(ve)
		{
			*pair = i >> 1;
			return denise_sprite_base(d, 0) + (i >> 1) * 4 + ve;
		}
		if(vo)
		{
			*pair = i >> 1;
			return denise_sprite_base(d, 1) + (i >> 1) * 4 + vo;
		}
	}
	return 0;
}

/* The colour of playfield colour number idx. Six planes in single playfield are extra half-brite:
 * plane 6 halves the colour the other five select, each gun shifted right by one (HRM, "Extra
 * Half Brite"). AGA first XORs the number with BPLCON4's BPLAM and drops half-brite with
 * BPLCON2's KILLEHB (AGA.guide, BPLCON4 and BPLCON2; WinUAE drawing.cpp, CMODE_EXTRAHB). */
static uint32_t denise_colour(const struct Denise *d, int idx)
{
	if(!AMIGA_A1200)
		return idx < 32 ? d->color[idx] : (d->color[idx & 31] >> 1) & 0x777777u;
	idx ^= d->bplcon4 >> 8;
	if((idx & 0x20) && denise_planes(d) == 6 && !(d->bplcon0 & 0x0C00) && !(d->bplcon2 & 0x0200))
		return (d->color[idx & 31] >> 1) & 0x7F7F7Fu;
	return d->color[idx & (AMIGA_COLORS - 1)];
}

static void denise_add_modulos(struct Denise *d)
{
	int p, bpu = denise_planes(d);
	for(p = 0; p < bpu; p++)
		d->bplpt[p] = (d->bplpt[p] + (uint32_t)(int32_t)((p & 1) ? d->bpl2mod : d->bpl1mod)) & 0x1FFFFE;
	d->mod_at = -1;
}

void denise_render_to(struct Amiga *m, int hpos)
{
	struct Denise *d = &m->denise;
	int to = hpos * 2 + 1, x;
	int hstart = denise_hstart(d), hstop = denise_hstop(d);
	/* BRDRBLNK blanks the border and BRDSPRT shows sprites in it, both only with ECSENA
	 * (AGA.guide, BPLCON3 and BPLCON0). */
	int ecsena = AMIGA_A1200 && (d->bplcon0 & 1);
	int border_sprites = ecsena && (d->bplcon3 & 0x0002);
	uint32_t border = ecsena && (d->bplcon3 & 0x0020) ? 0 : d->color[0];
	unsigned hide = m->pointer_hidden ? game_def.pointer_sprites : 0u;
	int inv;
	if(!d->fetched && hpos >= denise_ddf_start(d))
		denise_fetch(m);
	if(d->fetched == 1 && d->mod_at >= 0 && (hpos >= d->mod_at || hpos >= AMIGA_CCK_PER_LINE))
		denise_add_modulos(d);
	if(to > DENISE_X_MAX)
		to = DENISE_X_MAX;
	if(d->drawn_x < AMIGA_FRAME_DIW_X0)
		d->drawn_x = AMIGA_FRAME_DIW_X0;
	if(d->drawn_x >= to)
		return;
	inv = denise_in_vwindow(d);
	for(x = d->drawn_x; x < to; x++)
	{
		int inside = inv && x >= hstart && x < hstop;
		uint32_t c = border;
		if(inside || border_sprites)
		{
			int pair = 0, sp = m->sprites_enabled ? denise_sprite(d, x, hide, &pair) : 0;
			if(inside)
			{
				int idx = denise_playfield(d, x);
				/* Single playfield: PF2P is the playfield's priority code. HRM. */
				c = sp && (idx == 0 || pair < ((d->bplcon2 >> 3) & 7)) ? d->color[sp] : denise_colour(d, idx);
			}
			else if(sp)
				c = d->color[sp];
		}
		d->linebuf[x] = c;
	}
	d->drawn_x = to;
}

void denise_end_line(struct Amiga *m)
{
	struct Denise *d = &m->denise;
	int row = d->line - AMIGA_DISPLAY_LINE0, x;
	denise_render_to(m, AMIGA_CCK_PER_LINE);
	if(row < 0 || row >= AMIGA_FRAME_ROWS)
		return;
	for(x = 0; x < AMIGA_FRAME_STRIDE; x++)
		m->frame[row][x] = d->linebuf[AMIGA_FRAME_DIW_X0 + x];
}

/* A CPU or copper write to a wide sprite's data register repeats the word across the image, as
 * WinUAE does for 32-bit sprites (custom.cpp, wordto64convert); a 64-bit image's first word is
 * the chip bus's last value there, which is not modelled. */
static uint64_t denise_sprite_write(struct Amiga *m, const struct Denise *d, uint16_t v)
{
	int i, w = denise_sprite_words(d);
	uint64_t image = 0;
	if(w == 4)
		amiga_log_once(m, 0x94000000u, "sprite data write with 64-bit sprites: the word is repeated across the image");
	for(i = 0; i < w; i++)
		image = (image << 16) | v;
	return image;
}

static void denise_color_write(struct Denise *d, int n, uint16_t v)
{
	if(!AMIGA_A1200)
	{
		d->color[n] = denise_rgb(v);
		return;
	}
	/* BPLCON3's BANK picks the 32 registers; with LOCT the write sets the low nibbles, else both,
	 * the high copied to the low. RDRAM turns the writes into reads (AGA.guide, "Color Lookup
	 * Table", COLORxx, BPLCON2, BPLCON3). */
	if(d->bplcon2 & 0x0100)
		return;
	n += (d->bplcon3 >> 13) * 32;
	if(d->bplcon3 & 0x0200)
		d->color[n] = (d->color[n] & 0xF0F0F0u) | (denise_rgb(v) & 0x0F0F0Fu);
	else
		d->color[n] = denise_rgb(v);
}

void denise_write(struct Amiga *m, uint32_t reg, uint16_t v)
{
	struct Denise *d = &m->denise;
	if(reg >= 0x180 && reg <= 0x1BE)
	{
		denise_color_write(d, (int)((reg - 0x180) >> 1), v);
		return;
	}
	if(reg >= 0x0E0 && reg < 0x0E0 + 4 * AMIGA_PLANES)
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
		case 4: s->data = denise_sprite_write(m, d, v); s->armed = 1; break;
		case 6: s->datb = denise_sprite_write(m, d, v); break;
		}
		/* Agnus takes VSTART and VSTOP from every write of SPRxPOS and SPRxCTL, not only from
		 * the DMA's control words, so a sprite whose position the CPU or copper writes starts its
		 * data fetch at VSTART (FS-UAE 3.1.66 custom.cpp, SPRxCTLPOS and do_sprites_1). */
		if(!(reg & 4))
		{
			s->vstart = denise_sprite_vstart(s->pos, s->ctl);
			s->vstop = denise_sprite_vstop(s->ctl);
			if(s->dma_state == SpriteIdle || s->dma_state == SpriteWaitStart)
				s->dma_state = SpriteWaitStart;
		}
		return;
	}
	switch(reg)
	{
	case 0x08E: d->diwstrt = v; d->diwhigh_written = 0; return;
	case 0x090: d->diwstop = v; d->diwhigh_written = 0; return;
	case 0x092: d->ddfstrt = v; return;
	case 0x094: d->ddfstop = v; return;
	case 0x100: d->bplcon0 = v; return;
	case 0x102: d->bplcon1 = v; return;
	case 0x104: d->bplcon2 = v; return;
	case 0x108: d->bpl1mod = (int16_t)v; return;
	case 0x10A: d->bpl2mod = (int16_t)v; return;
#if AMIGA_A1200
	case 0x106:
		/* SPRES 2 and 3, hires and super-hires sprite pixels (AGA.guide, BPLCON3). */
		if((v & 0x00C0) >= 0x0080)
			amiga_log_once(m, 0x95000000u, "BPLCON3 0x%04X: hires and super-hires sprite pixels are drawn lores", v);
		d->bplcon3 = v;
		return;
	case 0x10C: d->bplcon4 = v; return;
	case 0x1E4: d->diwhigh = v; d->diwhigh_written = 1; return;
	case 0x1FC:
		/* SSCAN2 and BSCAN2, scan doubling (AGA.guide, FMODE). */
		if(v & 0xC000)
			amiga_log_once(m, 0x96000000u, "FMODE 0x%04X: scan doubling is not drawn", v);
		d->fmode = v;
		return;
#endif
	default:
		amiga_log_once(m, 0x90000000u | reg, "display register 0x%03X write ignored", (unsigned)reg);
		return;
	}
}
