#include "hle.h"
#include "cpu_ops.h"

#include <stdio.h>
#include <string.h>

/* exec.library and graphics.library functions, by negative LVO. Every LVO is from NDK 3.2
 * FD/exec_lib.fd and FD/graphics_lib.fd (bias 30, 6 bytes a slot). Returns 0 to return to the
 * caller, 1 when the function has set the PC itself, -1 when it is not implemented. */

#define D0 m->cpu.d[0]
#define D1 m->cpu.d[1]
#define A0 m->cpu.a[0]
#define A1 m->cpu.a[1]

/* --- memory: first fit over real MemHeaders -------------------------------------------- */

/* Replace a MemHeader's free list with `count` (address, bytes) chunks. */
void hle_mem_set(struct Amiga *m, uint32_t mh, const uint32_t *chunks, int count)
{
	uint32_t free = 0, prev = mh + MH_FIRST;
	int i;
	for(i = 0; i < count; i++)
	{
		hle_w32(m, prev, chunks[2 * i]);
		hle_w32(m, chunks[2 * i] + MC_BYTES, chunks[2 * i + 1]);
		prev = chunks[2 * i] + MC_NEXT;
		free += chunks[2 * i + 1];
	}
	hle_w32(m, prev, 0);
	hle_w32(m, mh + MH_FREE, free);
}

static void hle_mem_header(struct Amiga *m, uint32_t mh, uint16_t attr, int8_t pri, uint32_t lower, uint32_t upper)
{
	hle_w8(m, mh + LN_TYPE, NT_MEMORY);
	hle_w8(m, mh + LN_PRI, (uint8_t)pri);
	hle_w16(m, mh + MH_ATTRIBUTES, attr);
	hle_w32(m, mh + MH_LOWER, lower);
	hle_w32(m, mh + MH_UPPER, upper);
	hle_enqueue(m, HLE_EXEC_BASE + EB_MEMLIST, mh);
}

/* Both MemHeaders and their free lists exactly as Kickstart 1.3 left them at the bootblock
 * call, walked chunk by chunk under WinUAE: docs/runtime.md, "Kickstart handoff". */
void hle_mem_init(struct Amiga *m)
{
	static const uint32_t slow[4] = {0xC01808u, 0x10, 0xC04AD0u, 0x79D30u};
	static const uint32_t chip[2] = {0x0059E8u, 0x7A618u};
	hle_mem_header(m, HLE_SLOW_MEMHEADER, MEMF_PUBLIC | MEMF_FAST, 0, 0xC004E8u, 0xC7E800u);
	hle_mem_header(m, HLE_CHIP_MEMHEADER, MEMF_PUBLIC | MEMF_CHIP, -10, 0x000420u, AMIGA_CHIP_SIZE);
	hle_mem_set(m, HLE_SLOW_MEMHEADER, slow, 2);
	hle_mem_set(m, HLE_CHIP_MEMHEADER, chip, 1);
}

static uint32_t hle_alloc_from(struct Amiga *m, uint32_t mh, uint32_t size, uint32_t at)
{
	uint32_t prev = mh + MH_FIRST, c = hle_r32(m, prev);
	while(c)
	{
		uint32_t bytes = hle_r32(m, c + MC_BYTES), next = hle_r32(m, c + MC_NEXT);
		uint32_t start = at ? at : c;
		if(start >= c && start + size <= c + bytes)
		{
			uint32_t tail = c + bytes - (start + size);
			if(start > c)
			{
				hle_w32(m, c + MC_BYTES, start - c);
				prev = c + MC_NEXT;
			}
			if(tail)
			{
				uint32_t t = start + size;
				hle_w32(m, t + MC_NEXT, next);
				hle_w32(m, t + MC_BYTES, tail);
				next = t;
			}
			hle_w32(m, prev, next);
			hle_w32(m, mh + MH_FREE, hle_r32(m, mh + MH_FREE) - size);
			return start;
		}
		prev = c + MC_NEXT;
		c = next;
	}
	return 0;
}

uint32_t hle_alloc(struct Amiga *m, uint32_t size, uint32_t req, uint32_t at)
{
	uint32_t mh = hle_r32(m, HLE_EXEC_BASE + EB_MEMLIST + LH_HEAD);
	if(!size)
		return 0;
	if(at)
	{
		size += at & 7;
		at &= ~7u;
	}
	size = (size + 7) & ~7u;
	for(; hle_r32(m, mh + LN_SUCC); mh = hle_r32(m, mh + LN_SUCC))
	{
		uint32_t attr = hle_r16(m, mh + MH_ATTRIBUTES), p, i;
		if((req & (MEMF_CHIP | MEMF_FAST)) & ~attr)
			continue;
		if(at && (at < hle_r32(m, mh + MH_LOWER) || at >= hle_r32(m, mh + MH_UPPER)))
			continue;
		p = hle_alloc_from(m, mh, size, at);
		if(!p)
			continue;
		if(req & MEMF_CLEAR)
			for(i = 0; i < size; i += 4)
				hle_w32(m, p + i, 0);
		return p;
	}
	return 0;
}

