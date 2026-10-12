#include "hle.h"

/* graphics.library's RastPort drawing, regions and text, by negative LVO from NDK 3.2
 * FD/graphics_lib.fd, each from its autodoc (graphics.doc) and RKM Libraries, "Graphics
 * Primitives" and "Graphics Library and Text". Pixels are computed in C straight into the
 * BitMap's planes, as BltBitMap is (hle_gfx.c). A RastPort with a Layer draws in the Layer's
 * coordinates, clipped to its bounds and to the clip region InstallClipRegion gave it (region
 * coordinates are relative to the Layer). Returns 0 to return to the caller, -1 when the LVO is
 * not implemented. */

#define D0 m->cpu.d[0]
#define D1 m->cpu.d[1]
#define D2 m->cpu.d[2]
#define D3 m->cpu.d[3]
#define A0 m->cpu.a[0]
#define A1 m->cpu.a[1]

#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4
#define FPF_PROPORTIONAL 0x20
#define TF_YSIZE 0x14
#define TF_FLAGS 0x17
#define TF_XSIZE 0x18
#define TF_BASELINE 0x1A
#define TF_LOCHAR 0x20
#define TF_HICHAR 0x21
#define TF_CHARDATA 0x22
#define TF_MODULO 0x26
#define TF_CHARLOC 0x28
#define TF_CHARSPACE 0x2C
#define TF_CHARKERN 0x30
#define RG_BOUNDS 0x00
#define RG_RECTS 0x08
#define RG_SIZE 0x0C
#define RR_NEXT 0x00
#define RR_PREV 0x04
#define RR_BOUNDS 0x08
#define RR_SIZE 0x10
#define DRAW_RECTS 64

struct DrawRect { int x0, y0, x1, y1; };

/* Where a RastPort draws: its BitMap, its origin in that BitMap, the bounds a pixel must lie in
 * (BitMap coordinates), and its clip region's rectangles (RastPort coordinates). */
struct Draw
{
	uint32_t bm;
	int bpr, rows, depth;
	int ox, oy;
	int x0, y0, x1, y1;
	int nrects;
	struct DrawRect rects[DRAW_RECTS];
	uint8_t mask;
};

static int draw_region_read(struct Amiga *m, uint32_t rg, struct DrawRect *out, int max);

static int draw_open(struct Amiga *m, uint32_t rp, struct Draw *d)
{
	uint32_t layer = hle_r32(m, rp + RP_LAYER), rg;
	d->bm = hle_r32(m, rp + RP_BITMAP);
	if(!d->bm)
	{
		amiga_fatal(m, "graphics: a RastPort with no BitMap");
		return -1;
	}
	d->bpr = hle_r16(m, d->bm + BM_BYTESPERROW);
	d->rows = hle_r16(m, d->bm + BM_ROWS);
	d->depth = hle_r8(m, d->bm + BM_DEPTH);
	if(d->depth > 8)
		d->depth = 8;
	d->mask = hle_r8(m, rp + RP_MASK);
	d->ox = d->oy = 0;
	d->x0 = d->y0 = 0;
	d->x1 = d->bpr * 8 - 1;
	d->y1 = d->rows - 1;
	d->nrects = -1;
	if(!layer)
		return 0;
	d->ox = (int16_t)hle_r16(m, layer + LR_MINX);
	d->oy = (int16_t)hle_r16(m, layer + LR_MINY);
	if(d->ox > d->x0)
		d->x0 = d->ox;
	if(d->oy > d->y0)
		d->y0 = d->oy;
	if((int16_t)hle_r16(m, layer + LR_MAXX) < d->x1)
		d->x1 = (int16_t)hle_r16(m, layer + LR_MAXX);
	if((int16_t)hle_r16(m, layer + LR_MAXY) < d->y1)
		d->y1 = (int16_t)hle_r16(m, layer + LR_MAXY);
	rg = hle_r32(m, layer + LR_CLIPREGION);
	if(rg)
		d->nrects = draw_region_read(m, rg, d->rects, DRAW_RECTS);
	return m->fatal ? -1 : 0;
}

