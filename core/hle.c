#include "hle.h"
#include "cpu_ops.h"

#include <string.h>

/* High-level emulation of the Kickstart 1.3 calls a game makes. Guest structures are real
 * NDK-layout structures in slow RAM, so a game that reads or pokes them directly sees
 * something coherent. docs/runtime.md, "HLE". */

uint32_t hle_r32(struct Amiga *m, uint32_t a) { return mem_read32(m, a); }
uint16_t hle_r16(struct Amiga *m, uint32_t a) { return mem_read16(m, a); }
uint8_t hle_r8(struct Amiga *m, uint32_t a) { return mem_read8(m, a); }
void hle_w32(struct Amiga *m, uint32_t a, uint32_t v) { mem_write32(m, a, v); }
void hle_w16(struct Amiga *m, uint32_t a, uint16_t v) { mem_write16(m, a, v); }
void hle_w8(struct Amiga *m, uint32_t a, uint8_t v) { mem_write8(m, a, v); }

int hle_strcmp(struct Amiga *m, uint32_t a, const char *s)
{
	for(;; a++, s++)
	{
		uint8_t c = hle_r8(m, a);
		if(c != (uint8_t)*s)
			return 1;
		if(!c)
			return 0;
	}
}

uint32_t hle_string(struct Amiga *m, uint32_t at, const char *s)
{
	uint32_t i;
	for(i = 0; s[i]; i++)
		hle_w8(m, at + i, (uint8_t)s[i]);
	hle_w8(m, at + i, 0);
	return at;
}

void hle_name(struct Amiga *m, uint32_t a, char out[64])
{
	int i;
	for(i = 0; i < 63 && (out[i] = (char)hle_r8(m, a + (uint32_t)i)) != 0; i++)
		;
	out[i] = 0;
}

void hle_new_list(struct Amiga *m, uint32_t list, uint8_t type)
{
	hle_w32(m, list + LH_HEAD, list + LH_TAIL);
	hle_w32(m, list + LH_TAIL, 0);
	hle_w32(m, list + LH_TAILPRED, list + LH_HEAD);
	hle_w8(m, list + LH_TYPE, type);
}

void hle_add_tail(struct Amiga *m, uint32_t list, uint32_t node)
{
	uint32_t pred = hle_r32(m, list + LH_TAILPRED);
	hle_w32(m, node + LN_SUCC, list + LH_TAIL);
	hle_w32(m, node + LN_PRED, pred);
	hle_w32(m, pred + LN_SUCC, node);
	hle_w32(m, list + LH_TAILPRED, node);
}

void hle_remove(struct Amiga *m, uint32_t node)
{
	uint32_t succ = hle_r32(m, node + LN_SUCC), pred = hle_r32(m, node + LN_PRED);
	hle_w32(m, pred + LN_SUCC, succ);
	hle_w32(m, succ + LN_PRED, pred);
}

uint32_t hle_rem_head(struct Amiga *m, uint32_t list)
{
	uint32_t head = hle_r32(m, list + LH_HEAD);
	if(!hle_r32(m, head + LN_SUCC))
		return 0;
	hle_remove(m, head);
	return head;
}

/* Insert by ln_Pri, after every node of equal or higher priority. */
void hle_enqueue(struct Amiga *m, uint32_t list, uint32_t node)
{
	int8_t pri = (int8_t)hle_r8(m, node + LN_PRI);
	uint32_t at = hle_r32(m, list + LH_HEAD), pred;
	while(hle_r32(m, at + LN_SUCC) && (int8_t)hle_r8(m, at + LN_PRI) >= pri)
		at = hle_r32(m, at + LN_SUCC);
	pred = hle_r32(m, at + LN_PRED);
	hle_w32(m, node + LN_SUCC, at);
	hle_w32(m, node + LN_PRED, pred);
	hle_w32(m, pred + LN_SUCC, node);
	hle_w32(m, at + LN_PRED, node);
}

void hle_put_msg(struct Amiga *m, uint32_t port, uint32_t msg)
{
	uint32_t task = hle_r32(m, port + MP_SIGTASK);
	hle_add_tail(m, port + MP_MSGLIST, msg);
	if((hle_r8(m, port + MP_FLAGS) & 3) == 0 && task)
	{
		uint32_t bit = 1u << (hle_r8(m, port + MP_SIGBIT) & 31);
		hle_w32(m, task + TC_SIGRECVD, hle_r32(m, task + TC_SIGRECVD) | bit);
	}
}

void hle_jump_table(struct Amiga *m, uint32_t base, uint32_t negsize, int lib)
{
	uint32_t off;
	for(off = 6; off <= negsize; off += 6)
	{
		hle_w16(m, base - off, 0x4EF9);
		hle_w32(m, base - off + 2, HLE_ADDR(lib, off));
	}
}

