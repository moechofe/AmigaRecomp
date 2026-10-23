#include "hle.h"
#include "cpu_ops.h"

/* intuition.library functions, by negative LVO, from NDK 3.2 FD/intuition_lib.fd (bias 30,
 * 6 bytes a slot), each from its autodoc. Structures from NDK 3.2 intuition/screens.i and
 * intuition/intuition.i. Screens are real Screen structures whose ViewPorts the graphics HLE
 * turns into the copper list; nothing is drawn with Kickstart's ROM font, which this runtime does
 * not have, so a title bar, border or gadget that would show it is fatal. Returns 0 to return to
 * the caller, -1 when the LVO is not implemented. */

#define D0 m->cpu.d[0]
#define D1 m->cpu.d[1]
#define A0 m->cpu.a[0]
#define A1 m->cpu.a[1]

#define IB_VIEWLORD 0x22
#define IB_ACTIVEWINDOW 0x34
#define IB_ACTIVESCREEN 0x38
#define IB_FIRSTSCREEN 0x3C
#define IB_MOUSEY 0x44
#define IB_MOUSEX 0x46
#define IB_SIZE 0x50

#define NS_LEFTEDGE 0x00
#define NS_TOPEDGE 0x02
#define NS_WIDTH 0x04
#define NS_HEIGHT 0x06
#define NS_DEPTH 0x08
#define NS_DETAILPEN 0x0A
#define NS_BLOCKPEN 0x0B
#define NS_VIEWMODES 0x0C
#define NS_TYPE 0x0E
#define NS_FONT 0x10
#define NS_DEFAULTTITLE 0x14
#define NS_GADGETS 0x18
#define NS_CUSTOMBITMAP 0x1C

#define SC_NEXTSCREEN 0x00
#define SC_FIRSTWINDOW 0x04
#define SC_LEFTEDGE 0x08
#define SC_TOPEDGE 0x0A
#define SC_WIDTH 0x0C
#define SC_HEIGHT 0x0E
#define SC_FLAGS 0x14
#define SC_TITLE 0x16
#define SC_DEFAULTTITLE 0x1A
#define SC_FONT 0x28
#define SC_VIEWPORT 0x2C
#define SC_RASTPORT 0x54
#define SC_BITMAP 0xB8
#define SC_LAYERINFO 0xE0
#define SC_DETAILPEN 0x14A
#define SC_BLOCKPEN 0x14B
#define SC_SIZEOF 0x15A

#define NW_LEFTEDGE 0x00
#define NW_TOPEDGE 0x02
#define NW_WIDTH 0x04
#define NW_HEIGHT 0x06
#define NW_DETAILPEN 0x08
#define NW_BLOCKPEN 0x09
#define NW_IDCMPFLAGS 0x0A
#define NW_FLAGS 0x0E
#define NW_FIRSTGADGET 0x12
#define NW_TITLE 0x1A
#define NW_SCREEN 0x1E
#define NW_BITMAP 0x22
#define NW_MINWIDTH 0x26
#define NW_TYPE 0x2E

#define WD_NEXTWINDOW 0x00
#define WD_LEFTEDGE 0x04
#define WD_TOPEDGE 0x06
#define WD_WIDTH 0x08
#define WD_HEIGHT 0x0A
#define WD_MOUSEY 0x0C
#define WD_MOUSEX 0x0E
#define WD_MINWIDTH 0x10
#define WD_FLAGS 0x18
#define WD_TITLE 0x20
#define WD_WSCREEN 0x2E
#define WD_RPORT 0x32
#define WD_POINTER 0x4A
#define WD_PTRHEIGHT 0x4E
#define WD_PTRWIDTH 0x4F
#define WD_XOFFSET 0x50
#define WD_YOFFSET 0x51
#define WD_IDCMPFLAGS 0x52
#define WD_USERPORT 0x56
#define WD_WINDOWPORT 0x5A
#define WD_DETAILPEN 0x62
#define WD_BLOCKPEN 0x63
#define WD_GZZWIDTH 0x70
#define WD_GZZHEIGHT 0x72
#define WD_WLAYER 0x7C
#define WD_SIZE 0x88

#define CUSTOMBITMAP 0x40
#define SCREENTYPE 0x0F
#define SHOWTITLE 0x10

#define WFLG_SYSGADGETS 0x000F
#define WFLG_GIMMEZEROZERO 0x0400
#define WFLG_BORDERLESS 0x0800
#define WFLG_ACTIVATE 0x1000
#define WFLG_WINDOWACTIVE 0x2000

