#include "hle.h"

/* graphics.library, by negative LVO from NDK 3.2 FD/graphics_lib.fd. The View / ViewPort calls
 * build the same copper list graphics 34.1 built: graphics.library 34 / Kickstart 1.3
 * behaviour observed under WinUAE, docs/runtime.md, "HLE". Only what the supported games' views
 * use is implemented; anything else stops the runtime. */

#define A0 m->cpu.a[0]
#define A1 m->cpu.a[1]
#define D0 m->cpu.d[0]
#define D1 m->cpu.d[1]
#define D2 m->cpu.d[2]

#define GFX_V_SPRITES 0x4000
#define GFX_MAX_INS 128

static uint32_t gfx_ins[GFX_MAX_INS];

static int gfx_move(int n, uint16_t reg, uint16_t v)
{
	if(n < GFX_MAX_INS)
		gfx_ins[n] = ((uint32_t)reg << 16) | v;
	return n + 1;
}

static int gfx_wait(int n, int line, int hpos)
{
	if(n < GFX_MAX_INS)
		gfx_ins[n] = ((uint32_t)(((line & 0xFF) << 8) | (hpos & 0xFE) | 1) << 16) | 0xFFFE;
	return n + 1;
}

/* One non-interlaced lores ViewPort, in the order graphics 34.1 emitted it. The
 * vertical display-window bytes 0x05 and 0x40 are as observed (docs/runtime.md, "HLE"). */
static int gfx_viewport(struct Amiga *m, uint32_t view, uint32_t vp)
{
	uint32_t cm = hle_r32(m, vp + VP_COLORMAP), ri = hle_r32(m, vp + VP_RASINFO);
	uint32_t bm = hle_r32(m, ri + RI_BITMAP), table;
	uint16_t modes = hle_r16(m, vp + VP_MODES);
	int y0 = (int16_t)hle_r16(m, view + V_DYOFFSET) + (int16_t)hle_r16(m, vp + VP_DYOFFSET);
	int x0 = (int16_t)hle_r16(m, view + V_DXOFFSET) + (int16_t)hle_r16(m, vp + VP_DXOFFSET);
	int width = (int16_t)hle_r16(m, vp + VP_DWIDTH), height = (int16_t)hle_r16(m, vp + VP_DHEIGHT);
	int depth = hle_r8(m, bm + BM_DEPTH), bpr = hle_r16(m, bm + BM_BYTESPERROW);
	int rx = (int16_t)hle_r16(m, ri + RI_RXOFFSET), ry = (int16_t)hle_r16(m, ri + RI_RYOFFSET);
	int ddfstrt = ((x0 - 17) / 2) & 0xF8, n = 0, i, count, y1;
	if(modes & ~GFX_V_SPRITES)
	{
		amiga_fatal(m, "MakeVPort: ViewPort modes 0x%04X are not implemented", modes);
		return 0;
	}
	if(rx & 15)
	{
		amiga_fatal(m, "MakeVPort: a horizontal RxOffset of %d is not implemented", rx);
		return 0;
	}
	n = gfx_wait(n, y0 - 2, 0);
	count = cm ? hle_r16(m, cm + CM_COUNT) : 0;
	table = cm ? hle_r32(m, cm + CM_COLORTABLE) : 0;
	for(i = 0; i < count && i < 32; i++)
		n = gfx_move(n, (uint16_t)(0x180 + 2 * i), hle_r16(m, table + 2 * (uint32_t)i));
	n = gfx_move(n, 0x08E, (uint16_t)(0x0500 | (x0 & 0xFF)));
	n = gfx_move(n, 0x100, 0x0200);
	n = gfx_move(n, 0x104, hle_r8(m, vp + VP_SPRITEPRI));
	n = gfx_move(n, 0x090, (uint16_t)(0x4000 | ((x0 + width) & 0xFF)));
	n = gfx_move(n, 0x092, (uint16_t)ddfstrt);
	n = gfx_move(n, 0x094, (uint16_t)(ddfstrt + (width / 16 - 1) * 8));
	n = gfx_move(n, 0x102, 0);
	n = gfx_move(n, 0x108, (uint16_t)(bpr - width / 8));
	n = gfx_move(n, 0x10A, (uint16_t)(bpr - width / 8));
	for(i = 0; i < depth && i < 6; i++)
	{
		uint32_t pt = hle_r32(m, bm + BM_PLANES + 4 * (uint32_t)i) + (uint32_t)(ry * bpr + rx / 8);
		n = gfx_move(n, (uint16_t)(0x0E0 + 4 * i), (uint16_t)(pt >> 16));
		n = gfx_move(n, (uint16_t)(0x0E2 + 4 * i), (uint16_t)pt);
	}
	n = gfx_wait(n, y0, 0);
	n = gfx_move(n, 0x100, (uint16_t)((depth << 12) | 0x0200));
	y1 = y0 + height;
	if(y1 >= 256)
		n = gfx_wait(n, 255, 0xDE);
	n = gfx_wait(n, y1, 0);
	n = gfx_move(n, 0x100, 0x0200);
	if(n > GFX_MAX_INS)
		amiga_fatal(m, "MakeVPort: more than %d copper instructions", GFX_MAX_INS);
	return n;
}