static uint16_t hle_exec_chksum(struct Amiga *m)
{
	uint32_t sum = 0, off;
	for(off = 0x22; off < 0x52; off += 2)
		sum += hle_r16(m, HLE_EXEC_BASE + off);
	return (uint16_t)~sum;
}

/* Server lists live on the interrupt bits Kickstart gives them: PORTS, COPER, VERTB, EXTER
 * and NMI (RKM Libraries, "Exec Interrupts"). */
static const int hle_server_bits[5] = {3, 4, 5, 13, 15};

static const uint16_t hle_copinit[44] = {
	0x00E0, 0x0000, 0x00E2, 0x0000, 0x0120, 0x0000, 0x0122, 0x0C80,
	0x0124, 0x0000, 0x0126, 0x0478, 0x0128, 0x0000, 0x012A, 0x0478,
	0x012C, 0x0000, 0x012E, 0x0478, 0x0130, 0x0000, 0x0132, 0x0478,
	0x0134, 0x0000, 0x0136, 0x0478, 0x0138, 0x0000, 0x013A, 0x0478,
	0x013C, 0x0000, 0x013E, 0x0478, 0x0C01, 0xFFFE, 0x008A, 0x0000,
	0xFFFE, 0xFFFF, 0xFFFF, 0xFFFE};