/* Whether the RastPort pixel (x, y) may be drawn, and its BitMap position. */
static int draw_at(const struct Draw *d, int x, int y, int *bx, int *by)
{
	int i;
	*bx = x + d->ox;
	*by = y + d->oy;
	if(*bx < d->x0 || *bx > d->x1 || *by < d->y0 || *by > d->y1)
		return 0;
	if(d->nrects < 0)
		return 1;
	for(i = 0; i < d->nrects; i++)
		if(x >= d->rects[i].x0 && x <= d->rects[i].x1 && y >= d->rects[i].y0 && y <= d->rects[i].y1)
			return 1;
	return 0;
}

static uint32_t draw_byte(struct Amiga *m, const struct Draw *d, int p, int bx, int by)
{
	return hle_r32(m, d->bm + BM_PLANES + 4 * (uint32_t)p) + (uint32_t)(by * d->bpr + (bx >> 3));
}

static int draw_get(struct Amiga *m, const struct Draw *d, int bx, int by)
{
	int p, pen = 0;
	for(p = 0; p < d->depth; p++)
		if(hle_r8(m, draw_byte(m, d, p, bx, by)) & (0x80 >> (bx & 7)))
			pen |= 1 << p;
	return pen;
}

/* One pixel in pen `pen`, or complemented, through the RastPort's write mask. */
static void draw_set(struct Amiga *m, const struct Draw *d, int bx, int by, int pen, int complement)
{
	int p;
	for(p = 0; p < d->depth; p++)
	{
		uint32_t a;
		uint8_t b, bit = (uint8_t)(0x80 >> (bx & 7));
		if(!(d->mask & (1 << p)))
			continue;
		a = draw_byte(m, d, p, bx, by);
		b = hle_r8(m, a);
		if(complement)
			b ^= bit;
		else if(pen & (1 << p))
			b |= bit;
		else
			b &= (uint8_t)~bit;
		hle_w8(m, a, b);
	}
}

static void draw_pixel(struct Amiga *m, const struct Draw *d, int x, int y, int pen, int complement)
{
	int bx, by;
	if(draw_at(d, x, y, &bx, &by))
		draw_set(m, d, bx, by, pen, complement);
}

static int draw_mode_ok(struct Amiga *m, uint32_t rp, const char *what)
{
	uint8_t mode = hle_r8(m, rp + RP_DRAWMODE);
	if(mode & INVERSVID)
	{
		amiga_fatal(m, "graphics %s in draw mode 0x%02X (INVERSVID) is not implemented", what, mode);
		return 0;
	}
	return 1;
}

static void draw_rect(struct Amiga *m, uint32_t rp, int x0, int y0, int x1, int y1, int pen, int complement)
{
	struct Draw d;
	int x, y;
	if(draw_open(m, rp, &d) < 0)
		return;
	for(y = y0; y <= y1; y++)
		for(x = x0; x <= x1; x++)
			draw_pixel(m, &d, x, y, pen, complement);
}

/* --- regions ------------------------------------------------------------------------------ */

/* A Region's rectangles, absolute: each RegionRectangle is relative to the Region's bounds. */
static int draw_region_read(struct Amiga *m, uint32_t rg, struct DrawRect *out, int max)
{
	int bx = (int16_t)hle_r16(m, rg + RG_BOUNDS), by = (int16_t)hle_r16(m, rg + RG_BOUNDS + 2), n = 0;
	uint32_t r;
	for(r = hle_r32(m, rg + RG_RECTS); r; r = hle_r32(m, r + RR_NEXT))
	{
		if(n == max)
		{
			amiga_fatal(m, "graphics: a Region of more than %d rectangles", max);
			return n;
		}
		out[n].x0 = bx + (int16_t)hle_r16(m, r + RR_BOUNDS);
		out[n].y0 = by + (int16_t)hle_r16(m, r + RR_BOUNDS + 2);
		out[n].x1 = bx + (int16_t)hle_r16(m, r + RR_BOUNDS + 4);
		out[n].y1 = by + (int16_t)hle_r16(m, r + RR_BOUNDS + 6);
		n++;
	}
	return n;
}

