#include "hle.h"

/* keyboard.device, on the A1200 only (keyboard.doc and devices/keyboard.i, ADCD 2.1): the key
 * matrix that KBD_READMATRIX copies, one bit per raw key code, kept from the keys CIA-A's
 * serial port delivers (hle_input_key), and reset handlers, which are recorded and never
 * called because the machine is never reset. */

#if AMIGA_A1200

#define KBD_READEVENT 9
#define KBD_READMATRIX 10
#define KBD_ADDRESETHANDLER 11
#define KBD_REMRESETHANDLER 12
#define KBD_RESETHANDLERDONE 13

void hle_kbd_build(struct Amiga *m)
{
	hle_jump_table(m, HLE_KBD_BASE, 0x24, HLE_LIB_KEYBOARD);
	hle_w8(m, HLE_KBD_BASE + LN_TYPE, NT_DEVICE);
	hle_w32(m, HLE_KBD_BASE + LN_NAME, hle_string(m, HLE_RES_STRINGS + 0x80, "keyboard.device"));
	hle_w16(m, HLE_KBD_BASE + LIB_NEGSIZE, 0x24);
	hle_w16(m, HLE_KBD_BASE + LIB_POSSIZE, LIB_SIZE);
	hle_w16(m, HLE_KBD_BASE + LIB_VERSION, 37);
	hle_w16(m, HLE_KBD_BASE + LIB_REVISION, 12);
	hle_add_tail(m, HLE_EXEC_BASE + EB_DEVICELIST, HLE_KBD_BASE);
}

void hle_kbd_key(struct Amiga *m, uint8_t code)
{
	uint8_t key = code & 0x7F, bit = (uint8_t)(1u << (key & 7));
	if(code & 0x80)
		m->hle.key_matrix[key >> 3] &= (uint8_t)~bit;
	else
		m->hle.key_matrix[key >> 3] |= bit;
	hle_con_key(m, code);
}

/* BeginIO: every command completes at once. */
void hle_kbd_io(struct Amiga *m, uint32_t io)
{
	uint32_t i, n;
	hle_w8(m, io + IO_ERROR, 0);
	switch(hle_r16(m, io + IO_COMMAND))
	{
	case KBD_READMATRIX:
		n = hle_r32(m, io + IO_LENGTH);
		if(n > sizeof m->hle.key_matrix)
			n = sizeof m->hle.key_matrix;
		for(i = 0; i < n; i++)
			hle_w8(m, hle_r32(m, io + IO_DATA) + i, m->hle.key_matrix[i]);
		hle_w32(m, io + IO_ACTUAL, n);
		return;
	case KBD_ADDRESETHANDLER:
	case KBD_REMRESETHANDLER:
	case KBD_RESETHANDLERDONE:
		return;
	default:
		amiga_fatal(m, "keyboard.device command %u is not implemented", hle_r16(m, io + IO_COMMAND));
		return;
	}
}

#endif