void hle_free(struct Amiga *m, uint32_t block, uint32_t size)
{
	uint32_t mh = hle_r32(m, HLE_EXEC_BASE + EB_MEMLIST + LH_HEAD);
	size = (size + (block & 7) + 7) & ~7u;
	block &= ~7u;
	for(; hle_r32(m, mh + LN_SUCC); mh = hle_r32(m, mh + LN_SUCC))
	{
		uint32_t prev = mh + MH_FIRST, c;
		if(block < hle_r32(m, mh + MH_LOWER) || block >= hle_r32(m, mh + MH_UPPER))
			continue;
		for(c = hle_r32(m, prev); c && c < block; c = hle_r32(m, c + MC_NEXT))
			prev = c + MC_NEXT;
		hle_w32(m, block + MC_NEXT, c);
		hle_w32(m, block + MC_BYTES, size);
		hle_w32(m, prev, block);
		if(c && block + size == c)
		{
			hle_w32(m, block + MC_BYTES, size + hle_r32(m, c + MC_BYTES));
			hle_w32(m, block + MC_NEXT, hle_r32(m, c + MC_NEXT));
		}
		if(prev != mh + MH_FIRST)
		{
			uint32_t p = prev - MC_NEXT;
			if(p + hle_r32(m, p + MC_BYTES) == block)
			{
				hle_w32(m, p + MC_BYTES, hle_r32(m, p + MC_BYTES) + hle_r32(m, block + MC_BYTES));
				hle_w32(m, p + MC_NEXT, hle_r32(m, block + MC_NEXT));
			}
		}
		hle_w32(m, mh + MH_FREE, hle_r32(m, mh + MH_FREE) + size);
		return;
	}
	amiga_fatal(m, "FreeMem(0x%06X, %u) outside every MemHeader", block, size);
}

/* --- signals and messages ---------------------------------------------------------------- */

static uint32_t hle_task(struct Amiga *m)
{
	return hle_r32(m, HLE_EXEC_BASE + EB_THISTASK);
}

int32_t hle_exec_alloc_signal(struct Amiga *m, int32_t want)
{
	uint32_t t = hle_task(m), alloc = hle_r32(m, t + TC_SIGALLOC);
	int b;
	if(want >= 0)
	{
		if(want > 31 || (alloc & (1u << want)))
			return -1;
		b = want;
	}
	else
	{
		for(b = 31; b >= 0 && (alloc & (1u << b)); b--)
			;
		if(b < 0)
			return -1;
	}
	hle_w32(m, t + TC_SIGALLOC, alloc | (1u << b));
	hle_w32(m, t + TC_SIGRECVD, hle_r32(m, t + TC_SIGRECVD) & ~(1u << b));
	return b;
}

void hle_exec_free_signal(struct Amiga *m, uint32_t bit)
{
	if((int32_t)bit >= 0 && bit < 32)
		hle_w32(m, hle_task(m) + TC_SIGALLOC, hle_r32(m, hle_task(m) + TC_SIGALLOC) & ~(1u << bit));
}

void hle_reply_msg(struct Amiga *m, uint32_t msg)
{
	uint32_t port = hle_r32(m, msg + MN_REPLYPORT);
	if(!port)
	{
		hle_w8(m, msg + LN_TYPE, NT_FREEMSG);
		return;
	}
	hle_w8(m, msg + LN_TYPE, NT_REPLYMSG);
	hle_put_msg(m, port, msg);
}

/* --- I/O ------------------------------------------------------------------------------------ */

/* Returns 1 when the request is complete (and replied unless IOF_QUICK), 0 when the device
 * replies it later, -1 after a fatal. */
static int hle_begin_io(struct Amiga *m, uint32_t io)
{
	uint32_t dev = hle_r32(m, io + IO_DEVICE);
	if(dev == HLE_TD_BASE)
		hle_td_io(m, io);
	else if(dev == HLE_IN_BASE)
		hle_input_io(m, io);
	else if(dev == HLE_AUDIO_BASE)
	{
		if(!hle_audio_io(m, io))
			return 0;
	}
	else
	{
		amiga_fatal(m, "I/O on device 0x%06X, which this runtime does not provide", dev);
		return -1;
	}
	if(!(hle_r8(m, io + IO_FLAGS) & IOF_QUICK))
		hle_reply_msg(m, io);
	return 1;
}