static void draw_region_free_rects(struct Amiga *m, uint32_t rg)
{
	uint32_t r = hle_r32(m, rg + RG_RECTS);
	while(r)
	{
		uint32_t next = hle_r32(m, r + RR_NEXT);
		hle_free(m, r, RR_SIZE);
		r = next;
	}
	hle_w32(m, rg + RG_RECTS, 0);
}

/* Replace the Region's rectangles with `rects`, its bounds with their union. */
static void draw_region_write(struct Amiga *m, uint32_t rg, const struct DrawRect *rects, int n)
{
	int i, bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
	uint32_t prev = 0;
	draw_region_free_rects(m, rg);
	for(i = 0; i < n; i++)
	{
		if(!i || rects[i].x0 < bx0) bx0 = rects[i].x0;
		if(!i || rects[i].y0 < by0) by0 = rects[i].y0;
		if(!i || rects[i].x1 > bx1) bx1 = rects[i].x1;
		if(!i || rects[i].y1 > by1) by1 = rects[i].y1;
	}
	hle_w16(m, rg + RG_BOUNDS, (uint16_t)bx0);
	hle_w16(m, rg + RG_BOUNDS + 2, (uint16_t)by0);
	hle_w16(m, rg + RG_BOUNDS + 4, (uint16_t)bx1);
	hle_w16(m, rg + RG_BOUNDS + 6, (uint16_t)by1);
	for(i = 0; i < n; i++)
	{
		uint32_t r = hle_alloc(m, RR_SIZE, MEMF_PUBLIC | MEMF_CLEAR, 0);
		if(!r)
		{
			amiga_fatal(m, "graphics: no memory for a RegionRectangle");
			return;
		}
		hle_w16(m, r + RR_BOUNDS, (uint16_t)(rects[i].x0 - bx0));
		hle_w16(m, r + RR_BOUNDS + 2, (uint16_t)(rects[i].y0 - by0));
		hle_w16(m, r + RR_BOUNDS + 4, (uint16_t)(rects[i].x1 - bx0));
		hle_w16(m, r + RR_BOUNDS + 6, (uint16_t)(rects[i].y1 - by0));
		hle_w32(m, r + RR_PREV, prev);
		if(prev)
			hle_w32(m, prev + RR_NEXT, r);
		else
			hle_w32(m, rg + RG_RECTS, r);
		prev = r;
	}
}

/* a minus b: up to four disjoint rectangles appended to out. */
static int draw_subtract(struct DrawRect a, struct DrawRect b, struct DrawRect *out)
{
	int n = 0;
	if(b.x1 < a.x0 || b.x0 > a.x1 || b.y1 < a.y0 || b.y0 > a.y1)
	{
		out[n++] = a;
		return n;
	}
	if(b.y0 > a.y0)
	{
		out[n] = a;
		out[n++].y1 = b.y0 - 1;
		a.y0 = b.y0;
	}
	if(b.y1 < a.y1)
	{
		out[n] = a;
		out[n++].y0 = b.y1 + 1;
		a.y1 = b.y1;
	}
	if(b.x0 > a.x0)
	{
		out[n] = a;
		out[n++].x1 = b.x0 - 1;
	}
	if(b.x1 < a.x1)
	{
		out[n] = a;
		out[n++].x0 = b.x1 + 1;
	}
	return n;
}

