#include "hle.h"
#include "cpu_ops.h"

#include <stdio.h>
#include <string.h>

/* The A1200's Workbench screen and one CON: window on it, drawn by the HLE into a hires
 * two-plane bitmap in chip RAM and shown through a View of its own (docs/runtime.md,
 * "Console"). The screen's display registers, its palette and the window's border widths are
 * the ones measured on the oracle at Pinball Fantasies AGA's menu; the font
 * (core/hle_font.c) and the gadgets' images are ours. Console control codes and CSI sequences
 * per the console.device autodoc (ADCD 2.1, "console.device"). */

#if AMIGA_A1200

#define CON_W 640
#define CON_H 256
#define CON_ROW 80
#define CON_PLANE (CON_ROW * CON_H)
/* Border widths of a sizable window with a close-less title bar, measured: left 4, top 11,
 * right 18 (the sizing gadget's column), bottom 2. */
#define CON_LEFT 4
#define CON_TOP 11
#define CON_RIGHT 18
#define CON_BOTTOM 2
/* An EasyRequestArgs requester, ours (Kickstart's layout is not measured): the border of the
 * CON: window with the left border's width on the right too, an 8-pixel margin round the body
 * and the gadgets, and gadgets 14 pixels high with an 8-pixel margin round their text. */
#define REQ_PAD 8
#define REQ_GADGET_H 14
#define REQ_LINES 16
/* The pointer: sprite 0, a cross of our own 11 lores pixels square whose centre is the hot spot,
 * which denise_pointer reports to the frontend's steering (hle_con_pointer), so the OS cursor
 * carries it whether it is drawn or not. Its two colours are the pointer's on the oracle's Workbench
 * screen (reference/menu.png: #EE4444 and #EEEECC), as sprite colours 17 and 19 of the bank
 * BPLCON4 0x0011 selects; which register Kickstart gives which is not measured. */
#define PTR_SIZE 11
#define PTR_HOT 5
#define PTR_BYTES (4 * (PTR_SIZE + 2))
/* The display window's first pixel, DIWSTRT 0x2C81: line 0x2C, DIW x 0x81. */
#define CON_DIW_X 0x81
#define CON_DIW_Y 0x2C
/* Raw key codes, HRM appendix H. */
#define CON_KEY_V 0x34
#define CON_KEY_B 0x35
#define CON_KEY_LAMIGA 0x66

enum ConPen { ConGrey, ConBlack, ConWhite, ConBlue };

static void con_pixel(struct Amiga *m, int x, int y, int pen)
{
	uint32_t at = m->hle.con.bitmap + (uint32_t)(y * CON_ROW + (x >> 3));
	uint8_t bit = (uint8_t)(0x80 >> (x & 7));
	uint8_t *p0, *p1;
	if(x < 0 || x >= CON_W || y < 0 || y >= CON_H)
		return;
	p0 = mem_ram(m, at);
	p1 = mem_ram(m, at + CON_PLANE);
	*p0 = (uint8_t)((pen & 1) ? *p0 | bit : *p0 & ~bit);
	*p1 = (uint8_t)((pen & 2) ? *p1 | bit : *p1 & ~bit);
}

static int con_get(struct Amiga *m, int x, int y)
{
	uint32_t at = m->hle.con.bitmap + (uint32_t)(y * CON_ROW + (x >> 3));
	uint8_t bit = (uint8_t)(0x80 >> (x & 7));
	return ((*mem_ram(m, at) & bit) ? 1 : 0) | ((*mem_ram(m, at + CON_PLANE) & bit) ? 2 : 0);
}

static void con_fill(struct Amiga *m, int x0, int y0, int x1, int y1, int pen)
{
	int x, y;
	for(y = y0; y <= y1; y++)
		for(x = x0; x <= x1; x++)
			con_pixel(m, x, y, pen);
}

static void con_frame(struct Amiga *m, int x0, int y0, int x1, int y1, int pen)
{
	con_fill(m, x0, y0, x1, y0, pen);
	con_fill(m, x0, y1, x1, y1, pen);
	con_fill(m, x0, y0, x0, y1, pen);
	con_fill(m, x1, y0, x1, y1, pen);
}

/* A character of our font at (x, y): its set pixels in `fg`, the others in `bg`, or left as
 * they are when `bg` is negative. */
static void con_glyph(struct Amiga *m, int x, int y, int ch, int fg, int bg)
{
	int r, c;
	if(ch < 32 || ch > 127)
		ch = 127;
	for(r = 0; r < 8; r++)
		for(c = 0; c < 8; c++)
		{
			if(hle_font8[ch - 32][r] & (0x80 >> c))
				con_pixel(m, x + c, y + r, fg);
			else if(bg >= 0)
				con_pixel(m, x + c, y + r, bg);
		}
}

static void con_text(struct Amiga *m, int x, int y, const char *s, int fg)
{
	for(; *s; s++, x += 8)
		con_glyph(m, x, y, (unsigned char)*s, fg, -1);
}