void hle_build(struct Amiga *m)
{
	uint32_t eb = HLE_EXEC_BASE, n;
	uint32_t s_exec = hle_string(m, HLE_STRINGS, "exec.library");
	uint32_t s_gfx = hle_string(m, HLE_STRINGS + 0x20, "graphics.library");
	uint32_t s_td = hle_string(m, HLE_STRINGS + 0x40, "trackdisk.device");
	uint32_t s_in = hle_string(m, HLE_STRINGS + 0x60, "input.device");
	uint32_t s_int = hle_string(m, HLE_STRINGS + 0x80, "intuition.library");
	uint32_t s_exp = hle_string(m, HLE_STRINGS + 0xA0, "expansion.library");
	int i;

	for(n = 2; n < 64; n++)
		hle_w32(m, n * 4, HLE_EXCEPTION(n));
	hle_w32(m, 4, eb);

	/* ExecBase, with the scalar fields as they stood at the bootblock call (docs/runtime.md,
	 * "Kickstart handoff"). */
	hle_jump_table(m, eb, HLE_EXEC_NEGSIZE, HLE_LIB_EXEC);
	hle_w8(m, eb + LN_TYPE, NT_LIBRARY);
	hle_w32(m, eb + LN_NAME, s_exec);
	hle_w8(m, eb + LIB_FLAGS, 0x04);
	hle_w16(m, eb + LIB_NEGSIZE, HLE_EXEC_NEGSIZE);
	hle_w16(m, eb + LIB_POSSIZE, HLE_EXEC_POSSIZE);
	hle_w16(m, eb + LIB_VERSION, HLE_EXEC_VERSION);
	hle_w16(m, eb + LIB_REVISION, HLE_EXEC_REVISION);
	hle_w16(m, eb + LIB_OPENCNT, 2);
	hle_w32(m, eb + EB_CHKBASE, ~eb);
	hle_w32(m, eb + EB_SYSSTKUPPER, HLE_SSP);
	hle_w32(m, eb + EB_SYSSTKLOWER, 0xC7E800u);
	hle_w32(m, eb + EB_MAXLOCMEM, AMIGA_CHIP_SIZE);
	hle_w32(m, eb + EB_MAXEXTMEM, AMIGA_A1200 ? 0 : AMIGA_SLOW_BASE + AMIGA_SLOW_SIZE);
	hle_w16(m, eb + 0x52, hle_exec_chksum(m));
	hle_w32(m, eb + EB_THISTASK, HLE_THIS_TASK);
	hle_w16(m, eb + 0x120, 0x0010);
	hle_w8(m, eb + EB_IDNESTCNT, 0xFF);
	hle_w8(m, eb + EB_TDNESTCNT, 0xFF);
	/* AttnFlags: none on the 68000; AFF_68010 | AFF_68020 on the A1200 (exec/execbase.i). */
	hle_w16(m, eb + EB_ATTNFLAGS, AMIGA_A1200 ? 0x0003 : 0);
	hle_w32(m, eb + 0x13C, 0x0000FFFFu);
	hle_w16(m, eb + 0x140, 0x8000);
	hle_w8(m, eb + EB_VBLANKFREQ, 50);
	hle_w8(m, eb + EB_POWERFREQ, 50);
	hle_new_list(m, eb + EB_MEMLIST, NT_MEMORY);
	hle_new_list(m, eb + 0x150, 8);
	hle_new_list(m, eb + EB_DEVICELIST, NT_DEVICE);
	hle_new_list(m, eb + 0x16C, NT_INTERRUPT);
	hle_new_list(m, eb + EB_LIBLIST, NT_LIBRARY);
	hle_new_list(m, eb + EB_PORTLIST, NT_MSGPORT);
	hle_new_list(m, eb + 0x196, NT_TASK);
	hle_new_list(m, eb + 0x1A4, NT_TASK);
	hle_new_list(m, eb + 0x214, 15);
	hle_add_tail(m, eb + EB_LIBLIST, eb);

	/* expansion.library sits between exec and graphics in Kickstart 1.3's library list
	 * (docs/runtime.md, "Kickstart handoff"). It is stub library 0: OpenLibrary returns it and
	 * its first call is fatal. */
	hle_jump_table(m, HLE_EXP_BASE, HLE_EXP_NEGSIZE, HLE_LIB_STUB);
	hle_w8(m, HLE_EXP_BASE + LN_TYPE, NT_LIBRARY);
	hle_w32(m, HLE_EXP_BASE + LN_NAME, s_exp);
	hle_w16(m, HLE_EXP_BASE + LIB_NEGSIZE, HLE_EXP_NEGSIZE);
	hle_w16(m, HLE_EXP_BASE + LIB_POSSIZE, HLE_EXP_POSSIZE);
	hle_w16(m, HLE_EXP_BASE + LIB_VERSION, HLE_EXP_VERSION);
	hle_w16(m, HLE_EXP_BASE + LIB_REVISION, HLE_EXP_REVISION);
	hle_add_tail(m, eb + EB_LIBLIST, HLE_EXP_BASE);
	m->hle.stub_base[0] = HLE_EXP_BASE;
	m->hle.stub_count = 1;

	for(i = 0; i < 5; i++)
	{
		uint32_t list = HLE_SERVER_LISTS + (uint32_t)i * 16;
		uint32_t iv = eb + EB_INTVECTS + (uint32_t)hle_server_bits[i] * IV_SIZE;
		hle_new_list(m, list, NT_INTERRUPT);
		hle_w32(m, iv + IV_DATA, list);
		hle_w32(m, iv + IV_CODE, HLE_SERVERS);
	}

	/* The boot task: TaskSigAlloc's 16 reserved signals. */
	hle_w8(m, HLE_THIS_TASK + LN_TYPE, NT_TASK);
	hle_w8(m, HLE_THIS_TASK + 0x0F, 2);
	hle_w32(m, HLE_THIS_TASK + TC_SIGALLOC, 0x0000FFFFu);

	/* graphics.library */
	hle_jump_table(m, HLE_GFX_BASE, HLE_GFX_NEGSIZE, HLE_LIB_GFX);
	hle_w8(m, HLE_GFX_BASE + LN_TYPE, NT_LIBRARY);
	hle_w32(m, HLE_GFX_BASE + LN_NAME, s_gfx);
	hle_w16(m, HLE_GFX_BASE + LIB_NEGSIZE, HLE_GFX_NEGSIZE);
	hle_w16(m, HLE_GFX_BASE + LIB_POSSIZE, HLE_GFX_POSSIZE);
	hle_w16(m, HLE_GFX_BASE + LIB_VERSION, HLE_GFX_VERSION);
	hle_w16(m, HLE_GFX_BASE + LIB_REVISION, HLE_GFX_REVISION);
	hle_add_tail(m, eb + EB_LIBLIST, HLE_GFX_BASE);
	/* gb_DisplayFlags and graphics' display limits, as the PAL machine holds them; on the
	 * A1200 also gb_ChipRevBits0 (docs/runtime.md, "Kickstart handoff"). */
#if AMIGA_A1200
	hle_w16(m, HLE_GFX_BASE + GB_DISPLAYFLAGS, 0x0014);
	hle_w16(m, HLE_GFX_BASE + 0xD4, 0x0000);
	hle_w16(m, HLE_GFX_BASE + 0xD6, 0x01C7);
	hle_w16(m, HLE_GFX_BASE + 0xEA, 0x005D);
	hle_w8(m, HLE_GFX_BASE + 0xEC, 0x13);
#else
	hle_w16(m, HLE_GFX_BASE + GB_DISPLAYFLAGS, 0x0004);
	hle_w16(m, HLE_GFX_BASE + 0xD4, 0x0137);
	hle_w16(m, HLE_GFX_BASE + 0xD6, 0x01D1);
	hle_w16(m, HLE_GFX_BASE + 0xEA, 0x0030);
#endif
	hle_w16(m, HLE_GFX_BASE + 0xD8, 0x0100);
	hle_w16(m, HLE_GFX_BASE + 0xDA, 0x0280);
	hle_w16(m, HLE_GFX_BASE + 0xDC, 0x04CA);
	hle_w16(m, HLE_GFX_BASE + 0xDE, 0x0513);
	hle_w16(m, HLE_GFX_BASE + 0xE8, 0x3F9D);

	/* gb_copinit and the null sprite as they stand when the bootblock returns, and graphics'
	 * vertical-blank server at priority 10: graphics.library 34 / Kickstart 1.3 behaviour
	 * observed under WinUAE, docs/runtime.md, "HLE". */
	for(n = 0; n < sizeof hle_copinit / sizeof hle_copinit[0]; n++)
		hle_w16(m, HLE_COPINIT + 2 * n, hle_copinit[n]);
	hle_w32(m, HLE_NULL_SPRITE, 0xFE00FF00u);
	hle_w32(m, HLE_NULL_SPRITE + 4, 0);
	hle_w32(m, HLE_GFX_BASE + GB_COPINIT, HLE_COPINIT);
	m->copper.lc1 = HLE_COPINIT;
	hle_w8(m, HLE_GFX_BASE + GB_VBSRV + LN_PRI, 10);
	hle_w32(m, HLE_GFX_BASE + GB_VBSRV + IS_DATA, HLE_GFX_BASE);
	hle_w32(m, HLE_GFX_BASE + GB_VBSRV + IS_CODE, HLE_GFX_VBLANK);
	hle_enqueue(m, HLE_SERVER_LISTS + 2 * 16, HLE_GFX_BASE + GB_VBSRV);
	/* graphics' blitter interrupt handler: iv_Data is GfxBase at the bootblock call
	 * (docs/runtime.md, "Kickstart handoff"). */
	hle_w32(m, eb + EB_INTVECTS + 6 * IV_SIZE + IV_DATA, HLE_GFX_BASE);

	/* cia.resource's PORTS and EXTER servers, priority 0 as observed under WinUAE, docs/runtime.md, "HLE". */
	hle_w8(m, HLE_CIAA_NODE + LN_TYPE, NT_INTERRUPT);
	hle_w32(m, HLE_CIAA_NODE + IS_CODE, HLE_CIAA_SERVER);
	hle_enqueue(m, HLE_SERVER_LISTS + 0 * 16, HLE_CIAA_NODE);
	hle_w8(m, HLE_CIAB_NODE + LN_TYPE, NT_INTERRUPT);
	hle_w32(m, HLE_CIAB_NODE + IS_CODE, HLE_CIAB_SERVER);
	hle_enqueue(m, HLE_SERVER_LISTS + 3 * 16, HLE_CIAB_NODE);

	/* input.device and its handler list. */
	hle_jump_table(m, HLE_IN_BASE, HLE_IN_NEGSIZE, HLE_LIB_INPUT);
	hle_w8(m, HLE_IN_BASE + LN_TYPE, NT_DEVICE);
	hle_w32(m, HLE_IN_BASE + LN_NAME, s_in);
	hle_w16(m, HLE_IN_BASE + LIB_NEGSIZE, HLE_IN_NEGSIZE);
	hle_w16(m, HLE_IN_BASE + LIB_POSSIZE, 0x22);
	hle_add_tail(m, eb + EB_DEVICELIST, HLE_IN_BASE);
	hle_new_list(m, HLE_INPUT_HANDLERS, NT_INTERRUPT);

	/* trackdisk.device and its four units. */
	hle_jump_table(m, HLE_TD_BASE, HLE_TD_NEGSIZE, HLE_LIB_TRACKDISK);
	hle_w8(m, HLE_TD_BASE + LN_TYPE, NT_DEVICE);
	hle_w32(m, HLE_TD_BASE + LN_NAME, s_td);
	hle_w16(m, HLE_TD_BASE + LIB_NEGSIZE, HLE_TD_NEGSIZE);
	hle_w16(m, HLE_TD_BASE + LIB_POSSIZE, 0x40);
	hle_add_tail(m, eb + EB_DEVICELIST, HLE_TD_BASE);
	for(i = 0; i < AMIGA_DRIVES; i++)
	{
		hle_w8(m, hle_td_units[i] + LN_TYPE, NT_MSGPORT);
		hle_new_list(m, hle_td_units[i] + MP_MSGLIST, NT_MESSAGE);
	}

	/* layers.library sits between graphics and intuition in Kickstart 1.3's library list
	 * (docs/runtime.md, "Kickstart handoff"). */
	hle_layers_build(m);
	hle_add_tail(m, eb + EB_LIBLIST, HLE_LAYERS_BASE);
	hle_audio_build(m);

	/* intuition.library */
	hle_jump_table(m, HLE_INT_BASE, HLE_INT_NEGSIZE, HLE_LIB_INTUITION);
	hle_w8(m, HLE_INT_BASE + LN_TYPE, NT_LIBRARY);
	hle_w32(m, HLE_INT_BASE + LN_NAME, s_int);
	hle_w16(m, HLE_INT_BASE + LIB_NEGSIZE, HLE_INT_NEGSIZE);
	hle_w16(m, HLE_INT_BASE + LIB_POSSIZE, LIB_SIZE);
	hle_w16(m, HLE_INT_BASE + LIB_VERSION, HLE_INT_VERSION);
	hle_w16(m, HLE_INT_BASE + LIB_REVISION, HLE_INT_REVISION);
	hle_add_tail(m, eb + EB_LIBLIST, HLE_INT_BASE);
	hle_int_build(m);
	hle_res_build(m);

	/* The ROM's IORequest, opened on unit 0, and its reply port. */
	hle_w8(m, HLE_BOOT_PORT + LN_TYPE, NT_MSGPORT);
	hle_w32(m, HLE_BOOT_PORT + MP_SIGTASK, HLE_THIS_TASK);
	hle_new_list(m, HLE_BOOT_PORT + MP_MSGLIST, NT_MESSAGE);
	hle_w8(m, HLE_BOOT_IOREQ + LN_TYPE, NT_REPLYMSG);
	hle_w32(m, HLE_BOOT_IOREQ + MN_REPLYPORT, HLE_BOOT_PORT);
	hle_w16(m, HLE_BOOT_IOREQ + 0x12, 0x38);
	hle_w32(m, HLE_BOOT_IOREQ + IO_DEVICE, HLE_TD_BASE);
	hle_w32(m, HLE_BOOT_IOREQ + IO_UNIT, HLE_TD_UNIT0);

	hle_dos_build(m);
	hle_mem_init(m);
}