static uint32_t hle_io_error(struct Amiga *m, uint32_t io)
{
	return (uint32_t)(int32_t)(int8_t)hle_r8(m, io + IO_ERROR);
}

/* Wait(set): with none of the signals set, the CPU parks at HLE_WAIT_PARK until an interrupt
 * (input, audio, Intuition) signals the one task (hle.c). For WaitPort, `port` is the port, and
 * the park ends only once it holds a message; for WaitIO, `io` is the request, and it ends once
 * the request is replied. */
static int hle_wait(struct Amiga *m, uint32_t set, uint32_t port, uint32_t io)
{
	uint32_t t = hle_task(m), got = hle_r32(m, t + TC_SIGRECVD) & set;
	if(got && !port && !io)
	{
		hle_w32(m, t + TC_SIGRECVD, hle_r32(m, t + TC_SIGRECVD) & ~got);
		D0 = got;
		return 0;
	}
	if(!(set & hle_r32(m, t + TC_SIGALLOC)))
	{
		amiga_fatal(m, "Wait(0x%08X) on signals that are not allocated", set);
		return 0;
	}
	if((int8_t)hle_r8(m, HLE_EXEC_BASE + EB_IDNESTCNT) >= 0)
	{
		amiga_fatal(m, "Wait(0x%08X) with interrupts disabled", set);
		return 0;
	}
	m->hle.wait_mask = set;
	m->hle.wait_port = port;
	m->hle.wait_io = io;
	hle_w32(m, t + TC_SIGWAIT, set);
	m->cpu.pc = HLE_WAIT_PARK;
	return 1;
}

/* WaitIO(io): a replied request leaves its reply port and gives its io_Error; one still in the
 * device is waited for on the reply port's signal. */
static int hle_wait_io(struct Amiga *m, uint32_t io)
{
	uint32_t port = hle_r32(m, io + MN_REPLYPORT);
	if((hle_r8(m, io + IO_FLAGS) & IOF_QUICK) || hle_r8(m, io + LN_TYPE) == NT_REPLYMSG)
	{
		hle_exec_io_done(m, io);
		return 0;
	}
	if(!port)
	{
		amiga_fatal(m, "WaitIO(0x%06X) on a request with no reply port", io);
		return 0;
	}
	return hle_wait(m, 1u << (hle_r8(m, port + MP_SIGBIT) & 31), 0, io);
}

/* A completed request: off its reply port if it is queued there, D0 = io_Error. */
void hle_exec_io_done(struct Amiga *m, uint32_t io)
{
	uint32_t port = hle_r32(m, io + MN_REPLYPORT), n;
	if(port && !(hle_r8(m, io + IO_FLAGS) & IOF_QUICK))
		for(n = hle_r32(m, port + MP_MSGLIST + LH_HEAD); hle_r32(m, n + LN_SUCC); n = hle_r32(m, n + LN_SUCC))
			if(n == io)
			{
				hle_remove(m, io);
				break;
			}
	D0 = hle_io_error(m, io);
}

/* --- exec ---------------------------------------------------------------------------------- */

/* A library the HLE does not emulate: opening it succeeds, and the first call into it is fatal
 * and names it. Returns the library base, or 0 after a fatal. */
static uint32_t hle_stub_library(struct Amiga *m, uint32_t name_at)
{
	char name[64];
	uint32_t p, base;
	int k, lib;
	hle_name(m, name_at, name);
	for(k = 0; k < m->hle.stub_count; k++)
		if(!hle_strcmp(m, hle_r32(m, m->hle.stub_base[k] + LN_NAME), name))
			return m->hle.stub_base[k];
	if(m->hle.stub_count == HLE_STUBS)
	{
		amiga_fatal(m, "OpenLibrary(\"%s\"): all %d stub libraries are in use", name, HLE_STUBS);
		return 0;
	}
	p = hle_alloc(m, HLE_STUB_NEGSIZE + LIB_SIZE + 64, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!p)
	{
		amiga_fatal(m, "OpenLibrary(\"%s\"): no memory for a stub library", name);
		return 0;
	}
	base = p + HLE_STUB_NEGSIZE;
	lib = HLE_LIB_STUB + m->hle.stub_count;
	hle_jump_table(m, base, HLE_STUB_NEGSIZE, lib);
	hle_w8(m, base + LN_TYPE, NT_LIBRARY);
	hle_w32(m, base + LN_NAME, hle_string(m, base + LIB_SIZE, name));
	hle_w16(m, base + LIB_NEGSIZE, HLE_STUB_NEGSIZE);
	hle_w16(m, base + LIB_POSSIZE, LIB_SIZE);
	hle_add_tail(m, HLE_EXEC_BASE + EB_LIBLIST, base);
	amiga_logf(m, "OpenLibrary(\"%s\"): not emulated; stub library %d, every call is fatal", name, lib);
	m->hle.stub_base[m->hle.stub_count++] = base;
	return base;
}