/* Two overlapping frames, the back one hollow and the front one filled: our image for a depth
 * gadget whose top-left is (x, y). */
static void con_depth_image(struct Amiga *m, int x, int y)
{
	con_frame(m, x, y, x + 10, y + 4, ConBlack);
	con_fill(m, x + 4, y + 2, x + 14, y + 6, ConWhite);
	con_frame(m, x + 4, y + 2, x + 14, y + 6, ConBlack);
}

/* An active window from (wx0, wy0) to (wx1, wy1) with a right border `right` pixels wide. The
 * border, measured on the CON: window: white light edges, blue fill, black shadow edges, and the
 * interior's black and white inner edges. The title bar: the title, and at its right end a
 * depth gadget of 22 pixels with a black and a white line before it, measured; its image is
 * ours. */
static void con_window(struct Amiga *m, int wx0, int wy0, int wx1, int wy1, int right, const char *title)
{
	con_fill(m, wx0, wy0, wx1, wy1, ConBlue);
	con_fill(m, wx0, wy0, wx1, wy0, ConWhite);
	con_fill(m, wx0, wy0, wx0, wy1, ConWhite);
	con_fill(m, wx1, wy0 + 1, wx1, wy1, ConBlack);
	con_fill(m, wx0 + 1, wy1, wx1, wy1, ConBlack);
	con_fill(m, wx0 + 3, wy0 + CON_TOP - 1, wx1, wy0 + CON_TOP - 1, ConBlack);
	con_fill(m, wx0 + 3, wy0 + CON_TOP, wx0 + 3, wy1 - 2, ConBlack);
	con_fill(m, wx1 - right + 1, wy0 + CON_TOP, wx1 - right + 1, wy1 - 2, ConWhite);
	con_fill(m, wx0 + CON_LEFT, wy1 - 1, wx1 - right + 1, wy1 - 1, ConWhite);
	con_fill(m, wx0 + CON_LEFT, wy0 + CON_TOP, wx1 - right, wy1 - CON_BOTTOM - 1, ConGrey);
	con_text(m, wx0 + 5, wy0 + 1, title, ConBlack);
	con_fill(m, wx1 - 23, wy0 + 1, wx1 - 23, wy0 + 9, ConBlack);
	con_fill(m, wx1 - 22, wy0 + 1, wx1 - 22, wy0 + 9, ConWhite);
	con_depth_image(m, wx1 - 18, wy0 + 2);
}

/* The view: the screen's display registers, measured at the menu (docs/runtime.md, "Console"),
 * then its two planes and four colours, and once the pointer exists its colours and the eight
 * sprite pointers: sprite 0 the pointer's, the others the null sprite. */
static void con_view(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	static const uint16_t regs[] = {
		0x008E, 0x2C81, 0x0090, 0x2CC1, 0x01E4, 0x2100, 0x0092, 0x0038, 0x0094, 0x00D8,
		0x0100, 0xA200, 0x0102, 0x0000, 0x0104, 0x0224, 0x0106, 0x0C40, 0x010C, 0x0011,
		0x0108, 0xFFF8, 0x010A, 0xFFF8, 0x01FC, 0x0003,
		0x0180, 0x0AAA, 0x0182, 0x0000, 0x0184, 0x0FFF, 0x0186, 0x068B,
	};
	uint32_t at = c->copper, i, cpr = c->view + V_SIZE;
	for(i = 0; i < sizeof regs / sizeof regs[0]; i++, at += 2)
		hle_w16(m, at, regs[i]);
	for(i = 0; i < 2; i++)
	{
		uint32_t plane = c->bitmap + i * CON_PLANE;
		hle_w16(m, at, (uint16_t)(0x00E0 + 4 * i));
		hle_w16(m, at + 2, (uint16_t)(plane >> 16));
		hle_w16(m, at + 4, (uint16_t)(0x00E2 + 4 * i));
		hle_w16(m, at + 6, (uint16_t)plane);
		at += 8;
	}
	if(c->sprite)
	{
		static const uint16_t colours[] = {0x01A2, 0x0E44, 0x01A6, 0x0EEC};
		for(i = 0; i < 4; i++, at += 2)
			hle_w16(m, at, colours[i]);
		for(i = 0; i < 8; i++)
		{
			uint32_t data = i ? c->sprite : c->sprite + 8;
			hle_w16(m, at, (uint16_t)(0x0120 + 4 * i));
			hle_w16(m, at + 2, (uint16_t)(data >> 16));
			hle_w16(m, at + 4, (uint16_t)(0x0122 + 4 * i));
			hle_w16(m, at + 6, (uint16_t)data);
			at += 8;
		}
	}
	hle_w32(m, at, 0xFFFFFFFEu);
	hle_gfx_init_view(m, c->view);
	hle_w32(m, c->view + V_LOFCPRLIST, cpr);
	hle_w32(m, cpr + CPR_START, c->copper);
	hle_w16(m, cpr + CPR_MAXCOUNT, (uint16_t)((at + 4 - c->copper) / 4));
	hle_gfx_load_view(m, c->view);
	hle_w16(m, 0xDFF096, 0x8300);
}