/* --- interrupts -------------------------------------------------------------------------- */

static const uint16_t hle_level_bits[7] = {0, 0x0007, 0x0008, 0x0070, 0x0780, 0x1800, 0x2000};

static void hle_clear_intreq(struct Amiga *m, int bit)
{
	hle_w16(m, 0xDFF09C, (uint16_t)(1u << bit));
}

static void hle_frame_save(struct Amiga *m, struct HleFrame *f)
{
	f->saved[0] = m->cpu.d[0];
	f->saved[1] = m->cpu.d[1];
	f->saved[2] = m->cpu.a[0];
	f->saved[3] = m->cpu.a[1];
	f->saved[4] = m->cpu.a[5];
	f->saved[5] = m->cpu.a[6];
}

static void hle_finish(struct Amiga *m, const struct HleFrame *f)
{
	m->cpu.d[0] = f->saved[0];
	m->cpu.d[1] = f->saved[1];
	m->cpu.a[0] = f->saved[2];
	m->cpu.a[1] = f->saved[3];
	m->cpu.a[5] = f->saved[4];
	m->cpu.a[6] = f->saved[5];
	cpu_rte(m);
}

/* Call guest code `code` with exec's interrupt register conventions; HLE work resumes from
 * frame `f` when it returns into HLE_CONTINUE. */