/* OrRectRegion, AndRectRegion, ClearRectRegion: the rectangles stay disjoint. */
static void draw_region_op(struct Amiga *m, uint32_t rg, uint32_t rect, int op)
{
	struct DrawRect old[DRAW_RECTS], out[DRAW_RECTS * 4 + 1], r;
	int n = draw_region_read(m, rg, old, DRAW_RECTS), k = 0, i;
	r.x0 = (int16_t)hle_r16(m, rect);
	r.y0 = (int16_t)hle_r16(m, rect + 2);
	r.x1 = (int16_t)hle_r16(m, rect + 4);
	r.y1 = (int16_t)hle_r16(m, rect + 6);
	for(i = 0; i < n; i++)
	{
		if(op == 'a')
		{
			struct DrawRect c = old[i];
			if(r.x0 > c.x0) c.x0 = r.x0;
			if(r.y0 > c.y0) c.y0 = r.y0;
			if(r.x1 < c.x1) c.x1 = r.x1;
			if(r.y1 < c.y1) c.y1 = r.y1;
			if(c.x0 <= c.x1 && c.y0 <= c.y1)
				out[k++] = c;
		}
		else
			k += draw_subtract(old[i], r, out + k);
	}
	if(op == 'o' && r.x0 <= r.x1 && r.y0 <= r.y1)
		out[k++] = r;
	if(k > DRAW_RECTS)
	{
		amiga_fatal(m, "graphics: a Region of more than %d rectangles", DRAW_RECTS);
		return;
	}
	draw_region_write(m, rg, out, k);
}

/* --- text --------------------------------------------------------------------------------- */

/* The glyph index of character c, and its kerning, width in the strike and advance. */
static void draw_glyph(struct Amiga *m, uint32_t tf, uint8_t c, uint32_t *loc, int *kern, int *advance)
{
	uint8_t lo = hle_r8(m, tf + TF_LOCHAR), hi = hle_r8(m, tf + TF_HICHAR);
	uint32_t i = c >= lo && c <= hi ? (uint32_t)(c - lo) : (uint32_t)(hi - lo + 1);
	uint32_t space = hle_r32(m, tf + TF_CHARSPACE), kerning = hle_r32(m, tf + TF_CHARKERN);
	*loc = hle_r32(m, hle_r32(m, tf + TF_CHARLOC) + 4 * i);
	*kern = kerning ? (int16_t)hle_r16(m, kerning + 2 * i) : 0;
	*advance = (hle_r8(m, tf + TF_FLAGS) & FPF_PROPORTIONAL) && space ? (int16_t)hle_r16(m, space + 2 * i) : (int16_t)hle_r16(m, tf + TF_XSIZE);
}

static int draw_text_length(struct Amiga *m, uint32_t rp, uint32_t s, int n)
{
	uint32_t tf = hle_r32(m, rp + RP_FONT), loc;
	int i, len = 0, kern, adv;
	for(i = 0; i < n; i++)
	{
		draw_glyph(m, tf, hle_r8(m, s + (uint32_t)i), &loc, &kern, &adv);
		len += kern + adv + (int16_t)hle_r16(m, rp + RP_TXSPACING);
	}
	return len;
}

/* Text: with JAM2 the text's extent is first filled with BgPen; each glyph's set bits take
 * FgPen (or are complemented). */
static void draw_text(struct Amiga *m, uint32_t rp, uint32_t s, int n)
{
	uint32_t tf = hle_r32(m, rp + RP_FONT), data, loc;
	uint8_t mode = hle_r8(m, rp + RP_DRAWMODE);
	int fg = hle_r8(m, rp + RP_FGPEN), bg = hle_r8(m, rp + RP_BGPEN);
	int x = (int16_t)hle_r16(m, rp + RP_CP_X), top = (int16_t)hle_r16(m, rp + RP_CP_Y) - hle_r16(m, tf + TF_BASELINE);
	int ysize = hle_r16(m, tf + TF_YSIZE), modulo = (int16_t)hle_r16(m, tf + TF_MODULO), i, kern, adv;
	struct Draw d;
	if(!tf)
	{
		amiga_fatal(m, "graphics Text with no font: Kickstart's ROM font (topaz) is not in this runtime");
		return;
	}
	if(hle_r8(m, rp + RP_ALGOSTYLE))
	{
		amiga_fatal(m, "graphics Text with algorithmic style 0x%02X is not implemented", hle_r8(m, rp + RP_ALGOSTYLE));
		return;
	}
	if(mode & INVERSVID)
	{
		amiga_fatal(m, "graphics Text in draw mode 0x%02X (INVERSVID) is not implemented", mode);
		return;
	}
	if(draw_open(m, rp, &d) < 0)
		return;
	if((mode & 3) == JAM2)
	{
		int w = draw_text_length(m, rp, s, n), xx, yy;
		for(yy = 0; yy < ysize; yy++)
			for(xx = 0; xx < w; xx++)
				draw_pixel(m, &d, x + xx, top + yy, bg, 0);
	}
	data = hle_r32(m, tf + TF_CHARDATA);
	for(i = 0; i < n; i++)
	{
		int off, w, yy, xx;
		draw_glyph(m, tf, hle_r8(m, s + (uint32_t)i), &loc, &kern, &adv);
		x += kern;
		off = (int)(loc >> 16);
		w = (int)(loc & 0xFFFF);
		for(yy = 0; yy < ysize; yy++)
		{
			uint32_t row = data + (uint32_t)(yy * modulo);
			for(xx = 0; xx < w; xx++)
			{
				int bit = off + xx;
				if(hle_r8(m, row + (uint32_t)(bit >> 3)) & (0x80 >> (bit & 7)))
					draw_pixel(m, &d, x + xx, top + yy, fg, (mode & 3) == COMPLEMENT);
			}
		}
		x += adv + (int16_t)hle_r16(m, rp + RP_TXSPACING);
	}
	hle_w16(m, rp + RP_CP_X, (uint16_t)x);
}