/* --- libraries from LIBS: ----------------------------------------------------------------- */

/* InitStruct(table, memory, 0): exec autodoc, InitStruct; exec/initializers.i. */
static void hle_init_struct(struct Amiga *m, uint32_t p, uint32_t mem)
{
	uint32_t dst = mem;
	for(;;)
	{
		uint8_t cmd;
		int dd, ss, n, k;
		p = (p + 1) & ~1u;
		cmd = hle_r8(m, p++);
		if(!cmd)
			return;
		dd = cmd >> 6;
		ss = (cmd >> 4) & 3;
		n = (cmd & 15) + 1;
		if(dd == 2)
			dst = mem + hle_r8(m, p++);
		else if(dd == 3)
		{
			dst = mem + ((uint32_t)hle_r8(m, p) << 16 | (uint32_t)hle_r8(m, p + 1) << 8 | hle_r8(m, p + 2));
			p += 3;
		}
		if(ss == 3)
		{
			amiga_fatal(m, "InitStruct: source size 3 (exec would Alert)");
			return;
		}
		for(k = 0; k < (dd == 1 ? 1 : n); k++)
		{
			uint32_t v, r;
			if(ss == 0)
			{
				p = (p + 1) & ~1u;
				v = hle_r32(m, p);
				p += 4;
			}
			else if(ss == 1)
			{
				p = (p + 1) & ~1u;
				v = hle_r16(m, p);
				p += 2;
			}
			else
				v = hle_r8(m, p++);
			for(r = 0; r < (dd == 1 ? (uint32_t)n : 1u); r++)
			{
				if(ss == 0)
					hle_w32(m, dst, v);
				else if(ss == 1)
					hle_w16(m, dst, (uint16_t)v);
				else
					hle_w8(m, dst, (uint8_t)v);
				dst += ss == 0 ? 4 : ss == 1 ? 2 : 1;
			}
		}
	}
}

/* MakeLibrary(vectors, structure, -, dSize): a jump table of JMPs to the vectors (absolute
 * pointers, or word displacements from `vectors` when its first word is -1), then the data area
 * through InitStruct. Before V36 the base is not longword adjusted (exec autodoc). */
static uint32_t hle_make_library(struct Amiga *m, uint32_t vectors, uint32_t structure, uint32_t dsize)
{
	int rel = hle_r16(m, vectors) == 0xFFFF;
	uint32_t count = 0, neg, mem, base, i, at = rel ? vectors + 2 : vectors;
	while(rel ? hle_r16(m, at + 2 * count) != 0xFFFF : hle_r32(m, at + 4 * count) != 0xFFFFFFFFu)
		count++;
	neg = count * 6;
	mem = hle_alloc(m, neg + dsize, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!mem)
		return 0;
	base = mem + neg;
	for(i = 0; i < count; i++)
	{
		uint32_t fn = rel ? vectors + (uint32_t)(int32_t)(int16_t)hle_r16(m, at + 2 * i) : hle_r32(m, at + 4 * i);
		hle_w16(m, base - 6 * (i + 1), 0x4EF9);
		hle_w32(m, base - 6 * (i + 1) + 2, fn);
	}
	if(structure)
		hle_init_struct(m, structure, base);
	hle_w16(m, base + LIB_NEGSIZE, (uint16_t)neg);
	hle_w16(m, base + LIB_POSSIZE, (uint16_t)dsize);
	return base;
}

/* The rest of an OpenLibrary that went to guest code. Stage 0: the library's init routine
 * returned its base, which joins the library list. Stage 2: the library was already listed.
 * Both go on to its Open vector (D0 = version, A6 = base); stage 1: Open returned the result. */
void hle_exec_lib_continue(struct Amiga *m, struct HleFrame *f)
{
	uint32_t base = D0;
	if(f->level == 1)
	{
		m->cpu.a[6] = f->saved[1];
		m->cpu.pc = cpu_pop32(m);
		return;
	}
	if(f->level == 0)
	{
		if(!base)
		{
			amiga_fatal(m, "OpenLibrary: the init routine of the library at 0x%06X failed", f->node);
			return;
		}
		hle_w8(m, base + LN_TYPE, NT_LIBRARY);
		hle_enqueue(m, HLE_EXEC_BASE + EB_LIBLIST, base);
	}
	if(hle_r16(m, base + LIB_VERSION) < f->saved[0])
	{
		D0 = 0;
		m->cpu.a[6] = f->saved[1];
		m->cpu.pc = cpu_pop32(m);
		return;
	}
	f->level = 1;
	f->node = base;
	hle_call(m, f, base - 6);
	D0 = f->saved[0];
	m->cpu.a[6] = base;
}