/* vp->DspIns holds this runtime's own CopList: CopLStart points at raw copper words, Count
 * says how many. */
static void gfx_make_vport(struct Amiga *m, uint32_t view, uint32_t vp)
{
	uint32_t cl = hle_r32(m, vp + VP_DSPINS), buf;
	int n = gfx_viewport(m, view, vp), i;
	if(m->fatal)
		return;
	if(!cl)
	{
		cl = hle_alloc(m, CL_SIZE, MEMF_PUBLIC | MEMF_CLEAR, 0);
		buf = hle_alloc(m, GFX_MAX_INS * 4, MEMF_PUBLIC, 0);
		if(!cl || !buf)
		{
			amiga_fatal(m, "MakeVPort: out of memory");
			return;
		}
		hle_w32(m, cl + CL_LSTART, buf);
		hle_w32(m, vp + VP_DSPINS, cl);
	}
	buf = hle_r32(m, cl + CL_LSTART);
	for(i = 0; i < n; i++)
		hle_w32(m, buf + 4 * (uint32_t)i, gfx_ins[i]);
	hle_w16(m, cl + CL_COUNT, (uint16_t)n);
}

/* The View's long-frame list: every ViewPort's instructions and the end-of-list WAIT, in a chip
 * buffer of MaxCount words where MaxCount is the instruction count plus 3, as observed (docs/runtime.md, "HLE"). */
static void gfx_mrgcop(struct Amiga *m, uint32_t view)
{
	uint32_t vp, cpr = hle_r32(m, view + V_LOFCPRLIST), start, total = 1, at = 0;
	for(vp = hle_r32(m, view + V_VIEWPORT); vp; vp = hle_r32(m, vp + VP_NEXT))
		if(hle_r32(m, vp + VP_DSPINS))
			total += hle_r16(m, hle_r32(m, vp + VP_DSPINS) + CL_COUNT);
	if(!cpr)
	{
		cpr = hle_alloc(m, 12, MEMF_PUBLIC | MEMF_CLEAR, 0);
		if(!cpr)
		{
			amiga_fatal(m, "MrgCop: out of memory");
			return;
		}
		hle_w32(m, view + V_LOFCPRLIST, cpr);
	}
	start = hle_r32(m, cpr + CPR_START);
	if(start && hle_r16(m, cpr + CPR_MAXCOUNT) < total + 3)
	{
		hle_free(m, start, hle_r16(m, cpr + CPR_MAXCOUNT) * 4u);
		start = 0;
	}
	if(!start)
	{
		start = hle_alloc(m, (total + 3) * 4, MEMF_CHIP, 0);
		if(!start)
		{
			amiga_fatal(m, "MrgCop: out of chip memory");
			return;
		}
		hle_w32(m, cpr + CPR_START, start);
		hle_w16(m, cpr + CPR_MAXCOUNT, (uint16_t)(total + 3));
	}
	for(vp = hle_r32(m, view + V_VIEWPORT); vp; vp = hle_r32(m, vp + VP_NEXT))
	{
		uint32_t cl = hle_r32(m, vp + VP_DSPINS), buf, i, n;
		if(!cl)
			continue;
		buf = hle_r32(m, cl + CL_LSTART);
		n = hle_r16(m, cl + CL_COUNT);
		hle_w32(m, cl + CL_COPPTR, start + 4 * at);
		for(i = 0; i < n; i++, at++)
			hle_w32(m, start + 4 * at, hle_r32(m, buf + 4 * i));
	}
	hle_w32(m, start + 4 * at, 0xFFFFFFFEu);
}

/* One colour of a ViewPort: its ColorMap entry, and the colour MOVE in both its own list and
 * where MrgCop merged it. */
