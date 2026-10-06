#include "hle.h"

/* input.device, fed the way keyboard.device and gameport.device feed it: a key read off
 * CIA-A's serial port by the PORTS server, and the mouse counters and buttons sampled once a
 * vertical blank. The event chain goes to every handler added with IND_ADDHANDLER, highest
 * priority first, as guest calls at the end of the interrupt that produced the events.
 * Commands per NDK 3.2 devices/input.i, events per devices/inputevent.i. */

#define IE_NEXT 0x00
#define IE_CLASS 0x04
#define IE_CODE 0x06
#define IE_QUALIFIER 0x08
#define IE_X 0x0A
#define IE_Y 0x0C
#define IE_SIZE 0x16
#define IECLASS_RAWKEY 0x01
#define IECLASS_RAWMOUSE 0x02
#define IECODE_LBUTTON 0x68
#define IECODE_RBUTTON 0x69
#define IECODE_UP_PREFIX 0x80
#define IECODE_NOBUTTON 0xFF
#define IEQUALIFIER_LEFTBUTTON 0x4000
#define IEQUALIFIER_RBUTTON 0x2000
#define IEQUALIFIER_RELATIVEMOUSE 0x8000

static void hle_input_event(struct Amiga *m, uint8_t cls, uint16_t code, int16_t x, int16_t y)
{
	struct HleInput *in = &m->hle.input;
	uint32_t ev;
	uint16_t qual = in->qualifier;
	if(in->count >= HLE_INPUT_EVENTS)
	{
		amiga_log_once(m, 0xE0000000u, "input.device event queue full; event dropped");
		return;
	}
	if(m->paula.lmb)
		qual |= IEQUALIFIER_LEFTBUTTON;
	if(m->paula.rmb)
		qual |= IEQUALIFIER_RBUTTON;
	if(cls == IECLASS_RAWMOUSE)
		qual |= IEQUALIFIER_RELATIVEMOUSE;
	ev = HLE_INPUT_EVENTS_AT + (uint32_t)in->count * IE_SIZE;
	hle_w32(m, ev + IE_NEXT, 0);
	hle_w8(m, ev + IE_CLASS, cls);
	hle_w8(m, ev + IE_CLASS + 1, 0);
	hle_w16(m, ev + IE_CODE, code);
	hle_w16(m, ev + IE_QUALIFIER, qual);
	hle_w16(m, ev + IE_X, (uint16_t)x);
	hle_w16(m, ev + IE_Y, (uint16_t)y);
	hle_w32(m, ev + 0x0E, 0);
	hle_w32(m, ev + 0x12, 0);
	if(in->count)
		hle_w32(m, ev - IE_SIZE + IE_NEXT, ev);
	in->count++;
}

/* keyboard.device's half: a raw keycode from CIA-A's SDR. Keycodes 0x60-0x67 are the
 * qualifier keys, HRM appendix H. */
void hle_input_key(struct Amiga *m, uint8_t sdr)
{
	uint8_t raw = (uint8_t)~sdr;
	uint8_t code = (uint8_t)((raw >> 1) | (raw << 7));
	int key = code & 0x7F, up = code & 0x80;
	if(!m->hle.input.open)
		return;
	if(key >= 0x60 && key <= 0x67)
	{
		uint16_t bit = (uint16_t)(1u << (key - 0x60));
		if(up)
			m->hle.input.qualifier &= (uint16_t)~bit;
		else
			m->hle.input.qualifier |= bit;
	}
	hle_input_event(m, IECLASS_RAWKEY, code, 0, 0);
}

/* gameport.device's half: the JOY0DAT counters' movement since the last vertical blank, and
 * any button that changed, one event per change. */