#define PA_IGNORE 2
/* struct Remember (intuition/intuition.h): NextRemember, RememberSize, Memory. */
#define REMEMBER_SIZE 12

static uint32_t int_alloc(struct Amiga *m, uint32_t size, uint32_t req, const char *what)
{
	uint32_t p = hle_alloc(m, size, req | MEMF_CLEAR, 0);
	if(!p)
		amiga_fatal(m, "intuition: no memory for %s", what);
	return p;
}

/* A string the caller passed that Intuition would draw in the default font: only NULL and "" are
 * drawable without Kickstart's ROM font. */
static int int_text_needs_font(struct Amiga *m, uint32_t s)
{
	return s && hle_r8(m, s) != 0;
}

/* --- the display -------------------------------------------------------------------------- */

/* Rebuild the View from the screens, front first. A screen in front covers everything from its
 * TopEdge down, so each ViewPort further back shows from its own TopEdge to three lines above the
 * highest screen in front of it, as Intuition 1.3 placed the Workbench screen behind a screen at
 * TopEdge 20 (docs/runtime.md, "Library calls"). The visible screens therefore run from the
 * bottom up, and each goes to the head of the View's list. */
static void int_rethink(struct Amiga *m)
{
	uint32_t view = HLE_INT_BASE + IB_VIEWLORD, s;
	int top_in_front = 0x7FFF;
	hle_w32(m, view + V_VIEWPORT, 0);
	for(s = hle_r32(m, HLE_INT_BASE + IB_FIRSTSCREEN); s; s = hle_r32(m, s + SC_NEXTSCREEN))
	{
		uint32_t vp = s + SC_VIEWPORT;
		int top = (int16_t)hle_r16(m, s + SC_TOPEDGE), h = (int16_t)hle_r16(m, s + SC_HEIGHT);
		if(top + h > top_in_front - 3)
			h = top_in_front - 3 - top;
		if(h <= 0)
			continue;
		hle_w16(m, vp + VP_DHEIGHT, (uint16_t)h);
		hle_w16(m, vp + VP_DYOFFSET, (uint16_t)top);
		hle_w16(m, vp + VP_DXOFFSET, hle_r16(m, s + SC_LEFTEDGE));
		hle_w32(m, vp + VP_NEXT, hle_r32(m, view + V_VIEWPORT));
		hle_w32(m, view + V_VIEWPORT, vp);
		top_in_front = top;
	}
	for(s = hle_r32(m, view + V_VIEWPORT); s; s = hle_r32(m, s + VP_NEXT))
	{
		hle_gfx_make_vport(m, view, s);
		if(m->fatal)
			return;
	}
	hle_gfx_mrgcop(m, view);
	hle_gfx_load_view(m, view);
}

static void int_unlink(struct Amiga *m, uint32_t list, uint32_t next_off, uint32_t node)
{
	uint32_t at = list;
	while(hle_r32(m, at))
	{
		if(hle_r32(m, at) == node)
		{
			hle_w32(m, at, hle_r32(m, node + next_off));
			return;
		}
		at = hle_r32(m, at) + next_off;
	}
}

/* --- screens ------------------------------------------------------------------------------ */