/* OpenLibrary of a library exec does not hold: one already in the list, or LIBS:<name> loaded
 * through dos and set up by its ROMTag, as ramlib does. Returns 1 when guest code now runs, 0
 * when there is no such library (D0 untouched), -1 after a fatal. */
static int hle_open_disk_library(struct Amiga *m, uint32_t name_at, uint32_t version)
{
	struct HleFrame f;
	char name[64], path[80];
	uint32_t base, seg, hunk, size, rt, init, i, initfn;
	hle_name(m, name_at, name);
	memset(&f, 0, sizeof f);
	f.kind = HleFrameLibInit;
	f.saved[0] = version;
	f.saved[1] = m->cpu.a[6];
	for(base = hle_r32(m, HLE_EXEC_BASE + EB_LIBLIST + LH_HEAD); hle_r32(m, base + LN_SUCC); base = hle_r32(m, base + LN_SUCC))
	{
		int k, stub = 0;
		for(k = 0; k < m->hle.stub_count; k++)
			stub |= m->hle.stub_base[k] == base;
		if(stub || hle_strcmp(m, hle_r32(m, base + LN_NAME), name))
			continue;
		D0 = base;
		f.level = 2;
		hle_exec_lib_continue(m, &f);
		return 1;
	}
	if(!m->hle.dos.booted)
		return 0;
	snprintf(path, sizeof path, "LIBS:%s", name);
	seg = hle_dos_loadseg(m, path);
	if(m->fatal)
		return -1;
	if(!seg)
		return 0;
	hunk = (seg << 2) + 4;
	size = hle_r32(m, hunk - 8) - 8;
	for(rt = 0, i = 0; i + 0x1A <= size; i += 2)
	{
		if(hle_r16(m, hunk + i) == 0x4AFC && hle_r32(m, hunk + i + 2) == hunk + i)
		{
			rt = hunk + i;
			break;
		}
	}
	if(!rt)
	{
		amiga_fatal(m, "OpenLibrary(\"%s\"): LIBS:%s has no ROMTag in its first hunk", name, name);
		return -1;
	}
	if(!(hle_r8(m, rt + 0x0A) & 0x80))
	{
		amiga_fatal(m, "OpenLibrary(\"%s\"): a ROMTag without RTF_AUTOINIT is not implemented", name);
		return -1;
	}
	init = hle_r32(m, rt + 0x16);
	base = hle_make_library(m, hle_r32(m, init + 4), hle_r32(m, init + 8), hle_r32(m, init));
	if(!base)
	{
		amiga_fatal(m, "OpenLibrary(\"%s\"): no memory for the library", name);
		return -1;
	}
	amiga_logf(m, "OpenLibrary(\"%s\"): loaded from LIBS:, version %u", name, hle_r16(m, base + LIB_VERSION));
	f.node = base;
	initfn = hle_r32(m, init + 12);
	if(!initfn)
	{
		D0 = base;
		hle_exec_lib_continue(m, &f);
		return 1;
	}
	hle_call(m, &f, initfn);
	D0 = base;
	m->cpu.a[0] = seg;
	m->cpu.a[6] = HLE_EXEC_BASE;
	return 1;
}