static void hle_call_guest(struct Amiga *m, const struct HleFrame *f, uint32_t code, uint32_t data)
{
	if(m->hle.depth >= 8)
	{
		amiga_fatal(m, "HLE continuation stack overflow");
		return;
	}
	m->hle.frames[m->hle.depth++] = *f;
	m->cpu.a[0] = 0xDFF000;
	m->cpu.a[1] = data;
	m->cpu.a[5] = code;
	m->cpu.a[6] = HLE_EXEC_BASE;
	m->cpu.d[1] = m->paula.intena & m->paula.intreq;
	cpu_push32(m, HLE_CONTINUE);
	m->cpu.pc = code;
}

/* Call guest code `code` as a function, with the registers the caller sets; HLE work resumes
 * from frame `f` when it returns into HLE_CONTINUE. */
void hle_call(struct Amiga *m, const struct HleFrame *f, uint32_t code)
{
	if(m->hle.depth >= 8)
	{
		amiga_fatal(m, "HLE continuation stack overflow");
		return;
	}
	m->hle.frames[m->hle.depth++] = *f;
	cpu_push32(m, HLE_CONTINUE);
	m->cpu.pc = code;
}

/* The end of an interrupt. input.device's task would run now, so pending input events go to
 * the handler chain first, highest priority first, a0 = the events and a1 = is_Data. */