static uint32_t int_open_screen(struct Amiga *m, uint32_t ns)
{
	uint16_t type = hle_r16(m, ns + NS_TYPE), modes = hle_r16(m, ns + NS_VIEWMODES);
	int width = (int16_t)hle_r16(m, ns + NS_WIDTH), height = (int16_t)hle_r16(m, ns + NS_HEIGHT);
	int depth = (int16_t)hle_r16(m, ns + NS_DEPTH), i;
	uint32_t s, bm, ri, cm, title = hle_r32(m, ns + NS_DEFAULTTITLE);
	if((type & SCREENTYPE) != 0x0F)
	{
		amiga_fatal(m, "OpenScreen: screen type 0x%04X is not implemented (only CUSTOMSCREEN)", type);
		return 0;
	}
	if(int_text_needs_font(m, title))
	{
		amiga_fatal(m, "OpenScreen: a screen title needs Kickstart's ROM font (topaz), which this runtime does not have");
		return 0;
	}
	if(hle_r32(m, ns + NS_GADGETS))
	{
		amiga_fatal(m, "OpenScreen: screen gadgets are not implemented");
		return 0;
	}
	if(width < 0 || height < 0 || depth < 1 || depth > 6)
	{
		amiga_fatal(m, "OpenScreen: a %d x %d screen of depth %d is not implemented", width, height, depth);
		return 0;
	}
	amiga_logf(m, "intuition: OpenScreen %d x %d x %d at %d,%d, type 0x%04X, modes 0x%04X", width, height, depth,
		(int16_t)hle_r16(m, ns + NS_LEFTEDGE), (int16_t)hle_r16(m, ns + NS_TOPEDGE), type, modes);
	s = int_alloc(m, SC_SIZEOF, MEMF_PUBLIC, "a Screen");
	ri = int_alloc(m, 0x0C, MEMF_PUBLIC, "a RasInfo");
	cm = hle_gfx_color_map(m, 32);
	if(!s || !ri || !cm)
		return 0;
	hle_w16(m, s + SC_LEFTEDGE, hle_r16(m, ns + NS_LEFTEDGE));
	hle_w16(m, s + SC_TOPEDGE, hle_r16(m, ns + NS_TOPEDGE));
	hle_w16(m, s + SC_WIDTH, (uint16_t)width);
	hle_w16(m, s + SC_HEIGHT, (uint16_t)height);
	hle_w16(m, s + SC_FLAGS, (uint16_t)(type | SHOWTITLE));
	hle_w32(m, s + SC_TITLE, title);
	hle_w32(m, s + SC_DEFAULTTITLE, title);
	hle_w32(m, s + SC_FONT, hle_r32(m, ns + NS_FONT));
	hle_w8(m, s + SC_DETAILPEN, hle_r8(m, ns + NS_DETAILPEN));
	hle_w8(m, s + SC_BLOCKPEN, hle_r8(m, ns + NS_BLOCKPEN));

	bm = s + SC_BITMAP;
	if(type & CUSTOMBITMAP)
	{
		uint32_t from = hle_r32(m, ns + NS_CUSTOMBITMAP);
		for(i = 0; i < 0x28; i += 2)
			hle_w16(m, bm + (uint32_t)i, hle_r16(m, from + (uint32_t)i));
	}
	else
	{
		hle_w16(m, bm + BM_BYTESPERROW, (uint16_t)(((width + 15) >> 4) << 1));
		hle_w16(m, bm + BM_ROWS, (uint16_t)height);
		hle_w8(m, bm + BM_DEPTH, (uint8_t)depth);
		for(i = 0; i < depth; i++)
		{
			uint32_t plane = hle_gfx_alloc_raster(m, (uint32_t)width, (uint32_t)height);
			if(!plane)
			{
				amiga_fatal(m, "OpenScreen: no chip memory for the bitplanes");
				return 0;
			}
			hle_w32(m, bm + BM_PLANES + 4 * (uint32_t)i, plane);
		}
	}

	hle_gfx_init_vport(m, s + SC_VIEWPORT);
	hle_w16(m, s + SC_VIEWPORT + VP_DWIDTH, (uint16_t)width);
	hle_w16(m, s + SC_VIEWPORT + VP_DHEIGHT, (uint16_t)height);
	hle_w16(m, s + SC_VIEWPORT + VP_MODES, modes);
	hle_w32(m, s + SC_VIEWPORT + VP_RASINFO, ri);
	hle_w32(m, s + SC_VIEWPORT + VP_COLORMAP, cm);
	hle_w32(m, ri + RI_BITMAP, bm);
	hle_gfx_init_rastport(m, s + SC_RASTPORT);
	hle_w32(m, s + SC_RASTPORT + RP_BITMAP, bm);

	hle_w32(m, s + SC_NEXTSCREEN, hle_r32(m, HLE_INT_BASE + IB_FIRSTSCREEN));
	hle_w32(m, HLE_INT_BASE + IB_FIRSTSCREEN, s);
	hle_w32(m, HLE_INT_BASE + IB_ACTIVESCREEN, s);
	if(!hle_r32(m, HLE_INT_INPUT_NODE + LN_SUCC))
	{
		hle_enqueue(m, HLE_INPUT_HANDLERS, HLE_INT_INPUT_NODE);
		m->hle.input.open = 1;
		m->hle.input.mouse_x = (uint8_t)m->paula.mouse_x;
		m->hle.input.mouse_y = (uint8_t)m->paula.mouse_y;
		m->hle.input.lmb = m->paula.lmb;
		m->hle.input.rmb = m->paula.rmb;
	}
	int_rethink(m);
	return s;
}