void hle_input_vblank(struct Amiga *m)
{
	struct HleInput *in = &m->hle.input;
	int dx = (int8_t)(uint8_t)(m->paula.mouse_x - in->mouse_x);
	int dy = (int8_t)(uint8_t)(m->paula.mouse_y - in->mouse_y);
	int moved = dx || dy;
	if(!in->open)
		return;
	in->mouse_x = (uint8_t)m->paula.mouse_x;
	in->mouse_y = (uint8_t)m->paula.mouse_y;
	if(m->paula.lmb != in->lmb)
	{
		hle_input_event(m, IECLASS_RAWMOUSE, (uint16_t)(IECODE_LBUTTON | (m->paula.lmb ? 0 : IECODE_UP_PREFIX)), (int16_t)dx, (int16_t)dy);
		in->lmb = m->paula.lmb;
		moved = 0;
	}
	if(m->paula.rmb != in->rmb)
	{
		hle_input_event(m, IECLASS_RAWMOUSE, (uint16_t)(IECODE_RBUTTON | (m->paula.rmb ? 0 : IECODE_UP_PREFIX)), (int16_t)(moved ? dx : 0), (int16_t)(moved ? dy : 0));
		in->rmb = m->paula.rmb;
		moved = 0;
	}
	if(moved)
		hle_input_event(m, IECLASS_RAWMOUSE, IECODE_NOBUTTON, (int16_t)dx, (int16_t)dy);
}

int hle_input_pending(struct Amiga *m)
{
	return m->hle.input.count > 0 && hle_r32(m, hle_r32(m, HLE_INPUT_HANDLERS + LH_HEAD) + LN_SUCC) != 0;
}

void hle_input_io(struct Amiga *m, uint32_t io)
{
	uint16_t cmd = hle_r16(m, io + IO_COMMAND);
	uint32_t data = hle_r32(m, io + IO_DATA);
	hle_w8(m, io + IO_ERROR, 0);
	switch(cmd)
	{
	case 9: /* IND_ADDHANDLER */
		hle_enqueue(m, HLE_INPUT_HANDLERS, data);
		return;
	case 10: /* IND_REMHANDLER */
		hle_remove(m, data);
		return;
	case 12: /* IND_SETTHRESH */
	case 13: /* IND_SETPERIOD */
	case 14: /* IND_SETMPORT */
	case 15: /* IND_SETMTYPE */
	case 16: /* IND_SETMTRIG */
		amiga_log_once(m, 0xE1000000u | cmd, "input.device command %u accepted and ignored", cmd);
		return;
	default:
		amiga_fatal(m, "input.device command %u is not implemented", cmd);
		return;
	}
}

/* The handlers are done with the first `delivering` events; keep any that arrived meanwhile. */
void hle_input_take(struct Amiga *m)
{
	struct HleInput *in = &m->hle.input;
	int keep = in->count - in->delivering, i;
	uint32_t b;
	for(i = 0; i < keep; i++)
	{
		uint32_t from = HLE_INPUT_EVENTS_AT + (uint32_t)(in->delivering + i) * IE_SIZE;
		uint32_t to = HLE_INPUT_EVENTS_AT + (uint32_t)i * IE_SIZE;
		for(b = 0; b < IE_SIZE; b++)
			hle_w8(m, to + b, hle_r8(m, from + b));
		hle_w32(m, to + IE_NEXT, i + 1 < keep ? to + IE_SIZE : 0);
	}
	in->count = keep > 0 ? keep : 0;
	in->delivering = 0;
}

/* The device's own vectors: BeginIO -30, AbortIO -36 (exec/devices.i). */
int hle_in_call(struct Amiga *m, uint32_t lvo)
{
	uint32_t io = m->cpu.a[1];
	switch(lvo)
	{
	case 30:
		hle_input_io(m, io);
		if(!(hle_r8(m, io + IO_FLAGS) & IOF_QUICK))
		{
			uint32_t port = hle_r32(m, io + MN_REPLYPORT);
			hle_w8(m, io + LN_TYPE, NT_REPLYMSG);
			if(port)
				hle_put_msg(m, port, io);
		}
		return 0;
	case 36:
		m->cpu.d[0] = 0;
		return 0;
	default:
		return -1;
	}
}
