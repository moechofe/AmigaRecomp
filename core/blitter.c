#include "amiga.h"

/* The blitter, HRM chapter 6: area mode with shifts, masks, modulos, descending mode and
 * fill, and line mode. A blit completes the instant BLTSIZE is written. The A and B shifters'
 * previous words carry from row to row but start each blit at 0, as WinUAE's blitter.cpp
 * (blitter_start_init) has them; a left-over word would shift into the first row's first word. */

static uint16_t blit_minterm(uint8_t lf, uint16_t a, uint16_t b, uint16_t c)
{
	uint16_t d = 0;
	if(lf & 0x01) d |= (uint16_t)(~a & ~b & ~c);
	if(lf & 0x02) d |= (uint16_t)(~a & ~b & c);
	if(lf & 0x04) d |= (uint16_t)(~a & b & ~c);
	if(lf & 0x08) d |= (uint16_t)(~a & b & c);
	if(lf & 0x10) d |= (uint16_t)(a & ~b & ~c);
	if(lf & 0x20) d |= (uint16_t)(a & ~b & c);
	if(lf & 0x40) d |= (uint16_t)(a & b & ~c);
	if(lf & 0x80) d |= (uint16_t)(a & b & c);
	return d;
}

/* Fill one word right to left, carrying the fill state across words of a row. */
static uint16_t blit_fill(uint16_t d, int exclusive, int *carry)
{
	uint16_t out = 0;
	int i;
	for(i = 0; i < 16; i++)
	{
		int bit = (d >> i) & 1;
		*carry ^= bit;
		if(exclusive ? *carry : (*carry | bit))
			out |= (uint16_t)(1u << i);
	}
	return out;
}

static void blit_area(struct Amiga *m, int width, int height)
{
	struct Blitter *b = &m->blit;
	int use_a = (b->con0 >> 11) & 1, use_b = (b->con0 >> 10) & 1;
	int use_c = (b->con0 >> 9) & 1, use_d = (b->con0 >> 8) & 1;
	int ash = b->con0 >> 12, bsh = b->con1 >> 12;
	int desc = (b->con1 >> 1) & 1;
	int fci = (b->con1 >> 2) & 1, ife = (b->con1 >> 3) & 1, efe = (b->con1 >> 4) & 1;
	int32_t step = desc ? -2 : 2;
	uint8_t lf = (uint8_t)b->con0;
	int x, y;
	b->zero = 1;
	for(y = 0; y < height; y++)
	{
		int carry = fci;
		for(x = 0; x < width; x++)
		{
			uint16_t a, bb, c, d;
			uint32_t ah, bh;
			if(use_a)
			{
				b->adat = mem_chip16(m, b->apt);
				b->apt = (b->apt + (uint32_t)step) & 0x1FFFFE;
			}
			if(use_b)
			{
				b->bdat = mem_chip16(m, b->bpt);
				b->bpt = (b->bpt + (uint32_t)step) & 0x1FFFFE;
			}
			if(use_c)
			{
				b->cdat = mem_chip16(m, b->cpt);
				b->cpt = (b->cpt + (uint32_t)step) & 0x1FFFFE;
			}
			a = b->adat;
			if(x == 0)
				a &= b->afwm;
			if(x == width - 1)
				a &= b->alwm;
			if(desc)
			{
				ah = (((uint32_t)a << 16) | b->aold) >> (16 - ash);
				bh = (((uint32_t)b->bdat << 16) | b->bold) >> (16 - bsh);
			}
			else
			{
				ah = (((uint32_t)b->aold << 16) | a) >> ash;
				bh = (((uint32_t)b->bold << 16) | b->bdat) >> bsh;
			}
			b->aold = a;
			b->bold = b->bdat;
			bb = (uint16_t)bh;
			c = b->cdat;
			d = blit_minterm(lf, (uint16_t)ah, bb, c);
			if(desc && (ife || efe))
				d = blit_fill(d, efe, &carry);
			if(d)
				b->zero = 0;
			if(use_d)
			{
				mem_chip_write16(m, b->dpt, d);
				b->dpt = (b->dpt + (uint32_t)step) & 0x1FFFFE;
			}
		}
		if(use_a)
			b->apt = (b->apt + (uint32_t)(int32_t)(desc ? -b->amod : b->amod)) & 0x1FFFFE;
		if(use_b)
			b->bpt = (b->bpt + (uint32_t)(int32_t)(desc ? -b->bmod : b->bmod)) & 0x1FFFFE;
		if(use_c)
			b->cpt = (b->cpt + (uint32_t)(int32_t)(desc ? -b->cmod : b->cmod)) & 0x1FFFFE;
		if(use_d)
			b->dpt = (b->dpt + (uint32_t)(int32_t)(desc ? -b->dmod : b->dmod)) & 0x1FFFFE;
	}
}