static void int_close_screen(struct Amiga *m, uint32_t s)
{
	uint32_t bm = s + SC_BITMAP, vp = s + SC_VIEWPORT;
	int i;
	if(hle_r32(m, s + SC_FIRSTWINDOW))
	{
		amiga_fatal(m, "CloseScreen with windows still open");
		return;
	}
	int_unlink(m, HLE_INT_BASE + IB_FIRSTSCREEN, SC_NEXTSCREEN, s);
	if(hle_r32(m, HLE_INT_BASE + IB_ACTIVESCREEN) == s)
		hle_w32(m, HLE_INT_BASE + IB_ACTIVESCREEN, hle_r32(m, HLE_INT_BASE + IB_FIRSTSCREEN));
	int_rethink(m);
	hle_gfx_free_vport_coplists(m, vp);
	hle_gfx_free_color_map(m, hle_r32(m, vp + VP_COLORMAP));
	hle_free(m, hle_r32(m, vp + VP_RASINFO), 0x0C);
	if(!(hle_r16(m, s + SC_FLAGS) & CUSTOMBITMAP))
		for(i = 0; i < hle_r8(m, bm + BM_DEPTH); i++)
			hle_gfx_free_raster(m, hle_r32(m, bm + BM_PLANES + 4 * (uint32_t)i), hle_r16(m, s + SC_WIDTH), hle_r16(m, s + SC_HEIGHT));
	hle_free(m, s, SC_SIZEOF);
}

/* --- windows ------------------------------------------------------------------------------ */

/* The IDCMP ports: UserPort signals the task on a new signal bit; WindowPort takes the replies
 * and never signals. */
static int int_set_idcmp(struct Amiga *m, uint32_t w, uint32_t flags)
{
	uint32_t port = hle_r32(m, w + WD_USERPORT);
	hle_w32(m, w + WD_IDCMPFLAGS, flags);
	if(flags && !port)
	{
		int32_t bit;
		port = int_alloc(m, MP_SIZE, MEMF_PUBLIC, "an IDCMP port");
		if(!port)
			return -1;
		bit = hle_exec_alloc_signal(m, -1);
		if(bit < 0)
		{
			amiga_fatal(m, "intuition: no free signal for an IDCMP port");
			return -1;
		}
		hle_w8(m, port + LN_TYPE, NT_MSGPORT);
		hle_w8(m, port + MP_SIGBIT, (uint8_t)bit);
		hle_w32(m, port + MP_SIGTASK, hle_r32(m, HLE_EXEC_BASE + EB_THISTASK));
		hle_new_list(m, port + MP_MSGLIST, NT_MESSAGE);
		hle_w32(m, w + WD_USERPORT, port);
	}
	else if(!flags && port)
	{
		if(hle_r32(m, hle_r32(m, port + MP_MSGLIST + LH_HEAD) + LN_SUCC))
		{
			amiga_fatal(m, "ModifyIDCMP(0) with messages still queued");
			return -1;
		}
		hle_exec_free_signal(m, hle_r8(m, port + MP_SIGBIT));
		hle_free(m, port, MP_SIZE);
		hle_w32(m, w + WD_USERPORT, 0);
	}
	return 0;
}