int hle_exec_call(struct Amiga *m, uint32_t lvo)
{
	uint32_t eb = HLE_EXEC_BASE, t;
	switch(lvo)
	{
	case 234: /* Insert(a0 list, a1 node, a2 pred): after pred, or at the head when pred is 0 */
	{
		uint32_t pred = m->cpu.a[2] ? m->cpu.a[2] : A0 + LH_HEAD, succ = hle_r32(m, pred + LN_SUCC);
		if(!m->cpu.a[2])
			pred = A0;
		hle_w32(m, A1 + LN_SUCC, succ);
		hle_w32(m, A1 + LN_PRED, pred);
		hle_w32(m, succ + LN_PRED, A1);
		hle_w32(m, pred + LN_SUCC, A1);
		return 0;
	}
	case 240: /* AddHead(a0 list, a1 node) */
	{
		uint32_t head = hle_r32(m, A0 + LH_HEAD);
		hle_w32(m, A1 + LN_SUCC, head);
		hle_w32(m, A1 + LN_PRED, A0 + LH_HEAD);
		hle_w32(m, head + LN_PRED, A1);
		hle_w32(m, A0 + LH_HEAD, A1);
		return 0;
	}
	case 246: /* AddTail(a0 list, a1 node) */
		hle_add_tail(m, A0, A1);
		return 0;
	case 252: /* Remove(a1 node) */
		hle_remove(m, A1);
		return 0;
	case 258: /* RemHead(a0 list) */
		D0 = hle_rem_head(m, A0);
		return 0;
	case 264: /* RemTail(a0 list) */
		D0 = hle_r32(m, A0 + LH_TAILPRED);
		if(!hle_r32(m, D0 + LN_PRED))
		{
			D0 = 0;
			return 0;
		}
		hle_remove(m, D0);
		return 0;
	case 270: /* Enqueue(a0 list, a1 node) */
		hle_enqueue(m, A0, A1);
		return 0;
	case 276: /* FindName(a0 start, a1 name): the first node after `start` with that name */
	{
		char name[64];
		uint32_t n;
		hle_name(m, A1, name);
		for(n = hle_r32(m, A0 + LN_SUCC); n && hle_r32(m, n + LN_SUCC); n = hle_r32(m, n + LN_SUCC))
			if(hle_r32(m, n + LN_NAME) && !hle_strcmp(m, hle_r32(m, n + LN_NAME), name))
				break;
		D0 = n && hle_r32(m, n + LN_SUCC) ? n : 0;
		return 0;
	}
	case 624: /* CopyMem(a0 source, a1 dest, d0 size) */
	case 630: /* CopyMemQuick(a0 source, a1 dest, d0 size) */
		for(t = 0; t < D0; t++)
			hle_w8(m, A1 + t, hle_r8(m, A0 + t));
		return 0;
	case 120: /* Disable */
		hle_w16(m, 0xDFF09A, 0x4000);
		hle_w8(m, eb + EB_IDNESTCNT, (uint8_t)(hle_r8(m, eb + EB_IDNESTCNT) + 1));
		return 0;
	case 126: /* Enable */
		t = (uint8_t)(hle_r8(m, eb + EB_IDNESTCNT) - 1);
		hle_w8(m, eb + EB_IDNESTCNT, (uint8_t)t);
		if((int8_t)t < 0)
			hle_w16(m, 0xDFF09A, 0xC000);
		return 0;
	case 132: /* Forbid */
		hle_w8(m, eb + EB_TDNESTCNT, (uint8_t)(hle_r8(m, eb + EB_TDNESTCNT) + 1));
		return 0;
	case 138: /* Permit */
		hle_w8(m, eb + EB_TDNESTCNT, (uint8_t)(hle_r8(m, eb + EB_TDNESTCNT) - 1));
		return 0;
	case 150: /* SuperState: supervisor mode on the caller's stack; d0 = the old SSP */
	{
		uint32_t ret = cpu_pop32(m), old_ssp = cpu_ssp(&m->cpu);
		if(m->cpu.sr & 0x2000)
		{
			D0 = 0;
		}
		else
		{
			uint32_t sp = m->cpu.a[7];
			m->cpu.sr |= 0x2000;
			m->cpu.a[7] = sp;
			m->cpu.sp_other = sp;
			D0 = old_ssp;
		}
		m->cpu.pc = ret;
		return 1;
	}
	case 156: /* UserState(d0 = sysStack): user mode on the current stack */
	{
		uint32_t ret = cpu_pop32(m), sp = m->cpu.a[7];
		m->cpu.sr &= (uint16_t)~0x2000;
		m->cpu.a[7] = sp;
		m->cpu.sp_other = D0;
		m->cpu.pc = ret;
		return 1;
	}
	case 162: /* SetIntVector(d0 n, a1 interrupt): returns the old interrupt */
	{
		uint32_t iv = eb + EB_INTVECTS + (D0 & 15) * IV_SIZE;
		uint32_t old = hle_r32(m, iv + IV_NODE);
		hle_w32(m, iv + IV_DATA, A1 ? hle_r32(m, A1 + IS_DATA) : 0);
		hle_w32(m, iv + IV_CODE, A1 ? hle_r32(m, A1 + IS_CODE) : 0);
		hle_w32(m, iv + IV_NODE, A1);
		D0 = old;
		return 0;
	}
	case 168: /* AddIntServer(d0 n, a1 interrupt) */
	case 174: /* RemIntServer */
	{
		uint32_t iv = eb + EB_INTVECTS + (D0 & 15) * IV_SIZE, list = hle_r32(m, iv + IV_DATA);
		if(hle_r32(m, iv + IV_CODE) != HLE_SERVERS)
		{
			amiga_fatal(m, "%sIntServer on interrupt %u, which has no server list", lvo == 168 ? "Add" : "Rem", D0 & 15);
			return 0;
		}
		if(lvo == 168)
		{
			hle_enqueue(m, list, A1);
			hle_w16(m, 0xDFF09A, (uint16_t)(0x8000 | (1u << (D0 & 15))));
		}
		else
		{
			hle_remove(m, A1);
			if(!hle_r32(m, hle_r32(m, list + LH_HEAD) + LN_SUCC))
				hle_w16(m, 0xDFF09A, (uint16_t)(1u << (D0 & 15)));
		}
		return 0;
	}
	case 180: /* Cause(a1 interrupt) */
		if(hle_r8(m, A1 + LN_TYPE) != 11)
		{
			if(m->hle.softint_count >= 16)
			{
				amiga_fatal(m, "more than 16 software interrupts pending");
				return 0;
			}
			hle_w8(m, A1 + LN_TYPE, 11);
			m->hle.softint_pending[m->hle.softint_count++] = A1;
			hle_w16(m, 0xDFF09C, 0x8004);
		}
		return 0;
	case 198: /* AllocMem(d0 size, d1 requirements) */
		D0 = hle_alloc(m, D0, D1, 0);
		return 0;
	case 204: /* AllocAbs(d0 size, a1 location) */
		D0 = hle_alloc(m, D0, 0, A1);
		return 0;
	case 210: /* FreeMem(a1 block, d0 size) */
		if(A1 && D0)
			hle_free(m, A1, D0);
		return 0;
	case 216: /* AvailMem(d1 requirements): free bytes, or the largest chunk with MEMF_LARGEST */
	{
		uint32_t mh = hle_r32(m, eb + EB_MEMLIST + LH_HEAD), total = 0;
		for(; hle_r32(m, mh + LN_SUCC); mh = hle_r32(m, mh + LN_SUCC))
		{
			uint32_t c;
			if((D1 & (MEMF_CHIP | MEMF_FAST)) & ~(uint32_t)hle_r16(m, mh + MH_ATTRIBUTES))
				continue;
			if(!(D1 & 0x20000u))
			{
				total += hle_r32(m, mh + MH_FREE);
				continue;
			}
			for(c = hle_r32(m, mh + MH_FIRST); c; c = hle_r32(m, c + MC_NEXT))
				if(hle_r32(m, c + MC_BYTES) > total)
					total = hle_r32(m, c + MC_BYTES);
		}
		D0 = total;
		return 0;
	}
	case 294: /* FindTask(a1 name) */
		if(A1)
		{
			amiga_fatal(m, "FindTask by name is not implemented");
			return 0;
		}
		D0 = hle_task(m);
		return 0;
	case 306: /* SetSignal(d0 new, d1 mask) */
		t = hle_r32(m, hle_task(m) + TC_SIGRECVD);
		hle_w32(m, hle_task(m) + TC_SIGRECVD, (t & ~D1) | (D0 & D1));
		D0 = t;
		return 0;
	case 318: /* Wait(d0 set) */
		return hle_wait(m, D0, 0, 0);
	case 324: /* Signal(a1 task, d0 set) */
		hle_w32(m, A1 + TC_SIGRECVD, hle_r32(m, A1 + TC_SIGRECVD) | D0);
		return 0;
	case 330: /* AllocSignal(d0 n) */
		D0 = (uint32_t)hle_exec_alloc_signal(m, (int32_t)(int8_t)D0);
		return 0;
	case 336: /* FreeSignal(d0 n) */
		hle_exec_free_signal(m, D0);
		return 0;
	case 354: /* AddPort(a1 port) */
		hle_new_list(m, A1 + MP_MSGLIST, NT_MESSAGE);
		hle_w8(m, A1 + LN_TYPE, NT_MSGPORT);
		hle_enqueue(m, eb + EB_PORTLIST, A1);
		return 0;
	case 360: /* RemPort(a1 port) */
		hle_remove(m, A1);
		return 0;
	case 366: /* PutMsg(a0 port, a1 message) */
		hle_w8(m, A1 + LN_TYPE, NT_MESSAGE);
		hle_put_msg(m, A0, A1);
		return 0;
	case 372: /* GetMsg(a0 port) */
		D0 = hle_rem_head(m, A0 + MP_MSGLIST);
		return 0;
	case 378: /* ReplyMsg(a1 message) */
		hle_reply_msg(m, A1);
		return 0;
	case 384: /* WaitPort(a0 port): the first message, left on the port */
		D0 = hle_r32(m, A0 + MP_MSGLIST + LH_HEAD);
		if(hle_r32(m, D0 + LN_SUCC))
			return 0;
		return hle_wait(m, 1u << (hle_r8(m, A0 + MP_SIGBIT) & 31), A0, 0);
	case 96: /* FindResident(a1 name) */
		if(hle_strcmp(m, A1, "dos.library"))
		{
			char name[64];
			hle_name(m, A1, name);
			amiga_fatal(m, "FindResident(\"%s\") is not implemented", name);
			return 0;
		}
		D0 = HLE_DOS_RESIDENT;
		return 0;
	case 108: /* Alert(d7 alertNum) */
		amiga_fatal(m, "exec Alert(0x%08X)", m->cpu.d[7]);
		return 0;
	case 408: /* OldOpenLibrary(a1 name): version 0 */
		D0 = 0;
		/* fall through */
	case 552: /* OpenLibrary(a1 name, d0 version) */
	{
		uint32_t lib, want = D0;
		int builtin = 1;
		if(!hle_strcmp(m, A1, "graphics.library"))
			lib = HLE_GFX_BASE;
		else if(!hle_strcmp(m, A1, "intuition.library"))
			lib = HLE_INT_BASE;
		else if(!hle_strcmp(m, A1, "dos.library") && m->hle.dos.booted)
			lib = HLE_DOS_BASE;
		else if(!hle_strcmp(m, A1, "expansion.library"))
			lib = HLE_EXP_BASE;
		else if(!hle_strcmp(m, A1, "layers.library"))
			lib = HLE_LAYERS_BASE;
		else
		{
			int r = hle_open_disk_library(m, A1, want);
			if(r)
				return r > 0;
			lib = hle_stub_library(m, A1);
			builtin = 0;
		}
		if(!lib)
			return 0;
		/* "If the library's version is lower than the one asked for, NULL is returned" (exec
		 * autodoc, OpenLibrary). A stub has no version to compare. */
		if(builtin && hle_r16(m, lib + LIB_VERSION) < want)
		{
			D0 = 0;
			return 0;
		}
		hle_w16(m, lib + LIB_OPENCNT, (uint16_t)(hle_r16(m, lib + LIB_OPENCNT) + 1));
		D0 = lib;
		return 0;
	}
	case 414: /* CloseLibrary(a1 library) */
		if(A1)
			hle_w16(m, A1 + LIB_OPENCNT, (uint16_t)(hle_r16(m, A1 + LIB_OPENCNT) - 1));
		return 0;
	case 444: /* OpenDevice(a0 name, d0 unit, a1 ioRequest, d1 flags) */
	{
		uint32_t dev = 0, unit = 0;
		if(!hle_strcmp(m, A0, "trackdisk.device") && D0 < AMIGA_DRIVES)
		{
			dev = HLE_TD_BASE;
			unit = hle_td_units[D0];
		}
		else if(!hle_strcmp(m, A0, "input.device") && D0 == 0)
		{
			dev = HLE_IN_BASE;
			m->hle.input.open = 1;
			m->hle.input.mouse_x = (uint8_t)m->paula.mouse_x;
			m->hle.input.mouse_y = (uint8_t)m->paula.mouse_y;
			m->hle.input.lmb = m->paula.lmb;
			m->hle.input.rmb = m->paula.rmb;
		}
		else if(!hle_strcmp(m, A0, "audio.device"))
		{
			D0 = (uint32_t)(int32_t)hle_audio_open(m, A1);
			return 0;
		}
		if(!dev)
		{
			char name[64];
			hle_name(m, A0, name);
			amiga_fatal(m, "OpenDevice(\"%s\", unit %u) is not implemented", name, D0);
			return 0;
		}
		hle_w32(m, A1 + IO_DEVICE, dev);
		hle_w32(m, A1 + IO_UNIT, unit);
		hle_w8(m, A1 + IO_ERROR, 0);
		hle_w16(m, dev + LIB_OPENCNT, (uint16_t)(hle_r16(m, dev + LIB_OPENCNT) + 1));
		D0 = 0;
		return 0;
	}
	case 450: /* CloseDevice(a1 ioRequest) */
		return 0;
	case 456: /* DoIO(a1 ioRequest): a request the device completes later is waited for */
	{
		int r;
		hle_w8(m, A1 + IO_FLAGS, IOF_QUICK);
		r = hle_begin_io(m, A1);
		if(r < 0)
			return 0;
		if(r == 0)
			return hle_wait_io(m, A1);
		hle_w8(m, A1 + LN_TYPE, NT_REPLYMSG);
		D0 = hle_io_error(m, A1);
		return 0;
	}
	case 462: /* SendIO(a1 ioRequest) */
		hle_w8(m, A1 + IO_FLAGS, 0);
		hle_begin_io(m, A1);
		return 0;
	case 468: /* CheckIO(a1 ioRequest): the request if it is complete, else 0 */
		D0 = (hle_r8(m, A1 + IO_FLAGS) & IOF_QUICK) || hle_r8(m, A1 + LN_TYPE) == NT_REPLYMSG ? A1 : 0;
		return 0;
	case 474: /* WaitIO(a1 ioRequest) */
		return hle_wait_io(m, A1);
	case 480: /* AbortIO(a1 ioRequest) */
		if(hle_r32(m, A1 + IO_DEVICE) == HLE_AUDIO_BASE)
			hle_audio_abort(m, A1);
		D0 = 0;
		return 0;
	default:
		return -1;
	}
}