static void gfx_set_color(struct Amiga *m, uint32_t vp, uint32_t i, uint16_t c)
{
	uint32_t cm = hle_r32(m, vp + VP_COLORMAP), cl = hle_r32(m, vp + VP_DSPINS);
	uint32_t ins = ((0x180u + 2 * i) << 16) | c;
	if(!cm || i >= hle_r16(m, cm + CM_COUNT))
		return;
	hle_w16(m, hle_r32(m, cm + CM_COLORTABLE) + 2 * i, c);
	if(!cl)
		return;
	hle_w32(m, hle_r32(m, cl + CL_LSTART) + 4 * (1 + i), ins);
	if(hle_r32(m, cl + CL_COPPTR))
		hle_w32(m, hle_r32(m, cl + CL_COPPTR) + 4 * (1 + i), ins);
}

/* A SimpleSprite's pointer goes into gb_copinit's SPRxPT MOVEs (instructions 2 + 2n and
 * 3 + 2n of the list at HLE_COPINIT). */
static void gfx_sprite_pointer(struct Amiga *m, int num, uint32_t data)
{
	if(num < 0 || num > 7)
		return;
	hle_w16(m, HLE_COPINIT + 4 * (2 + 2 * (uint32_t)num) + 2, (uint16_t)(data >> 16));
	hle_w16(m, HLE_COPINIT + 4 * (3 + 2 * (uint32_t)num) + 2, (uint16_t)data);
}

/* The SimpleSprite's x, y and height as hardware POS/CTL words at posctldata[0..1], offset by
 * the View's and ViewPort's display offsets. The horizontal -1 places sprite x 0 on the first
 * playfield pixel; not measured against a sprite on screen. */
static void gfx_sprite_position(struct Amiga *m, uint32_t vp, uint32_t ss)
{
	uint32_t view = hle_r32(m, HLE_GFX_BASE + GB_ACTIVIEW), data = hle_r32(m, ss);
	int x = (int16_t)hle_r16(m, ss + 0x06), y = (int16_t)hle_r16(m, ss + 0x08);
	int h = (int16_t)hle_r16(m, ss + 0x04), stop;
	if(!data)
		return;
	if(vp)
	{
		x += (int16_t)hle_r16(m, vp + VP_DXOFFSET);
		y += (int16_t)hle_r16(m, vp + VP_DYOFFSET);
	}
	if(view)
	{
		x += (int16_t)hle_r16(m, view + V_DXOFFSET) - 1;
		y += (int16_t)hle_r16(m, view + V_DYOFFSET);
	}
	stop = y + h;
	hle_w16(m, data, (uint16_t)(((y & 0xFF) << 8) | ((x >> 1) & 0xFF)));
	hle_w16(m, data + 2, (uint16_t)(((stop & 0xFF) << 8) | (((y >> 8) & 1) << 2) | (((stop >> 8) & 1) << 1) | (x & 1)));
}

/* BltBitMap's result, computed the way the blitter's area mode would leave it: inside the
 * rectangle, the upper minterm nibble with B = source and C = destination (graphics autodoc,
 * HRM "Blitter"); outside it, the destination untouched. Plane i takes part when mask bit i
 * is set and both bitmaps have that plane. Same-stride overlap runs in the direction that reads
 * every source bit before it is overwritten. */