/* --- lines and blits ---------------------------------------------------------------------- */

/* Draw: a line from the current position to (x, y), both ends included, one pixel per step
 * along the major axis (Bresenham). */
static void draw_line(struct Amiga *m, uint32_t rp, int x1, int y1)
{
	struct Draw d;
	int x0 = (int16_t)hle_r16(m, rp + RP_CP_X), y0 = (int16_t)hle_r16(m, rp + RP_CP_Y);
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1, err = dx - dy;
	int pen = hle_r8(m, rp + RP_FGPEN), comp = (hle_r8(m, rp + RP_DRAWMODE) & 3) == COMPLEMENT;
	if(hle_r16(m, rp + RP_LINEPTRN) != 0xFFFF)
	{
		amiga_fatal(m, "graphics Draw with a line pattern (0x%04X) is not implemented", hle_r16(m, rp + RP_LINEPTRN));
		return;
	}
	if(!draw_mode_ok(m, rp, "Draw") || draw_open(m, rp, &d) < 0)
		return;
	for(;;)
	{
		int e2 = 2 * err;
		draw_pixel(m, &d, x0, y0, pen, comp);
		if(x0 == x1 && y0 == y1)
			break;
		if(e2 > -dy)
		{
			err -= dy;
			x0 += sx;
		}
		if(e2 < dx)
		{
			err += dx;
			y0 += sy;
		}
	}
	hle_w16(m, rp + RP_CP_X, (uint16_t)x1);
	hle_w16(m, rp + RP_CP_Y, (uint16_t)y1);
}

/* BltBitMapRastPort: the minterm's upper nibble with B = source and C = destination, as
 * BltBitMap (hle_gfx.c), into the RastPort's visible pixels and the planes of its mask. */
static void draw_blit_rastport(struct Amiga *m, uint32_t sbm, int sx, int sy, uint32_t rp, int dx, int dy, int w, int h, uint8_t lf)
{
	struct Draw d;
	int sbpr = hle_r16(m, sbm + BM_BYTESPERROW), sdepth = hle_r8(m, sbm + BM_DEPTH), x, y, p;
	if(draw_open(m, rp, &d) < 0)
		return;
	for(y = 0; y < h; y++)
		for(x = 0; x < w; x++)
		{
			int bx, by;
			if(!draw_at(&d, dx + x, dy + y, &bx, &by))
				continue;
			for(p = 0; p < d.depth; p++)
			{
				uint32_t a;
				uint8_t bit = (uint8_t)(0x80 >> (bx & 7)), v, sbit;
				int b = 0, c, r = 0;
				if(!(d.mask & (1 << p)))
					continue;
				if(p < sdepth)
				{
					int px = sx + x;
					uint32_t sa = hle_r32(m, sbm + BM_PLANES + 4 * (uint32_t)p) + (uint32_t)((sy + y) * sbpr + (px >> 3));
					sbit = (uint8_t)(0x80 >> (px & 7));
					b = (hle_r8(m, sa) & sbit) != 0;
				}
				a = draw_byte(m, &d, p, bx, by);
				v = hle_r8(m, a);
				c = (v & bit) != 0;
				if((lf & 0x80) && b && c) r = 1;
				if((lf & 0x40) && b && !c) r = 1;
				if((lf & 0x20) && !b && c) r = 1;
				if((lf & 0x10) && !b && !c) r = 1;
				hle_w8(m, a, (uint8_t)(r ? v | bit : v & ~bit));
			}
		}
}