/* The Workbench screen: grey, with its title bar (white, a black line under it) and our depth
 * gadget at its right end; the bar's geometry is measured. */
static int con_workbench(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	uint32_t planes, mem;
	if(c->bitmap)
		return 1;
	planes = hle_alloc(m, 2 * CON_PLANE + 8, MEMF_CHIP | MEMF_CLEAR, 0);
	mem = hle_alloc(m, 64 * 4 + V_SIZE + 12, MEMF_CHIP | MEMF_CLEAR, 0);
	if(!planes || !mem)
	{
		amiga_fatal(m, "console: no chip memory for the Workbench screen");
		return 0;
	}
	/* FMODE 3 fetches 64 bits at a time, from an address a multiple of 8 (AGA). */
	c->bitmap = (planes + 7) & ~7u;
	c->copper = mem;
	c->view = mem + 64 * 4;
	con_fill(m, 0, 0, CON_W - 1, 9, ConWhite);
	con_fill(m, 0, 10, CON_W - 1, 10, ConBlack);
	con_text(m, 5, 1, "Workbench Screen", ConBlack);
	con_fill(m, 618, 0, 638, 9, ConGrey);
	con_fill(m, 639, 0, 639, 9, ConBlack);
	con_depth_image(m, 621, 2);
	con_view(m);
	amiga_logf(m, "console: the Workbench screen is open");
	return 1;
}

static void con_cursor(struct Amiga *m, int on)
{
	struct HleCon *c = &m->hle.con;
	int x = c->x0 + 8 * c->col, y = c->y0 + 8 * c->row, i, j;
	if(!c->cursor_on || c->col >= c->cols)
		return;
	for(j = 0; j < 8; j++)
		for(i = 0; i < 8; i++)
			con_pixel(m, x + i, y + j, on ? ConBlue : ConGrey);
}

static void con_scroll(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	int x, y, last = c->y0 + 8 * c->rows - 1;
	for(y = c->y0; y + 8 <= last; y++)
		for(x = c->x0; x < c->x0 + 8 * c->cols; x++)
			con_pixel(m, x, y, con_get(m, x, y + 8));
	con_fill(m, c->x0, last - 7, c->x0 + 8 * c->cols - 1, last, ConGrey);
	c->row = c->rows - 1;
}

static void con_newline(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	c->col = 0;
	if(++c->row >= c->rows)
		con_scroll(m);
}

/* CON:x/y/width/height/title (AmigaDOS Manual, "Devices"). */
int hle_con_open(struct Amiga *m, const char *spec)
{
	struct HleCon *c = &m->hle.con;
	int x, y, w, h, n = 0, wx0, wy0, wx1, wy1;
	const char *title;
	if(c->open)
	{
		amiga_fatal(m, "console: a second CON: window (\"%s\") is not implemented", spec);
		return 0;
	}
	if(sscanf(spec + 4, "%d/%d/%d/%d/%n", &x, &y, &w, &h, &n) != 4 || !n)
	{
		amiga_fatal(m, "console: \"%s\" is not CON:x/y/width/height/title", spec);
		return 0;
	}
	title = spec + 4 + n;
	if(x < 0 || y < 11 || w < CON_LEFT + CON_RIGHT + 8 || h < CON_TOP + CON_BOTTOM + 8 || x + w > CON_W || y + h > CON_H)
	{
		amiga_fatal(m, "console: the window of \"%s\" does not fit the Workbench screen", spec);
		return 0;
	}
	if(!con_workbench(m))
		return 0;
	wx0 = x;
	wy0 = y;
	wx1 = x + w - 1;
	wy1 = y + h - 1;
	con_window(m, wx0, wy0, wx1, wy1, CON_RIGHT, title);
	/* The zoom gadget, 24 pixels with a black and a white line before it, measured; its image
	 * is ours. */
	con_fill(m, wx1 - 46, wy0 + 1, wx1 - 46, wy0 + 9, ConBlack);
	con_fill(m, wx1 - 45, wy0 + 1, wx1 - 45, wy0 + 9, ConWhite);
	con_frame(m, wx1 - 40, wy0 + 2, wx1 - 28, wy0 + 8, ConBlack);
	con_fill(m, wx1 - 39, wy0 + 3, wx1 - 34, wy0 + 5, ConWhite);
	/* The sizing gadget in the right border's foot: a white line over it, our triangle. */
	con_fill(m, wx1 - CON_RIGHT + 1, wy1 - 9, wx1 - 1, wy1 - 9, ConWhite);
	for(n = 0; n < 6; n++)
		con_fill(m, wx1 - 4 - 2 * n, wy1 - 2 - n, wx1 - 4, wy1 - 2 - n, ConWhite);
	con_fill(m, wx1 - 14, wy1 - 2, wx1 - 4, wy1 - 2, ConBlack);
	c->open = 1;
	c->x0 = wx0 + CON_LEFT;
	c->y0 = wy0 + CON_TOP;
	c->cols = (w - CON_LEFT - CON_RIGHT) / 8;
	c->rows = (h - CON_TOP - CON_BOTTOM - 1) / 8;
	c->wx0 = wx0;
	c->wy0 = wy0;
	c->wx1 = wx1;
	c->wy1 = wy1;
	c->col = c->row = 0;
	c->cursor_on = 1;
	c->line_len = 0;
	c->ready = 0;
	c->esc = 0;
	con_cursor(m, 1);
	amiga_logf(m, "console: window %d,%d %dx%d \"%s\", %d columns and %d lines", x, y, w, h, title, c->cols, c->rows);
	return 1;
}