static uint32_t int_open_window(struct Amiga *m, uint32_t nw)
{
	uint32_t flags = hle_r32(m, nw + NW_FLAGS), s = hle_r32(m, nw + NW_SCREEN), w, rp, layer, wp;
	int left = (int16_t)hle_r16(m, nw + NW_LEFTEDGE), top = (int16_t)hle_r16(m, nw + NW_TOPEDGE);
	int width = (int16_t)hle_r16(m, nw + NW_WIDTH), height = (int16_t)hle_r16(m, nw + NW_HEIGHT);
	if(hle_r16(m, nw + NW_TYPE) != 0x0F || !s)
	{
		amiga_fatal(m, "OpenWindow: only windows on a custom screen are implemented");
		return 0;
	}
	if(!(flags & WFLG_BORDERLESS) || (flags & (WFLG_SYSGADGETS | WFLG_GIMMEZEROZERO)) || hle_r32(m, nw + NW_FIRSTGADGET) ||
		int_text_needs_font(m, hle_r32(m, nw + NW_TITLE)))
	{
		amiga_fatal(m, "OpenWindow: borders, a title or gadgets (flags 0x%08X) are not implemented; they need Kickstart's ROM font (topaz)", flags);
		return 0;
	}
	if((flags & 0xC0) == 0x80)
	{
		amiga_fatal(m, "OpenWindow: a SUPER_BITMAP window is not implemented");
		return 0;
	}
	if(left < 0 || top < 0 || left + width > (int16_t)hle_r16(m, s + SC_WIDTH) || top + height > (int16_t)hle_r16(m, s + SC_HEIGHT))
	{
		amiga_fatal(m, "OpenWindow: a %d x %d window at %d,%d does not fit its screen", width, height, left, top);
		return 0;
	}
	if(hle_r32(m, s + SC_FIRSTWINDOW))
	{
		amiga_fatal(m, "OpenWindow: a second window on one screen is not implemented");
		return 0;
	}
	w = int_alloc(m, WD_SIZE, MEMF_PUBLIC, "a Window");
	rp = int_alloc(m, RP_SIZE, MEMF_PUBLIC, "a RastPort");
	layer = int_alloc(m, LR_SIZE, MEMF_PUBLIC, "a Layer");
	wp = int_alloc(m, MP_SIZE, MEMF_PUBLIC, "a WindowPort");
	if(!w || !rp || !layer || !wp)
		return 0;
	hle_w16(m, w + WD_LEFTEDGE, (uint16_t)left);
	hle_w16(m, w + WD_TOPEDGE, (uint16_t)top);
	hle_w16(m, w + WD_WIDTH, (uint16_t)width);
	hle_w16(m, w + WD_HEIGHT, (uint16_t)height);
	hle_w16(m, w + WD_GZZWIDTH, (uint16_t)width);
	hle_w16(m, w + WD_GZZHEIGHT, (uint16_t)height);
	for(int i = 0; i < 8; i += 2)
		hle_w16(m, w + WD_MINWIDTH + (uint32_t)i, hle_r16(m, nw + NW_MINWIDTH + (uint32_t)i));
	hle_w32(m, w + WD_FLAGS, flags);
	hle_w32(m, w + WD_TITLE, hle_r32(m, nw + NW_TITLE));
	hle_w32(m, w + WD_WSCREEN, s);
	hle_w32(m, w + WD_RPORT, rp);
	hle_w32(m, w + WD_WLAYER, layer);
	hle_w8(m, w + WD_DETAILPEN, hle_r8(m, nw + NW_DETAILPEN));
	hle_w8(m, w + WD_BLOCKPEN, hle_r8(m, nw + NW_BLOCKPEN));
	hle_w8(m, wp + LN_TYPE, NT_MSGPORT);
	hle_w8(m, wp + MP_FLAGS, PA_IGNORE);
	hle_new_list(m, wp + MP_MSGLIST, NT_MESSAGE);
	hle_w32(m, w + WD_WINDOWPORT, wp);

	hle_gfx_init_rastport(m, rp);
	hle_w32(m, rp + RP_BITMAP, s + SC_BITMAP);
	hle_w32(m, rp + RP_LAYER, layer);
	hle_w32(m, layer + LR_RP, rp);
	hle_w16(m, layer + LR_MINX, (uint16_t)left);
	hle_w16(m, layer + LR_MINY, (uint16_t)top);
	hle_w16(m, layer + LR_MAXX, (uint16_t)(left + width - 1));
	hle_w16(m, layer + LR_MAXY, (uint16_t)(top + height - 1));
	hle_w32(m, layer + LR_LAYERINFO, s + SC_LAYERINFO);
	hle_w32(m, layer + LR_WINDOW, w);

	hle_w32(m, s + SC_FIRSTWINDOW, w);
	if(int_set_idcmp(m, w, hle_r32(m, nw + NW_IDCMPFLAGS)) < 0)
		return 0;
	if(flags & WFLG_ACTIVATE)
	{
		hle_w32(m, w + WD_FLAGS, flags | WFLG_WINDOWACTIVE);
		hle_w32(m, HLE_INT_BASE + IB_ACTIVEWINDOW, w);
	}
	return w;
}

/* Every IntuiMessage is freed here, so the unreplied tick is never a freed block. */
static void int_free_msg(struct Amiga *m, uint32_t msg)
{
	if(msg == m->hle.int_tick)
		m->hle.int_tick = 0;
	hle_free(m, msg, IM_SIZE);
}