static void hle_end(struct Amiga *m, struct HleFrame *f)
{
	uint32_t node;
	if(m->hle.input.delivering || !hle_input_pending(m))
	{
		hle_finish(m, f);
		return;
	}
	node = hle_r32(m, HLE_INPUT_HANDLERS + LH_HEAD);
	m->hle.input.delivering = m->hle.input.count;
	f->kind = HleFrameInput;
	f->node = node;
	hle_call_guest(m, f, hle_r32(m, node + IS_CODE), hle_r32(m, node + IS_DATA));
	m->cpu.a[0] = HLE_INPUT_EVENTS_AT;
}

static void hle_next_soft(struct Amiga *m, struct HleFrame *f)
{
	uint32_t node;
	if(!m->hle.softint_count)
	{
		hle_end(m, f);
		return;
	}
	node = m->hle.softint_pending[0];
	memmove(m->hle.softint_pending, m->hle.softint_pending + 1, (size_t)(--m->hle.softint_count) * sizeof(uint32_t));
	hle_w8(m, node + LN_TYPE, NT_INTERRUPT);
	f->node = node;
	hle_call_guest(m, f, hle_r32(m, node + IS_CODE), hle_r32(m, node + IS_DATA));
}

static void hle_interrupt(struct Amiga *m, int level)
{
	uint16_t pend = m->paula.intena & m->paula.intreq & hle_level_bits[level];
	struct HleFrame f;
	uint32_t iv, code, data;
	int bit;
	if(!pend || !(m->paula.intena & 0x4000))
	{
		cpu_rte(m);
		return;
	}
	for(bit = 13; bit >= 0 && !(pend & (1u << bit)); bit--)
		;
	memset(&f, 0, sizeof f);
	f.level = level;
	f.bit = bit;
	hle_frame_save(m, &f);
	iv = HLE_EXEC_BASE + EB_INTVECTS + (uint32_t)bit * IV_SIZE;
	code = hle_r32(m, iv + IV_CODE);
	data = hle_r32(m, iv + IV_DATA);
	if(bit == 2 && code == 0)
	{
		hle_clear_intreq(m, 2);
		f.kind = HleFrameSoft;
		hle_next_soft(m, &f);
		return;
	}
	if(code == HLE_SERVERS)
	{
		uint32_t node = hle_r32(m, data + LH_HEAD);
		if(!hle_r32(m, node + LN_SUCC))
		{
			hle_clear_intreq(m, bit);
			hle_end(m, &f);
			return;
		}
		f.kind = HleFrameServer;
		f.node = node;
		hle_call_guest(m, &f, hle_r32(m, node + IS_CODE), hle_r32(m, node + IS_DATA));
		return;
	}
	if(code == 0)
	{
		hle_clear_intreq(m, bit);
		hle_end(m, &f);
		return;
	}
	f.kind = HleFrameHandler;
	hle_call_guest(m, &f, code, data);
}

#if AMIGA_A1200
/* A CIA's server (level 0 CIA-A, 1 CIA-B): the owner of each ICR bit in `bit` in turn, guest
 * code by exec's interrupt conventions (cia.doc), the HLE's own at once; then the server returns
 * with Z set. */
static void hle_icr_next(struct Amiga *m, struct HleFrame *f)
{
	uint32_t pending = (uint32_t)f->bit;
	uint32_t node = hle_res_icr_next(m, f->level, &pending);
	f->bit = (int)pending;
	if(node)
	{
		hle_call_guest(m, f, hle_r32(m, node + IS_CODE), hle_r32(m, node + IS_DATA));
		return;
	}
	m->cpu.z = 1;
	m->cpu.pc = cpu_pop32(m);
}

static void hle_icr_server(struct Amiga *m, int which)
{
	struct Cia *c = which ? &m->cia_b : &m->cia_a;
	struct HleFrame f;
	uint8_t enabled = c->icr_mask;
	memset(&f, 0, sizeof f);
	f.kind = HleFrameIcr;
	f.level = which;
	f.bit = cia_read(m, c, 0xD) & enabled & 0x1F;
	hle_icr_next(m, &f);
}
#endif