void hle_con_close(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	if(!c->open)
		return;
	con_fill(m, c->wx0, c->wy0, c->wx1, c->wy1, ConGrey);
	c->open = 0;
}

/* A CSI sequence's final character with its parameters (console.device autodoc). */
static void con_csi(struct Amiga *m, int final)
{
	struct HleCon *c = &m->hle.con;
	int p0 = c->args > 0 ? c->arg[0] : 0, p1 = c->args > 1 ? c->arg[1] : 0, x, y;
	switch(final)
	{
	case 'H':
		c->row = (p0 ? p0 : 1) - 1;
		c->col = (p1 ? p1 : 1) - 1;
		if(c->row >= c->rows)
			c->row = c->rows - 1;
		if(c->col >= c->cols)
			c->col = c->cols - 1;
		return;
	case 'A': c->row -= p0 ? p0 : 1; if(c->row < 0) c->row = 0; return;
	case 'B': c->row += p0 ? p0 : 1; if(c->row >= c->rows) c->row = c->rows - 1; return;
	case 'C': c->col += p0 ? p0 : 1; if(c->col >= c->cols) c->col = c->cols - 1; return;
	case 'D': c->col -= p0 ? p0 : 1; if(c->col < 0) c->col = 0; return;
	case 'J':
		for(y = c->row; y < c->rows; y++)
			for(x = y == c->row ? c->col : 0; x < c->cols; x++)
				con_glyph(m, c->x0 + 8 * x, c->y0 + 8 * y, ' ', ConBlack, ConGrey);
		return;
	case 'K':
		for(x = c->col; x < c->cols; x++)
			con_glyph(m, c->x0 + 8 * x, c->y0 + 8 * c->row, ' ', ConBlack, ConGrey);
		return;
	case 'p':
		/* "CSI 0 SPACE p" hides the cursor, "CSI SPACE p" shows it. */
		if(c->space)
			c->cursor_on = !(c->args && !p0);
		return;
	default:
		amiga_log_once(m, 0xD0000000u | (uint32_t)final, "console: CSI sequence ending '%c' ignored", final);
		return;
	}
}

static void con_put(struct Amiga *m, int ch)
{
	struct HleCon *c = &m->hle.con;
	if(c->esc == 1)
	{
		c->esc = ch == '[' ? 2 : 0;
		c->args = 0;
		c->space = 0;
		c->arg[0] = 0;
		return;
	}
	if(c->esc == 2)
	{
		if(ch >= '0' && ch <= '9')
		{
			if(!c->args)
				c->args = 1;
			c->arg[c->args - 1] = c->arg[c->args - 1] * 10 + (ch - '0');
			return;
		}
		if(ch == ';')
		{
			if(!c->args)
				c->args = 1;
			if(c->args < 4)
				c->arg[c->args++] = 0;
			return;
		}
		if(ch == ' ')
		{
			c->space = 1;
			return;
		}
		c->esc = 0;
		con_csi(m, ch);
		return;
	}
	switch(ch)
	{
	case 0x1B: c->esc = 1; return;
	case 0x9B: c->esc = 2; c->args = 0; c->space = 0; c->arg[0] = 0; return;
	case 0x07: return;
	case 0x08: if(c->col > 0) c->col--; return;
	case 0x09: c->col = (c->col + 8) & ~7; if(c->col >= c->cols) con_newline(m); return;
	case 0x0A: con_newline(m); return;
	case 0x0B: if(c->row > 0) c->row--; return;
	case 0x0C:
		con_fill(m, c->x0, c->y0, c->x0 + 8 * c->cols - 1, c->y0 + 8 * c->rows - 1, ConGrey);
		c->col = c->row = 0;
		return;
	case 0x0D: c->col = 0; return;
	default:
		break;
	}
	if(ch < 0x20 || (ch >= 0x80 && ch < 0xA0))
		return;
	if(c->col >= c->cols)
		con_newline(m);
	con_glyph(m, c->x0 + 8 * c->col, c->y0 + 8 * c->row, ch, ConBlack, ConGrey);
	c->col++;
}