/* Line mode. BLTAPT carries the error term, ASH the pixel within the word, BLTCON1 the
 * octant (SUD, SUL, AUL), SING one-dot-per-row and SIGN the initial error sign. The first dot
 * goes to BLTDPT; every later one to the word its C was read from, as FS-UAE's blitter.cpp
 * (actually_do_blit) moves D to C after each step. */
static void blit_line(struct Amiga *m, int height)
{
	struct Blitter *b = &m->blit;
	int shift = b->con0 >> 12;
	int bsh = b->con1 >> 12;
	int sud = (b->con1 >> 4) & 1, sul = (b->con1 >> 3) & 1, aul = (b->con1 >> 2) & 1;
	int sing = (b->con1 >> 1) & 1;
	int sign = (b->con1 >> 6) & 1;
	uint16_t tex = (uint16_t)((b->bdat >> bsh) | (b->bdat << (16 - bsh)));
	uint8_t lf = (uint8_t)b->con0;
	int onedot = 0, i;
	b->zero = 1;
	for(i = 0; i < height; i++)
	{
		uint16_t c = mem_chip16(m, b->cpt);
		uint16_t a = (uint16_t)(b->adat >> shift);
		uint16_t bb = (tex & 1) ? 0xFFFF : 0;
		uint16_t d = blit_minterm(lf, a, bb, c);
		if(sing && onedot)
			d = c;
		onedot = 1;
		if(d)
			b->zero = 0;
		mem_chip_write16(m, b->dpt, d);

		if(!sign)
		{
			b->apt = (b->apt + (uint32_t)(int32_t)b->amod) & 0xFFFFFFFFu;
			if(sud)
			{
				b->cpt = (b->cpt + (uint32_t)(int32_t)(sul ? -b->cmod : b->cmod)) & 0x1FFFFE;
				onedot = 0;
			}
			else if(sul)
			{
				if(shift-- == 0) { shift = 15; b->cpt = (b->cpt - 2) & 0x1FFFFE; }
			}
			else
			{
				if(++shift == 16) { shift = 0; b->cpt = (b->cpt + 2) & 0x1FFFFE; }
			}
		}
		else
		{
			b->apt = (b->apt + (uint32_t)(int32_t)b->bmod) & 0xFFFFFFFFu;
		}
		if(sud)
		{
			if(aul)
			{
				if(shift-- == 0) { shift = 15; b->cpt = (b->cpt - 2) & 0x1FFFFE; }
			}
			else
			{
				if(++shift == 16) { shift = 0; b->cpt = (b->cpt + 2) & 0x1FFFFE; }
			}
		}
		else
		{
			b->cpt = (b->cpt + (uint32_t)(int32_t)(aul ? -b->cmod : b->cmod)) & 0x1FFFFE;
			onedot = 0;
		}
		sign = (int16_t)b->apt < 0;
		b->dpt = b->cpt;
		tex = (uint16_t)((tex << 1) | (tex >> 15));
	}
	b->con0 = (uint16_t)((b->con0 & 0x0FFF) | (shift << 12));
	b->con1 = (uint16_t)((b->con1 & ~0x40) | (sign << 6));
}

void blit_start(struct Amiga *m, uint16_t bltsize)
{
	int width = bltsize & 0x3F, height = (bltsize >> 6) & 0x3FF;
	if(!width)
		width = 64;
	if(!height)
		height = 1024;
	if(!(m->paula.dmacon & 0x0040) || !(m->paula.dmacon & 0x0200))
		amiga_log_once(m, 0xA0000000u, "blit started with blitter DMA off; run anyway");
	m->blit.aold = 0;
	m->blit.bold = 0;
	if(m->blit.con1 & 1)
		blit_line(m, height);
	else
		blit_area(m, width, height);
	custom_set_intreq(m, 0x0040);
}