static void hle_continue(struct Amiga *m)
{
	struct HleFrame f;
	if(m->hle.depth == 0)
	{
		amiga_fatal(m, "HLE continuation reached with nothing to continue");
		return;
	}
	f = m->hle.frames[--m->hle.depth];
	switch(f.kind)
	{
	case HleFrameHandler:
		hle_end(m, &f);
		return;
	case HleFrameServer:
	{
		uint32_t next = hle_r32(m, f.node + LN_SUCC);
		/* A server returning with Z clear ends the chain. RKM Libraries. */
		if(m->cpu.z && hle_r32(m, next + LN_SUCC))
		{
			f.node = next;
			hle_call_guest(m, &f, hle_r32(m, next + IS_CODE), hle_r32(m, next + IS_DATA));
			return;
		}
		hle_clear_intreq(m, f.bit);
		hle_end(m, &f);
		return;
	}
	case HleFrameSoft:
		hle_next_soft(m, &f);
		return;
	case HleFrameInput:
	{
		uint32_t events = m->cpu.d[0], next = hle_r32(m, f.node + LN_SUCC);
		if(events && hle_r32(m, next + LN_SUCC))
		{
			f.node = next;
			hle_call_guest(m, &f, hle_r32(m, next + IS_CODE), hle_r32(m, next + IS_DATA));
			m->cpu.a[0] = events;
			return;
		}
		hle_input_take(m);
		hle_finish(m, &f);
		return;
	}
	case HleFrameLibInit:
		hle_exec_lib_continue(m, &f);
		return;
	case HleFrameFmt:
		hle_exec_fmt_continue(m, &f);
		return;
#if AMIGA_A1200
	case HleFrameIcr:
		hle_icr_next(m, &f);
		return;
	case HleFrameDosRequest:
		hle_dos_request_done(m, &f);
		return;
	case HleFrameRunCommand:
		hle_dos_run_done(m, &f);
		return;
#endif
	default:
		amiga_fatal(m, "bad HLE frame kind %d", f.kind);
		return;
	}
}

/* --- dispatch ---------------------------------------------------------------------------- */

static int hle_internal(struct Amiga *m, uint32_t off)
{
	if(off < 0x400)
	{
		int vec = (int)(off / 4);
		if(vec >= 25 && vec <= 30)
		{
			hle_interrupt(m, vec - 24);
			return 0;
		}
		amiga_fatal(m, "unhandled exception %d, stacked SR 0x%04X PC 0x%06X", vec,
			hle_r16(m, m->cpu.a[7]), hle_r32(m, m->cpu.a[7] + 2));
		return 0;
	}
	switch(off)
	{
	case 0x400:
		boot_return(m);
		return 0;
	case 0x408:
		if(m->frame_count >= m->hle.wait_tof_frame)
		{
			m->cpu.pc = cpu_pop32(m);
			return 0;
		}
		return 1;
	case 0x40C:
		hle_continue(m);
		return 0;
	case 0x410:
		/* graphics', gameport's, Intuition's and timer.device's vertical-blank work, and on the
		 * A1200 the requester's pointer. */
		hle_gfx_vblank(m);
		hle_input_vblank(m);
		hle_int_vblank(m);
		hle_timer_vblank(m);
#if AMIGA_A1200
		hle_con_vblank(m);
#endif
		m->cpu.z = 1;
		m->cpu.pc = cpu_pop32(m);
		return 0;
	case 0x414:
	{
		/* cia.resource acknowledges CIA-A by reading ICR; a serial-port interrupt is a key
		 * for keyboard.device. On the A1200 each ICR bit goes to its owner (hle_res.c). */
		uint8_t icr;
#if AMIGA_A1200
		hle_icr_server(m, 0);
		return 0;
#endif
		icr = cia_read(m, &m->cia_a, 0xD);
		if(icr & 0x08)
			hle_input_key(m, m->cia_a.sdr);
		m->cpu.z = 1;
		m->cpu.pc = cpu_pop32(m);
		return 0;
	}
	case 0x418:
#if AMIGA_A1200
		hle_icr_server(m, 1);
		return 0;
#endif
		cia_read(m, &m->cia_b, 0xD);
		m->cpu.z = 1;
		m->cpu.pc = cpu_pop32(m);
		return 0;
	case 0x41C:
		hle_dos_boot(m);
		return 0;
	case 0x420:
		hle_dos_cli_return(m);
		return 0;
	case 0x424:
		/* dos Delay: parked until its frame. */
		if(m->frame_count >= m->hle.dos.delay_frame)
		{
			m->cpu.pc = cpu_pop32(m);
			return 0;
		}
		return 1;
	case 0x428:
	{
		/* exec Wait, WaitPort and WaitIO: parked until one of the signals arrives; WaitPort
		 * until its port also holds a message, which it returns; WaitIO until its request is
		 * replied. */
		uint32_t task = hle_r32(m, HLE_EXEC_BASE + EB_THISTASK), got = hle_r32(m, task + TC_SIGRECVD) & m->hle.wait_mask;
		if(!got)
			return 1;
		hle_w32(m, task + TC_SIGRECVD, hle_r32(m, task + TC_SIGRECVD) & ~got);
		if(m->hle.wait_port)
		{
			uint32_t msg = hle_r32(m, m->hle.wait_port + MP_MSGLIST + LH_HEAD);
			if(!hle_r32(m, msg + LN_SUCC))
				return 1;
			got = msg;
			m->hle.wait_port = 0;
		}
		hle_w32(m, task + TC_SIGWAIT, 0);
		m->cpu.d[0] = got;
		if(m->hle.wait_io)
		{
			if(hle_r8(m, m->hle.wait_io + LN_TYPE) != NT_REPLYMSG)
				return 1;
			hle_exec_io_done(m, m->hle.wait_io);
			m->hle.wait_io = 0;
		}
		m->cpu.pc = cpu_pop32(m);
		return 0;
	}
	case 0x42C:
		/* Intuition's input handler: a0 the events, a1 is_Data; d0 the events it passes on. */
		hle_int_input(m);
		m->cpu.pc = cpu_pop32(m);
		return 0;
	case 0x430:
		/* audio.device's interrupt handler for channel a1 (iv_Data). */
		hle_audio_interrupt(m, (int)(m->cpu.a[1] & 3));
		m->cpu.pc = cpu_pop32(m);
		return 0;
	case 0x434:
		/* keyboard.device's serial-port interrupt, called as its ICR vector's code. */
		hle_input_key(m, m->cia_a.sdr);
		m->cpu.pc = cpu_pop32(m);
		return 0;
	case 0x438:
		/* An ICR owner whose work the HLE does elsewhere. */
		m->cpu.pc = cpu_pop32(m);
		return 0;
#if AMIGA_A1200
	case 0x43C:
		return hle_con_park(m);
	case 0x440:
		return hle_con_request_park(m);
#endif
	default:
		amiga_fatal(m, "jump to internal HLE entry 0x%03X", (unsigned)off);
		return 0;
	}
}