void hle_con_write(struct Amiga *m, uint32_t buf, uint32_t len)
{
	uint32_t i;
	if(!m->hle.con.open)
		return;
	con_cursor(m, 0);
	for(i = 0; i < len; i++)
		con_put(m, hle_r8(m, buf + i));
	con_cursor(m, 1);
}

/* Copies what the line holds, up to `len`, to `buf`; the rest stays for the next Read. */
static uint32_t con_take(struct Amiga *m, uint32_t buf, uint32_t len)
{
	struct HleCon *c = &m->hle.con;
	uint32_t n = (uint32_t)c->line_len < len ? (uint32_t)c->line_len : len, i;
	for(i = 0; i < n; i++)
		hle_w8(m, buf + i, (uint8_t)c->line[i]);
	memmove(c->line, c->line + n, (size_t)c->line_len - n);
	c->line_len -= (int)n;
	if(!c->line_len)
		c->ready = 0;
	return n;
}

/* Read: a whole line, Return included, as the console's cooked mode gives it. Returns the
 * count, or -1 when the reader must wait for Return (hle_con_park). */
int32_t hle_con_read(struct Amiga *m, uint32_t buf, uint32_t len)
{
	struct HleCon *c = &m->hle.con;
	if(!c->open)
	{
		amiga_fatal(m, "console: Read with no CON: window open");
		return 0;
	}
	if(c->ready)
		return (int32_t)con_take(m, buf, len);
	c->read_buf = buf;
	c->read_len = len;
	c->reading = 1;
	return -1;
}

/* The parked reader (internal entry): 1 while it waits, 0 once D0 holds the count. */
int hle_con_park(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	if(!c->ready)
		return 1;
	c->reading = 0;
	m->cpu.d[0] = m->cpu.d[1] = con_take(m, c->read_buf, c->read_len);
	m->cpu.pc = cpu_pop32(m);
	return 0;
}

/* The usa keymap's characters for raw key codes 0x00-0x3F, unshifted and shifted (HRM appendix
 * H for the codes; RKM Libraries, "Keymap Library", for the usa map); 0 for none. */
static const char con_keys[2][0x40] = {
	{
		'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, '0',
		'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, '1', '2', '3',
		'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, '4', '5', '6',
		0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '.', '7', '8', '9',
	},
	{
		'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '|', 0, '0',
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, '1', '2', '3',
		'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0, 0, '4', '5', '6',
		0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '.', '7', '8', '9',
	},
};

/* A key from keyboard.device: Shift is tracked; while a requester is up its window is the active
 * one, and left Amiga V and left Amiga B answer it as its leftmost and rightmost gadgets would
 * (RKM Libraries, "Intuition Requesters", "Easy Requesters"); otherwise, while the window is
 * open, a typed character is echoed and kept, Backspace takes one back, and Return ends the
 * line. */
void hle_con_key(struct Amiga *m, uint8_t code)
{
	struct HleCon *c = &m->hle.con;
	int key = code & 0x7F, up = code & 0x80, ch = 0;
	if(key == 0x60 || key == 0x61)
	{
		c->shift = up ? (uint8_t)(c->shift & ~(1u << (key - 0x60))) : (uint8_t)(c->shift | (1u << (key - 0x60)));
		return;
	}
	if(c->req_save)
	{
		if(!up && !c->req_done && (key == CON_KEY_V || key == CON_KEY_B)
			&& (m->hle.key_matrix[CON_KEY_LAMIGA >> 3] & (1u << (CON_KEY_LAMIGA & 7))))
		{
			c->req_answer = key == CON_KEY_V && c->req_gadgets > 1 ? 1 : 0;
			c->req_done = 1;
		}
		return;
	}
	if(up || !c->open || c->ready)
		return;
	if(key < 0x40)
		ch = con_keys[c->shift ? 1 : 0][key];
	else if(key == 0x40)
		ch = ' ';
	else if(key == 0x41)
	{
		if(c->line_len > 0)
		{
			con_cursor(m, 0);
			c->line_len--;
			if(c->col > 0)
				c->col--;
			con_cursor(m, 1);
		}
		return;
	}
	else if(key == 0x43 || key == 0x44)
	{
		con_cursor(m, 0);
		if((size_t)c->line_len < sizeof c->line)
			c->line[c->line_len++] = '\n';
		con_newline(m);
		con_cursor(m, 1);
		c->ready = 1;
		return;
	}
	if(!ch || (size_t)c->line_len >= sizeof c->line - 1)
		return;
	c->line[c->line_len++] = (char)ch;
	con_cursor(m, 0);
	con_put(m, ch);
	con_cursor(m, 1);
}

/* --- requesters --------------------------------------------------------------------------- */

/* intuition/intuition.i, EasyStruct. */
#define ES_TITLE 8
#define ES_TEXTFORMAT 12
#define ES_GADGETFORMAT 16

/* RawDoFmt's formatting of `fmt` with `data` (hle_fmt) into `out`, and the data after it in
 * *next. Returns 0 after a fatal. */