static uint32_t gfx_blt_bitmap(struct Amiga *m, uint32_t sbm, int xs, int ys, uint32_t dbm, int xd, int yd, int w, int h, uint8_t lf, uint8_t mask)
{
	uint32_t sbpr = hle_r16(m, sbm + BM_BYTESPERROW), dbpr = hle_r16(m, dbm + BM_BYTESPERROW);
	int depth = hle_r8(m, sbm + BM_DEPTH), planes = 0, i;
	if(hle_r8(m, dbm + BM_DEPTH) < depth)
		depth = hle_r8(m, dbm + BM_DEPTH);
	if(depth > 8)
		depth = 8;
	if(w <= 0 || h <= 0)
		return 0;
	for(i = 0; i < depth; i++)
	{
		uint32_t sp, dp, s0, d0;
		int back, r, wx0, wx1;
		if(!(mask & (1u << i)))
			continue;
		sp = hle_r32(m, sbm + BM_PLANES + 4 * (uint32_t)i);
		dp = hle_r32(m, dbm + BM_PLANES + 4 * (uint32_t)i);
		s0 = sp + (uint32_t)ys * sbpr;
		d0 = dp + (uint32_t)yd * dbpr;
		if(sbpr == dbpr)
		{
			back = (uint64_t)d0 * 8 + (uint32_t)xd > (uint64_t)s0 * 8 + (uint32_t)xs;
		}
		else
		{
			if(s0 < d0 + (uint32_t)h * dbpr && d0 < s0 + (uint32_t)h * sbpr)
			{
				amiga_fatal(m, "BltBitMap between overlapping planes of strides %u and %u", sbpr, dbpr);
				return 0;
			}
			back = 0;
		}
		wx0 = xd >> 4;
		wx1 = (xd + w - 1) >> 4;
		for(r = 0; r < h; r++)
		{
			int row = back ? h - 1 - r : r, k;
			uint32_t srow = s0 + (uint32_t)row * sbpr, drow = d0 + (uint32_t)row * dbpr;
			for(k = 0; k <= wx1 - wx0; k++)
			{
				int wx = back ? wx1 - k : wx0 + k, bit;
				uint32_t addr = (drow + 2 * (uint32_t)wx) & 0x1FFFFE;
				uint16_t b = 0, c = mem_chip16(m, addr), in = 0, d = 0;
				for(bit = 0; bit < 16; bit++)
				{
					int px = wx * 16 + bit, sx;
					if(px < xd || px >= xd + w)
						continue;
					sx = xs + (px - xd);
					in |= (uint16_t)(0x8000u >> bit);
					if(mem_chip16(m, (srow + 2 * (uint32_t)(sx >> 4)) & 0x1FFFFE) & (0x8000u >> (sx & 15)))
						b |= (uint16_t)(0x8000u >> bit);
				}
				if(lf & 0x80) d |= (uint16_t)(b & c);
				if(lf & 0x40) d |= (uint16_t)(b & ~c);
				if(lf & 0x20) d |= (uint16_t)(~b & c);
				if(lf & 0x10) d |= (uint16_t)(~b & ~c);
				mem_chip_write16(m, addr, (uint16_t)((c & ~in) | (d & in)));
			}
		}
		planes++;
	}
	return (uint32_t)planes;
}

/* graphics' vertical-blank server (gb_vbsrv): the copper's second list follows gb_LOFlist. */
void hle_gfx_vblank(struct Amiga *m)
{
	uint32_t lof = hle_r32(m, HLE_GFX_BASE + GB_LOFLIST);
	if(lof)
	{
		hle_w16(m, 0xDFF084, (uint16_t)(lof >> 16));
		hle_w16(m, 0xDFF086, (uint16_t)lof);
	}
}