/* The PC is in the HLE region. Returns 1 if the CPU is idle and time should advance. */
int hle_dispatch(struct Amiga *m)
{
	uint32_t pc = m->cpu.pc & 0xFFFFFFu;
	int lib = (int)((pc >> 12) & (HLE_LIBS - 1));
	uint32_t off = pc & 0xFFF;
	uint32_t caller = mem_read32(m, m->cpu.a[7]);
	int done;
	if(lib == HLE_LIB_INTERNAL)
		return hle_internal(m, off);
	if(lib < HLE_CALL_LIBS && off / 6 < 256)
		m->hle.calls[lib][off / 6]++;
	switch(lib)
	{
	case HLE_LIB_EXEC: done = hle_exec_call(m, off); break;
	case HLE_LIB_GFX: done = hle_gfx_call(m, off); break;
	case HLE_LIB_TRACKDISK: done = hle_td_call(m, off); break;
	case HLE_LIB_INPUT: done = hle_in_call(m, off); break;
	case HLE_LIB_INTUITION: done = hle_int_call(m, off); break;
	case HLE_LIB_DOS: done = hle_dos_call(m, off); break;
	case HLE_LIB_LAYERS: done = hle_layers_call(m, off); break;
	case HLE_LIB_AUDIO: done = hle_audio_call(m, off); break;
	default: done = lib >= HLE_LIB_POTGO ? hle_res_call(m, lib, off) : -1; break;
	}
	if(done < 0)
	{
		static const char *names[HLE_LIBS] = {"exec.library", "graphics.library", "trackdisk.device", "input.device",
			"intuition.library", "dos.library", "layers.library", "audio.device", [HLE_LIB_POTGO] = "potgo.resource",
			[HLE_LIB_CIAA] = "ciaa.resource", [HLE_LIB_CIAB] = "ciab.resource", [HLE_LIB_TIMER] = "timer.device",
			[HLE_LIB_KEYBOARD] = "keyboard.device", [HLE_LIB_UTILITY] = "utility.library", [HLE_LIB_DISK] = "disk.resource"};
		char buf[64];
		const char *who = names[lib] ? names[lib] : "unknown library";
		if(lib >= HLE_LIB_STUB && lib - HLE_LIB_STUB < m->hle.stub_count)
		{
			hle_name(m, hle_r32(m, m->hle.stub_base[lib - HLE_LIB_STUB] + LN_NAME), buf);
			who = buf;
		}
		amiga_fatal(m, "%s LVO -%u (0x%03X) is not implemented, called from 0x%06X",
			who, (unsigned)off, (unsigned)off, caller);
		return 0;
	}
	if(done == 0)
		m->cpu.pc = cpu_pop32(m);
	return 0;
}