static void int_free_replies(struct Amiga *m, uint32_t w)
{
	uint32_t msg;
	while((msg = hle_rem_head(m, hle_r32(m, w + WD_WINDOWPORT) + MP_MSGLIST)) != 0)
		int_free_msg(m, msg);
}

static void int_close_window(struct Amiga *m, uint32_t w)
{
	uint32_t s = hle_r32(m, w + WD_WSCREEN), port = hle_r32(m, w + WD_USERPORT), msg;
	if(port)
	{
		while((msg = hle_rem_head(m, port + MP_MSGLIST)) != 0)
			int_free_msg(m, msg);
		hle_w32(m, w + WD_IDCMPFLAGS, 0);
		int_set_idcmp(m, w, 0);
	}
	int_unlink(m, s + SC_FIRSTWINDOW, WD_NEXTWINDOW, w);
	if(hle_r32(m, HLE_INT_BASE + IB_ACTIVEWINDOW) == w)
		hle_w32(m, HLE_INT_BASE + IB_ACTIVEWINDOW, 0);
	hle_free(m, hle_r32(m, w + WD_RPORT), RP_SIZE);
	hle_free(m, hle_r32(m, w + WD_WLAYER), LR_SIZE);
	hle_free(m, hle_r32(m, w + WD_WINDOWPORT), MP_SIZE);
	hle_free(m, w, WD_SIZE);
}

/* --- input -------------------------------------------------------------------------------- */

#define IE_NEXT 0x00
#define IE_CLASS 0x04
#define IE_CODE 0x06
#define IE_QUALIFIER 0x08
#define IE_X 0x0A
#define IE_Y 0x0C
#define IECLASS_RAWKEY 0x01
#define IECLASS_RAWMOUSE 0x02
#define IECODE_NOBUTTON 0xFF
#define IDCMP_MOUSEBUTTONS 0x00000008u
#define IDCMP_MOUSEMOVE 0x00000010u
#define IDCMP_RAWKEY 0x00000400u
#define IDCMP_INTUITICKS 0x00400000u
#define INT_TICK_FRAMES 5
#define IM_CLASS 0x14
#define IM_CODE 0x18
#define IM_QUALIFIER 0x1A
#define IM_IADDRESS 0x1C
#define IM_MOUSEX 0x20
#define IM_MOUSEY 0x22
#define IM_IDCMPWINDOW 0x2C
#define WFLG_REPORTMOUSE 0x0200

/* The message, or 0 after a fatal error. */
static uint32_t int_message(struct Amiga *m, uint32_t w, uint32_t cls, uint16_t code, uint16_t qual)
{
	uint32_t msg = hle_alloc(m, IM_SIZE, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!msg)
	{
		amiga_fatal(m, "intuition: no memory for an IntuiMessage");
		return 0;
	}
	hle_w8(m, msg + LN_TYPE, NT_MESSAGE);
	hle_w32(m, msg + MN_REPLYPORT, hle_r32(m, w + WD_WINDOWPORT));
	hle_w16(m, msg + MN_LENGTH, IM_SIZE);
	hle_w32(m, msg + IM_CLASS, cls);
	hle_w16(m, msg + IM_CODE, code);
	hle_w16(m, msg + IM_QUALIFIER, qual);
	hle_w16(m, msg + IM_MOUSEX, hle_r16(m, w + WD_MOUSEX));
	hle_w16(m, msg + IM_MOUSEY, hle_r16(m, w + WD_MOUSEY));
	hle_w32(m, msg + IM_IDCMPWINDOW, w);
	hle_put_msg(m, hle_r32(m, w + WD_USERPORT), msg);
	return msg;
}

/* Intuition's input handler (priority 50 in input.device's list; RKM Devices, "Input Device"):
 * the replies of earlier IntuiMessages are freed, the pointer position follows the mouse, and
 * each raw key and mouse event the active window's IDCMPFlags ask for becomes an IntuiMessage
 * on its UserPort. The events are passed on. */