int hle_gfx_call(struct Amiga *m, uint32_t lvo)
{
	uint32_t i;
	switch(lvo)
	{
	case 30: /* BltBitMap(a0 src, d0 xSrc, d1 ySrc, a1 dest, d2 xDest, d3 yDest, d4 xSize,
	          * d5 ySize, d6 minterm, d7 mask, a2 tempA): d0 = planes blitted */
		D0 = gfx_blt_bitmap(m, A0, (int16_t)D0, (int16_t)D1, A1, (int16_t)D2, (int16_t)m->cpu.d[3],
			(int16_t)m->cpu.d[4], (int16_t)m->cpu.d[5], (uint8_t)m->cpu.d[6], (uint8_t)m->cpu.d[7]);
		return 0;
	case 204: /* InitVPort(a0 vp): SpritePriorities 0x24, as observed (docs/runtime.md, "HLE") */
		for(i = 0; i < VP_SIZE; i += 2)
			hle_w16(m, A0 + i, 0);
		hle_w8(m, A0 + VP_SPRITEPRI, 0x24);
		return 0;
	case 210: /* MrgCop(a1 view) */
		gfx_mrgcop(m, A1);
		return 0;
	case 216: /* MakeVPort(a0 view, a1 vp) */
		gfx_make_vport(m, A0, A1);
		return 0;
	case 222: /* LoadView(a1 view) */
		hle_w32(m, HLE_GFX_BASE + GB_ACTIVIEW, A1);
		if(!A1)
		{
			amiga_log_once(m, 0xC0000000u | lvo, "LoadView(NULL): ActiView cleared, copper left alone");
			return 0;
		}
		if(hle_r32(m, A1 + V_LOFCPRLIST))
			hle_w32(m, HLE_GFX_BASE + GB_LOFLIST, hle_r32(m, hle_r32(m, A1 + V_LOFCPRLIST) + CPR_START));
		return 0;
	case 408: /* GetSprite(a0 ss, d0 num): reserve that sprite, or the first free one for -1 */
	{
		uint8_t res = hle_r8(m, HLE_GFX_BASE + 0xA6);
		int want = (int16_t)D0, n = -1;
		if(want < 0)
		{
			for(n = 0; n < 8 && (res & (1u << n)); n++)
				;
			if(n == 8)
				n = -1;
		}
		else if(want < 8 && !(res & (1u << want)))
		{
			n = want;
		}
		if(n >= 0)
		{
			hle_w8(m, HLE_GFX_BASE + 0xA6, (uint8_t)(res | (1u << n)));
			hle_w16(m, A0 + 0x0A, (uint16_t)n);
		}
		D0 = (uint32_t)n;
		return 0;
	}
	case 420: /* ChangeSprite(a0 vp, a1 ss, a2 newdata) */
		hle_w32(m, A1, m->cpu.a[2]);
		gfx_sprite_pointer(m, hle_r16(m, A1 + 0x0A), m->cpu.a[2]);
		gfx_sprite_position(m, A0, A1);
		return 0;
	case 426: /* MoveSprite(a0 vp, a1 ss, d0 x, d1 y) */
		hle_w16(m, A1 + 0x06, (uint16_t)D0);
		hle_w16(m, A1 + 0x08, (uint16_t)D1);
		gfx_sprite_position(m, A0, A1);
		return 0;
	case 414: /* FreeSprite(d0 num): gb_SpriteReserved (GfxBase +0xA6) loses that bit */
		if((int32_t)(int16_t)D0 >= 0 && (D0 & 0xFFFF) < 8)
			hle_w8(m, HLE_GFX_BASE + 0xA6, (uint8_t)(hle_r8(m, HLE_GFX_BASE + 0xA6) & ~(1u << (D0 & 7))));
		return 0;
	case 192: /* LoadRGB4(a0 vp, a1 colors, d0 count) */
	{
		uint32_t want = (uint32_t)(int16_t)D0 > 0 ? (uint32_t)(int16_t)D0 : 0;
		for(i = 0; i < want && i < 32; i++)
			gfx_set_color(m, A0, i, hle_r16(m, A1 + 2 * i));
		return 0;
	}
	case 288: /* SetRGB4(a0 vp, d0 index, d1 red, d2 green, d3 blue), 4 bits a gun */
		gfx_set_color(m, A0, D0 & 31, (uint16_t)(((D1 & 15) << 8) | ((D2 & 15) << 4) | (m->cpu.d[3] & 15)));
		return 0;
	case 198: /* InitRastPort(a1 rp): zero the first 0x4E bytes, then Mask, FgPen, AOlPen and
	           * LinePtrn all ones and DrawMode JAM2 (graphics autodoc); the last 0x16 bytes of
	           * the structure stay as they were (docs/runtime.md, "Observed"); there is no ROM
	           * font to point Font at */
		for(i = 0; i < 0x4E; i += 2)
			hle_w16(m, A1 + i, 0);
		hle_w8(m, A1 + 0x18, 0xFF);
		hle_w8(m, A1 + 0x19, 0xFF);
		hle_w8(m, A1 + 0x1B, 0xFF);
		hle_w8(m, A1 + 0x1C, 1);
		hle_w16(m, A1 + 0x22, 0xFFFF);
		amiga_log_once(m, 0xC1000000u, "InitRastPort: rp_Font left 0, there is no ROM font");
		return 0;
	case 228: /* WaitBlit: the blitter finishes instantly */
		return 0;
	case 270: /* WaitTOF: park until the next vertical blank */
		m->hle.wait_tof_frame = m->frame_count + 1;
		m->cpu.pc = HLE_WAIT_TOF;
		return 1;
	case 360: /* InitView(a1 view): DyOffset 0x2C, DxOffset 0x81, as observed (docs/runtime.md, "HLE") */
		for(i = 0; i < V_SIZE; i += 2)
			hle_w16(m, A1 + i, 0);
		hle_w16(m, A1 + V_DYOFFSET, 0x2C);
		hle_w16(m, A1 + V_DXOFFSET, 0x81);
		return 0;
	case 384: /* VBeamPos: the beam's vertical position, 0-312 on PAL */
		D0 = (uint32_t)sched_line(m, amiga_now(m));
		return 0;
	case 390: /* InitBitMap(a0 bm, d0 depth, d1 width, d2 height) */
		hle_w16(m, A0 + BM_BYTESPERROW, (uint16_t)(((D1 + 15) >> 4) << 1));
		hle_w16(m, A0 + BM_ROWS, (uint16_t)D2);
		hle_w8(m, A0 + BM_FLAGS, 0);
		hle_w8(m, A0 + BM_DEPTH, (uint8_t)D0);
		hle_w16(m, A0 + 6, 0);
		return 0;
	case 456: /* OwnBlitter */
	case 462: /* DisownBlitter */
		return 0;
	default:
		return -1;
	}
}