int hle_draw_call(struct Amiga *m, uint32_t lvo)
{
	switch(lvo)
	{
	case 54: /* TextLength(a1 rp, a0 string, d0 count): the low word only before V36 */
		D0 = (uint16_t)draw_text_length(m, A1, A0, (int16_t)D0);
		return 0;
	case 60: /* Text(a1 rp, a0 string, d0 count) */
		draw_text(m, A1, A0, (int16_t)D0);
		return 0;
	case 66: /* SetFont(a1 rp, a0 font): the font and its text attributes; no soft style */
		hle_w32(m, A1 + RP_FONT, A0);
		if(A0)
		{
			hle_w16(m, A1 + RP_TXHEIGHT, hle_r16(m, A0 + TF_YSIZE));
			hle_w16(m, A1 + RP_TXWIDTH, hle_r16(m, A0 + TF_XSIZE));
			hle_w16(m, A1 + RP_TXBASELINE, hle_r16(m, A0 + TF_BASELINE));
		}
		hle_w8(m, A1 + RP_ALGOSTYLE, 0);
		return 0;
	case 234: /* SetRast(a1 rp, d0 pen): every pixel the RastPort can draw */
	{
		struct Draw d;
		int x, y;
		if(draw_open(m, A1, &d) < 0)
			return 0;
		d.mask = 0xFF;
		for(y = d.y0; y <= d.y1; y++)
			for(x = d.x0; x <= d.x1; x++)
				draw_set(m, &d, x, y, (int)(D0 & 0xFF), 0);
		return 0;
	}
	case 240: /* Move(a1 rp, d0 x, d1 y) */
		hle_w16(m, A1 + RP_CP_X, (uint16_t)D0);
		hle_w16(m, A1 + RP_CP_Y, (uint16_t)D1);
		return 0;
	case 246: /* Draw(a1 rp, d0 x, d1 y) */
		draw_line(m, A1, (int16_t)D0, (int16_t)D1);
		return 0;
	case 300: /* BltClear(a1 memBlock, d0 bytecount, d1 flags) */
	{
		uint32_t n = (D1 & 2) ? (D0 >> 16) * (D0 & 0xFFFF) : D0, i;
		if(D1 & 4)
		{
			amiga_fatal(m, "BltClear with a fill value (V36) is not implemented");
			return 0;
		}
		for(i = 0; i < n; i++)
			hle_w8(m, A1 + i, 0);
		return 0;
	}
	case 306: /* RectFill(a1 rp, d0 xmin, d1 ymin, d2 xmax, d3 ymax) */
		if(hle_r32(m, A1 + 0x08))
		{
			amiga_fatal(m, "graphics RectFill with an area pattern is not implemented");
			return 0;
		}
		if(!draw_mode_ok(m, A1, "RectFill"))
			return 0;
		draw_rect(m, A1, (int16_t)D0, (int16_t)D1, (int16_t)D2, (int16_t)D3, hle_r8(m, A1 + RP_FGPEN),
			(hle_r8(m, A1 + RP_DRAWMODE) & 3) == COMPLEMENT);
		return 0;
	case 318: /* ReadPixel(a1 rp, d0 x, d1 y): the pen, or -1 */
	{
		struct Draw d;
		int bx, by;
		if(draw_open(m, A1, &d) < 0)
			return 0;
		D0 = draw_at(&d, (int16_t)D0, (int16_t)D1, &bx, &by) ? (uint32_t)draw_get(m, &d, bx, by) : 0xFFFFFFFFu;
		return 0;
	}
	case 324: /* WritePixel(a1 rp, d0 x, d1 y): 0, or -1 outside the RastPort */
	{
		struct Draw d;
		int bx, by;
		if(!draw_mode_ok(m, A1, "WritePixel") || draw_open(m, A1, &d) < 0)
			return 0;
		if(!draw_at(&d, (int16_t)D0, (int16_t)D1, &bx, &by))
		{
			D0 = 0xFFFFFFFFu;
			return 0;
		}
		draw_set(m, &d, bx, by, hle_r8(m, A1 + RP_FGPEN), (hle_r8(m, A1 + RP_DRAWMODE) & 3) == COMPLEMENT);
		D0 = 0;
		return 0;
	}
	case 342: /* SetAPen(a1 rp, d0 pen) */
		hle_w8(m, A1 + RP_FGPEN, (uint8_t)D0);
		return 0;
	case 348: /* SetBPen(a1 rp, d0 pen) */
		hle_w8(m, A1 + RP_BGPEN, (uint8_t)D0);
		return 0;
	case 354: /* SetDrMd(a1 rp, d0 mode) */
		hle_w8(m, A1 + RP_DRAWMODE, (uint8_t)D0);
		return 0;
	case 366: /* CBump(a1 ucl): the next instruction of the current CopList block */
	{
		uint32_t cl = hle_r32(m, A1 + UCL_COPLIST);
		uint16_t count = (uint16_t)(hle_r16(m, cl + CL_COUNT) + 1);
		if(count >= hle_r16(m, cl + CL_MAXCOUNT))
		{
			amiga_fatal(m, "CBump past a CopList block's MaxCount (%u) is not implemented", hle_r16(m, cl + CL_MAXCOUNT));
			return 0;
		}
		hle_w16(m, cl + CL_COUNT, count);
		hle_w32(m, cl + CL_COPPTR, hle_r32(m, cl + CL_COPPTR) + 6);
		return 0;
	}
	case 372: /* CMove(a1 ucl, d0 register, d1 value): a CopIns MOVE (graphics/copper.i) */
	case 378: /* CWait(a1 ucl, d0 v, d1 h): a CopIns WAIT */
	{
		uint32_t ins = hle_r32(m, hle_r32(m, A1 + UCL_COPLIST) + CL_COPPTR);
		hle_w16(m, ins, lvo == 372 ? 0 : 1);
		hle_w16(m, ins + 2, (uint16_t)D0);
		hle_w16(m, ins + 4, (uint16_t)D1);
		return 0;
	}
	case 510: /* OrRectRegion(a0 region, a1 rectangle): TRUE */
		draw_region_op(m, A0, A1, 'o');
		D0 = 1;
		return 0;
	case 504: /* AndRectRegion(a0 region, a1 rectangle) */
		draw_region_op(m, A0, A1, 'a');
		return 0;
	case 516: /* NewRegion: an empty Region */
		D0 = hle_alloc(m, RG_SIZE, MEMF_PUBLIC | MEMF_CLEAR, 0);
		return 0;
	case 522: /* ClearRectRegion(a0 region, a1 rectangle): TRUE */
		draw_region_op(m, A0, A1, 'c');
		D0 = 1;
		return 0;
	case 528: /* ClearRegion(a0 region) */
		draw_region_write(m, A0, 0, 0);
		return 0;
	case 534: /* DisposeRegion(a0 region) */
		draw_region_free_rects(m, A0);
		hle_free(m, A0, RG_SIZE);
		return 0;
	case 606: /* BltBitMapRastPort(a0 src, d0 srcX, d1 srcY, a1 destRP, d2 destX, d3 destY, d4 sizeX,
	           * d5 sizeY, d6 minterm): TRUE */
		draw_blit_rastport(m, A0, (int16_t)D0, (int16_t)D1, A1, (int16_t)D2, (int16_t)D3, (int16_t)m->cpu.d[4],
			(int16_t)m->cpu.d[5], (uint8_t)m->cpu.d[6]);
		D0 = 1;
		return 0;
	default:
		return -1;
	}
}