static int con_format(struct Amiga *m, uint32_t fmt, uint32_t data, char *out, size_t size, uint32_t *next)
{
	uint32_t len, buf = hle_fmt(m, fmt, data, &len, next), i;
	if(!buf)
		return 0;
	for(i = 0; i < len && i < size - 1; i++)
		out[i] = (char)hle_r8(m, buf + i);
	out[i] = 0;
	hle_free(m, buf, len);
	return 1;
}

/* `s` cut at each `sep` into at most `max` parts; the count, or -1 for more. */
static int con_split(char *s, int sep, char **parts, int max)
{
	int n = 0;
	for(;;)
	{
		if(n == max)
			return -1;
		parts[n++] = s;
		if(!(s = strchr(s, sep)))
			return n;
		*s++ = 0;
	}
}

static uint32_t con_request_size(const struct HleCon *c)
{
	return (uint32_t)(c->req_w / 8 * c->req_h * 2);
}

/* The requester's rectangle of both planes, to the saved backdrop (`save`) or back from it. */
static void con_request_backdrop(struct Amiga *m, int save)
{
	struct HleCon *c = &m->hle.con;
	uint32_t at = c->req_save, p, y, x;
	for(p = 0; p < 2; p++)
		for(y = 0; y < (uint32_t)c->req_h; y++)
			for(x = 0; x < (uint32_t)c->req_w / 8; x++, at++)
			{
				uint32_t screen = c->bitmap + p * CON_PLANE + y * CON_ROW + x;
				if(save)
					hle_w8(m, at, hle_r8(m, screen));
				else
					hle_w8(m, screen, hle_r8(m, at));
			}
}

/* A gadget `w` pixels wide: white light edges, black shadow edges, its text centred. */
static void con_button(struct Amiga *m, int x, int y, int w, const char *text)
{
	con_fill(m, x, y, x + w - 1, y, ConWhite);
	con_fill(m, x, y, x, y + REQ_GADGET_H - 1, ConWhite);
	con_fill(m, x + 1, y + REQ_GADGET_H - 1, x + w - 1, y + REQ_GADGET_H - 1, ConBlack);
	con_fill(m, x + w - 1, y + 1, x + w - 1, y + REQ_GADGET_H - 1, ConBlack);
	con_text(m, x + REQ_PAD, y + (REQ_GADGET_H - 8) / 2, text, ConBlack);
}

/* The pointer's sprite data in chip RAM, once: a null sprite (its two control words 0), then
 * the pointer's control words, its rows (a red cross, the hot spot cream) and the two words that
 * end it. Returns 0 after a fatal. */
static int con_pointer_alloc(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	int r;
	if(c->sprite)
		return 1;
	c->sprite = hle_alloc(m, 8 + PTR_BYTES, MEMF_CHIP | MEMF_CLEAR, 0);
	if(!c->sprite)
	{
		amiga_fatal(m, "intuition: no chip memory for the pointer");
		return 0;
	}
	for(r = 0; r < PTR_SIZE; r++)
	{
		uint32_t row = c->sprite + 12 + 4 * (uint32_t)r;
		hle_w16(m, row, (uint16_t)(r == PTR_HOT ? 0xFFFF << (16 - PTR_SIZE) : 0x8000 >> PTR_HOT));
		hle_w16(m, row + 2, (uint16_t)(r == PTR_HOT ? 0x8000 >> PTR_HOT : 0));
	}
	return 1;
}

/* The pointer's control words: its hot spot at (ptr_x, ptr_y), a lores sprite whose column c
 * shows at DIW x HSTART + 1 + c (denise_sprite), or hidden (both words 0). */
static void con_pointer(struct Amiga *m, int shown)
{
	struct HleCon *c = &m->hle.con;
	int hstart = CON_DIW_X + c->ptr_x / 2 - 1 - PTR_HOT, vstart = CON_DIW_Y + c->ptr_y - PTR_HOT, vstop = vstart + PTR_SIZE;
	uint32_t at = c->sprite + 8;
	hle_w16(m, at, shown ? (uint16_t)(((vstart & 0xFF) << 8) | ((hstart >> 1) & 0xFF)) : 0);
	hle_w16(m, at + 2, shown ? (uint16_t)(((vstop & 0xFF) << 8) | (((vstart >> 8) & 1) << 2) | (((vstop >> 8) & 1) << 1) | (hstart & 1)) : 0);
}

/* The gadget under the pointer's hot spot, or -1. */
static int con_gadget_at(const struct HleCon *c)
{
	int i;
	for(i = 0; i < c->req_gadgets; i++)
		if(c->ptr_x >= c->req_gadget_x[i] && c->ptr_x < c->req_gadget_x[i] + c->req_gadget_w[i]
			&& c->ptr_y >= c->req_gadget_y && c->ptr_y < c->req_gadget_y + REQ_GADGET_H)
			return i;
	return -1;
}