void hle_int_input(struct Amiga *m)
{
	uint32_t w = hle_r32(m, HLE_INT_BASE + IB_ACTIVEWINDOW), ev;
	m->cpu.d[0] = m->cpu.a[0];
	if(!w)
		return;
	int_free_replies(m, w);
	for(ev = m->cpu.a[0]; ev && !m->fatal; ev = hle_r32(m, ev + IE_NEXT))
	{
		uint8_t cls = hle_r8(m, ev + IE_CLASS);
		uint16_t code = hle_r16(m, ev + IE_CODE), qual = hle_r16(m, ev + IE_QUALIFIER);
		uint32_t idcmp = hle_r32(m, w + WD_IDCMPFLAGS);
		if(cls == IECLASS_RAWMOUSE)
		{
			uint32_t s = hle_r32(m, w + WD_WSCREEN);
			int x = (int16_t)hle_r16(m, s + 0x12) + (int16_t)hle_r16(m, ev + IE_X);
			int y = (int16_t)hle_r16(m, s + 0x10) + (int16_t)hle_r16(m, ev + IE_Y);
			x = x < 0 ? 0 : x >= (int16_t)hle_r16(m, s + SC_WIDTH) ? (int16_t)hle_r16(m, s + SC_WIDTH) - 1 : x;
			y = y < 0 ? 0 : y >= (int16_t)hle_r16(m, s + SC_HEIGHT) ? (int16_t)hle_r16(m, s + SC_HEIGHT) - 1 : y;
			hle_w16(m, s + 0x12, (uint16_t)x);
			hle_w16(m, s + 0x10, (uint16_t)y);
			hle_w16(m, w + WD_MOUSEX, (uint16_t)(x - (int16_t)hle_r16(m, w + WD_LEFTEDGE)));
			hle_w16(m, w + WD_MOUSEY, (uint16_t)(y - (int16_t)hle_r16(m, w + WD_TOPEDGE)));
			if(code != IECODE_NOBUTTON && (idcmp & IDCMP_MOUSEBUTTONS))
				int_message(m, w, IDCMP_MOUSEBUTTONS, code, qual);
			else if(code == IECODE_NOBUTTON && (idcmp & IDCMP_MOUSEMOVE) && (hle_r32(m, w + WD_FLAGS) & WFLG_REPORTMOUSE))
				int_message(m, w, IDCMP_MOUSEMOVE, code, qual);
		}
		else if(cls == IECLASS_RAWKEY && (idcmp & IDCMP_RAWKEY))
			int_message(m, w, IDCMP_RAWKEY, code, qual);
	}
}

/* IDCMP_INTUITICKS: Intuition ticks "roughly ten times a second", sends the message only to the
 * active window, and sends none while the last one is unreplied (RKM Libraries, "Event Message
 * Classes and Flags", "Other Flags"). Ten a second is every fifth PAL frame. Replies are freed
 * first, as the input handler does, so ticks with no input in between do not pile up. */
void hle_int_vblank(struct Amiga *m)
{
	uint32_t w = hle_r32(m, HLE_INT_BASE + IB_ACTIVEWINDOW);
	if(m->frame_count % INT_TICK_FRAMES || !w || !(hle_r32(m, w + WD_IDCMPFLAGS) & IDCMP_INTUITICKS))
		return;
	int_free_replies(m, w);
	if(m->hle.int_tick && hle_r8(m, m->hle.int_tick + LN_TYPE) != NT_REPLYMSG)
		return;
	m->hle.int_tick = int_message(m, w, IDCMP_INTUITICKS, 0, m->hle.input.qualifier);
}

/* --- the library -------------------------------------------------------------------------- */

void hle_int_build(struct Amiga *m)
{
	hle_w16(m, HLE_INT_BASE + LIB_POSSIZE, IB_SIZE);
	hle_gfx_init_view(m, HLE_INT_BASE + IB_VIEWLORD);
	hle_w8(m, HLE_INT_INPUT_NODE + LN_TYPE, NT_INTERRUPT);
	hle_w8(m, HLE_INT_INPUT_NODE + LN_PRI, 50);
	hle_w32(m, HLE_INT_INPUT_NODE + IS_CODE, HLE_INT_HANDLER);
}