/* The answer of gadget i: left to right 1, 2, ..., n - 1, and 0 for the rightmost. */
static void con_request_answer(struct HleCon *c, int i)
{
	c->req_answer = i == c->req_gadgets - 1 ? 0 : i + 1;
	c->req_done = 1;
}

/* EasyRequestArgs (RKM Libraries, "Intuition Requesters", "Easy Requesters" and "System
 * Requesters"): with no window, on the default public screen, the Workbench screen, in the upper
 * left corner, the screen moved to the front; the title es_Title or "System Request"; the body's
 * lines (cut at LF) and the gadgets' texts (cut at '|', evenly spaced from left to right) each
 * formatted as RawDoFmt does, the gadgets' arguments after the body's. The requester ends with a
 * click on a gadget (hle_con_vblank), left Amiga V or B (hle_con_key), or with -1 on an event of
 * the flags at `idcmp`, of which only IDCMP_DISKINSERTED is implemented (hle_con_disk_inserted);
 * the pointer shows while it is up. Returns 0 after a fatal; otherwise the caller parks at
 * HLE_REQ_PARK. */
int hle_con_request(struct Amiga *m, uint32_t window, uint32_t es, uint32_t idcmp, uint32_t args)
{
	struct HleCon *c = &m->hle.con;
	char title[64], body[512], gadgets[256], *line[REQ_LINES], *gadget[HLE_REQ_GADGETS], log[160];
	uint32_t next, behind = hle_r32(m, HLE_GFX_BASE + GB_ACTIVIEW);
	int lines, count, i, w, h, x, y, gap, right, inner, longest = 0, total = 0, width[HLE_REQ_GADGETS];
	size_t n = 0;
	if(window)
	{
		amiga_fatal(m, "EasyRequestArgs on a window's screen is not implemented (only the Workbench screen)");
		return 0;
	}
	if(c->req_save)
	{
		amiga_fatal(m, "EasyRequestArgs: a second requester is not implemented");
		return 0;
	}
	if(idcmp && (hle_r32(m, idcmp) & ~IDCMP_DISKINSERTED))
	{
		amiga_fatal(m, "EasyRequestArgs: IDCMP flags 0x%08X are not implemented (only IDCMP_DISKINSERTED)", hle_r32(m, idcmp));
		return 0;
	}
	if(hle_r32(m, es + ES_TITLE))
		hle_name(m, hle_r32(m, es + ES_TITLE), title);
	else
		snprintf(title, sizeof title, "System Request");
	if(!con_format(m, hle_r32(m, es + ES_TEXTFORMAT), args, body, sizeof body, &next)
		|| !con_format(m, hle_r32(m, es + ES_GADGETFORMAT), next, gadgets, sizeof gadgets, &next))
		return 0;
	lines = con_split(body, '\n', line, REQ_LINES);
	count = con_split(gadgets, '|', gadget, HLE_REQ_GADGETS);
	if(lines < 0 || count < 0)
	{
		amiga_fatal(m, "EasyRequestArgs: more than %d lines or %d gadgets is not implemented", REQ_LINES, HLE_REQ_GADGETS);
		return 0;
	}
	for(i = 0; i < lines; i++)
	{
		int len = (int)strlen(line[i]);
		longest = len > longest ? len : longest;
		n += (size_t)snprintf(log + n, sizeof log - n, "%s%s", i ? " / " : "", line[i]);
		n = n < sizeof log ? n : sizeof log - 1;
	}
	for(i = 0; i < count; i++)
	{
		width[i] = 8 * (int)strlen(gadget[i]) + 2 * REQ_PAD;
		total += width[i];
		n += (size_t)snprintf(log + n, sizeof log - n, "%s%s", i ? "|" : " [", gadget[i]);
		n = n < sizeof log ? n : sizeof log - 1;
	}
	inner = total + (count - 1) * REQ_PAD > 8 * longest ? total + (count - 1) * REQ_PAD : 8 * longest;
	w = 2 * CON_LEFT + 2 * REQ_PAD + inner;
	w = w > 31 + 8 * (int)strlen(title) ? w : 31 + 8 * (int)strlen(title);
	c->req_w = (w + 7) & ~7;
	c->req_h = CON_TOP + CON_BOTTOM + 1 + 3 * REQ_PAD + 8 * lines + REQ_GADGET_H;
	w = c->req_w;
	h = c->req_h;
	if(w > CON_W || h > CON_H)
	{
		amiga_fatal(m, "EasyRequestArgs: a %d x %d requester does not fit the Workbench screen", w, h);
		return 0;
	}
	if(!con_workbench(m))
		return 0;
	c->req_save = hle_alloc(m, con_request_size(c), MEMF_PUBLIC, 0);
	if(!c->req_save)
	{
		amiga_fatal(m, "EasyRequestArgs: no memory for the requester");
		return 0;
	}
	c->req_idcmp = idcmp;
	c->req_gadgets = (uint8_t)count;
	c->req_done = 0;
	c->req_front = behind != c->view;
	c->req_behind = behind;
	if(!con_pointer_alloc(m))
		return 0;
	con_view(m);
	hle_w16(m, 0xDFF096, 0x8020);
	con_request_backdrop(m, 1);
	right = w - 1 - CON_LEFT;
	con_window(m, 0, 0, w - 1, h - 1, CON_LEFT, title);
	for(i = 0; i < lines; i++)
		con_text(m, CON_LEFT + REQ_PAD, CON_TOP + REQ_PAD + 8 * i, line[i], ConBlack);
	y = CON_TOP + 2 * REQ_PAD + 8 * lines;
	x = CON_LEFT + REQ_PAD;
	gap = count > 1 ? (right + 1 - REQ_PAD - x - total) / (count - 1) : 0;
	c->req_gadget_y = y;
	for(i = 0; i < count; i++)
	{
		c->req_gadget_x[i] = (int16_t)(count == 1 ? (w - width[0]) / 2 : i == count - 1 ? right + 1 - REQ_PAD - width[i] : x);
		c->req_gadget_w[i] = (int16_t)width[i];
		con_button(m, c->req_gadget_x[i], y, width[i], gadget[i]);
		x += width[i] + gap;
	}
	c->req_mx = (uint8_t)m->paula.mouse_x;
	c->req_my = (uint8_t)m->paula.mouse_y;
	c->req_lmb = m->paula.lmb;
	c->req_pressed = -1;
	con_pointer(m, !m->os_cursor);
	amiga_logf(m, "intuition: EasyRequestArgs \"%s\": %s]", title, log);
	return 1;
}