int hle_int_call(struct Amiga *m, uint32_t lvo)
{
	switch(lvo)
	{
	case 60: /* ClearPointer(a0 window): Intuition's own pointer again */
		hle_w32(m, A0 + WD_POINTER, 0);
		return 0;
	case 66: /* CloseScreen(a0 screen) */
		int_close_screen(m, A0);
		D0 = 1;
		return 0;
	case 72: /* CloseWindow(a0 window) */
		int_close_window(m, A0);
		return 0;
	case 78: /* CloseWorkBench: TRUE once closed; FALSE while the Initial CLI's console window is
	          * on it or when it was not open (intuition autodoc) */
		D0 = m->hle.dos.workbench && m->hle.dos.cli_ended ? 1 : 0;
		if(D0)
			m->hle.dos.workbench = 0;
		return 0;
	case 84: /* CurrentTime(a0 &seconds, a1 &micros): the system clock (hle_dos_time) */
	{
		uint32_t secs, micros;
		hle_dos_time(m, &secs, &micros);
		hle_w32(m, A0, secs);
		hle_w32(m, A1, micros);
		return 0;
	}
	case 150: /* ModifyIDCMP(a0 window, d0 flags) */
		int_set_idcmp(m, A0, D0);
		return 0;
	case 198: /* OpenScreen(a0 newScreen) */
		D0 = int_open_screen(m, A0);
		return 0;
	case 204: /* OpenWindow(a0 newWindow) */
		D0 = int_open_window(m, A0);
		return 0;
	case 210: /* OpenWorkBench */
		amiga_fatal(m, "OpenWorkBench: the Workbench screen needs Kickstart's ROM font (topaz), which this runtime does not have");
		return 0;
	case 270: /* SetPointer(a0 window, a1 pointer, d0 height, d1 width, d2 xOffset, d3 yOffset) */
		hle_w32(m, A0 + WD_POINTER, A1);
		hle_w8(m, A0 + WD_PTRHEIGHT, (uint8_t)D0);
		hle_w8(m, A0 + WD_PTRWIDTH, (uint8_t)D1);
		hle_w8(m, A0 + WD_XOFFSET, (uint8_t)m->cpu.d[2]);
		hle_w8(m, A0 + WD_YOFFSET, (uint8_t)m->cpu.d[3]);
		return 0;
	case 282: /* ShowTitle(a0 screen, d0 showIt): the bar is never drawn here */
		hle_w16(m, A0 + SC_FLAGS, (uint16_t)(D0 ? hle_r16(m, A0 + SC_FLAGS) | SHOWTITLE : hle_r16(m, A0 + SC_FLAGS) & ~SHOWTITLE));
		return 0;
	case 294: /* ViewAddress */
		D0 = HLE_INT_BASE + IB_VIEWLORD;
		return 0;
	case 378: /* MakeScreen(a0 screen) */
		hle_gfx_make_vport(m, HLE_INT_BASE + IB_VIEWLORD, A0 + SC_VIEWPORT);
		return 0;
	case 384: /* RemakeDisplay */
	case 390: /* RethinkDisplay */
		int_rethink(m);
		return 0;
	case 396: /* AllocRemember(a0 rememberKey, d0 size, d1 flags): AllocMem's block, linked at
	           * the head of the key's list in a Remember (NextRemember, RememberSize, Memory);
	           * NULL if either allocation fails */
	{
		uint32_t node = hle_alloc(m, REMEMBER_SIZE, MEMF_PUBLIC, 0), mem = node ? hle_alloc(m, D0, D1, 0) : 0;
		if(node && !mem)
			hle_free(m, node, REMEMBER_SIZE);
		if(mem)
		{
			hle_w32(m, node, hle_r32(m, A0));
			hle_w32(m, node + 4, D0);
			hle_w32(m, node + 8, mem);
			hle_w32(m, A0, node);
		}
		D0 = mem;
		return 0;
	}
	case 408: /* FreeRemember(a0 rememberKey, d0 reallyForget): every Remember freed, and with
	           * reallyForget its memory too; the key becomes NULL */
	{
		uint32_t node = hle_r32(m, A0);
		while(node)
		{
			uint32_t next = hle_r32(m, node);
			if(D0)
				hle_free(m, hle_r32(m, node + 8), hle_r32(m, node + 4));
			hle_free(m, node, REMEMBER_SIZE);
			node = next;
		}
		hle_w32(m, A0, 0);
		return 0;
	}
	case 414: /* LockIBase(d0 dontknow): a value for UnlockIBase; one task runs, so nothing waits on it */
		D0 = 0;
		return 0;
	case 420: /* UnlockIBase(a0 ibLock) */
		return 0;
#if AMIGA_A1200
	case 588: /* EasyRequestArgs(a0 window, a1 easyStruct, a2 idcmpPtr, a3 args): drawn on the
	           * Workbench screen and parked until answered (hle_con_request) */
		if(!hle_con_request(m, A0, A1, m->cpu.a[2], m->cpu.a[3]))
			return 0;
		m->cpu.pc = HLE_REQ_PARK;
		return 1;
#endif
	default:
		return -1;
	}
}