/* The parked EasyRequestArgs (internal entry): 1 while it waits; then the backdrop comes back,
 * the View the screen was moved in front of is shown again, and D0 is the answer. */
int hle_con_request_park(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	if(!c->req_done)
		return 1;
	con_request_backdrop(m, 0);
	con_pointer(m, 0);
	hle_free(m, c->req_save, con_request_size(c));
	c->req_save = 0;
	if(c->req_front)
		hle_gfx_load_view(m, c->req_behind);
	if(c->req_answer < 0)
		hle_w32(m, c->req_idcmp, IDCMP_DISKINSERTED);
	amiga_logf(m, "intuition: EasyRequestArgs answered %d", (int)c->req_answer);
	m->cpu.d[0] = (uint32_t)c->req_answer;
	m->cpu.pc = cpu_pop32(m);
	return 0;
}

/* A disk went into a drive: IDCMP_DISKINSERTED, which ends a requester whose flags ask for it. */
void hle_con_disk_inserted(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	if(c->req_save && !c->req_done && c->req_idcmp && (hle_r32(m, c->req_idcmp) & IDCMP_DISKINSERTED))
	{
		c->req_answer = -1;
		c->req_done = 1;
	}
}

/* At each vertical blank while a requester is up: the pointer follows the mouse counters, one
 * screen pixel a count as Intuition's input handler here moves it (unscaled), held inside the
 * screen, and is drawn unless the OS cursor shows over the game (Amiga.os_cursor); a gadget
 * answers when the left button goes up over the gadget it went down on, as a RELVERIFY gadget
 * does (RKM Libraries, "Intuition Gadgets"). */
void hle_con_vblank(struct Amiga *m)
{
	struct HleCon *c = &m->hle.con;
	int dx, dy, at;
	if(!c->req_save || c->req_done)
		return;
	dx = (int8_t)(uint8_t)((uint8_t)m->paula.mouse_x - c->req_mx);
	dy = (int8_t)(uint8_t)((uint8_t)m->paula.mouse_y - c->req_my);
	c->req_mx = (uint8_t)m->paula.mouse_x;
	c->req_my = (uint8_t)m->paula.mouse_y;
	c->ptr_x += dx;
	c->ptr_y += dy;
	c->ptr_x = c->ptr_x < 0 ? 0 : c->ptr_x >= CON_W ? CON_W - 1 : c->ptr_x;
	c->ptr_y = c->ptr_y < 0 ? 0 : c->ptr_y >= CON_H ? CON_H - 1 : c->ptr_y;
	con_pointer(m, !m->os_cursor);
	at = con_gadget_at(c);
	if(m->paula.lmb && !c->req_lmb)
		c->req_pressed = (int8_t)at;
	else if(!m->paula.lmb && c->req_lmb)
	{
		if(at >= 0 && at == c->req_pressed)
			con_request_answer(c, at);
		c->req_pressed = -1;
	}
	c->req_lmb = m->paula.lmb;
}

int hle_con_pointer(const struct Amiga *m, int *x, int *y)
{
	const struct HleCon *c = &m->hle.con;
	if(!c->req_save || c->req_done)
		return 0;
	*x = CON_DIW_X + c->ptr_x / 2;
	*y = CON_DIW_Y + c->ptr_y;
	return 1;
}

#endif
