#include "hle.h"
#include "cpu_ops.h"

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

static uint32_t hle_task(void)
{
	return HLE_THIS_TASK;
}

static int32_t hle_alloc_signal(struct Amiga *m, int32_t want)
{
	uint32_t t = hle_task(), alloc = hle_r32(m, t + TC_SIGALLOC);
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

static void hle_reply(struct Amiga *m, uint32_t msg)
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

static int hle_begin_io(struct Amiga *m, uint32_t io)
{
	uint32_t dev = hle_r32(m, io + IO_DEVICE);
	if(dev == HLE_TD_BASE)
		hle_td_io(m, io);
	else if(dev == HLE_IN_BASE)
		hle_input_io(m, io);
	else
	{
		amiga_fatal(m, "I/O on device 0x%06X, which this runtime does not provide", dev);
		return -1;
	}
	if(!(hle_r8(m, io + IO_FLAGS) & IOF_QUICK))
		hle_reply(m, io);
	return 0;
}

static uint32_t hle_io_error(struct Amiga *m, uint32_t io)
{
	return (uint32_t)(int32_t)(int8_t)hle_r8(m, io + IO_ERROR);
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

int hle_exec_call(struct Amiga *m, uint32_t lvo)
{
	uint32_t eb = HLE_EXEC_BASE, t;
	switch(lvo)
	{
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
		D0 = hle_task();
		return 0;
	case 306: /* SetSignal(d0 new, d1 mask) */
		t = hle_r32(m, hle_task() + TC_SIGRECVD);
		hle_w32(m, hle_task() + TC_SIGRECVD, (t & ~D1) | (D0 & D1));
		D0 = t;
		return 0;
	case 318: /* Wait(d0 set) */
		t = hle_r32(m, hle_task() + TC_SIGRECVD) & D0;
		if(!t)
		{
			amiga_fatal(m, "Wait(0x%08X) with none of those signals set", D0);
			return 0;
		}
		hle_w32(m, hle_task() + TC_SIGRECVD, hle_r32(m, hle_task() + TC_SIGRECVD) & ~t);
		D0 = t;
		return 0;
	case 324: /* Signal(a1 task, d0 set) */
		hle_w32(m, A1 + TC_SIGRECVD, hle_r32(m, A1 + TC_SIGRECVD) | D0);
		return 0;
	case 330: /* AllocSignal(d0 n) */
		D0 = (uint32_t)hle_alloc_signal(m, (int32_t)(int8_t)D0);
		return 0;
	case 336: /* FreeSignal(d0 n) */
		if((int32_t)D0 >= 0 && D0 < 32)
			hle_w32(m, hle_task() + TC_SIGALLOC, hle_r32(m, hle_task() + TC_SIGALLOC) & ~(1u << D0));
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
		hle_reply(m, A1);
		return 0;
	case 384: /* WaitPort(a0 port) */
		D0 = hle_r32(m, A0 + MP_MSGLIST + LH_HEAD);
		if(!hle_r32(m, D0 + LN_SUCC))
		{
			amiga_fatal(m, "WaitPort(0x%06X) on an empty port", A0);
			return 0;
		}
		return 0;
	case 408: /* OldOpenLibrary(a1 name) */
	case 552: /* OpenLibrary(a1 name, d0 version) */
	{
		uint32_t lib;
		if(!hle_strcmp(m, A1, "graphics.library"))
			lib = HLE_GFX_BASE;
		else if(!hle_strcmp(m, A1, "intuition.library"))
			lib = HLE_INT_BASE;
		else
			lib = hle_stub_library(m, A1);
		if(!lib)
			return 0;
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
	case 456: /* DoIO(a1 ioRequest) */
		hle_w8(m, A1 + IO_FLAGS, IOF_QUICK);
		if(hle_begin_io(m, A1) < 0)
			return 0;
		hle_w8(m, A1 + LN_TYPE, NT_REPLYMSG);
		D0 = hle_io_error(m, A1);
		return 0;
	case 462: /* SendIO(a1 ioRequest) */
		hle_w8(m, A1 + IO_FLAGS, 0);
		hle_begin_io(m, A1);
		return 0;
	case 468: /* CheckIO(a1 ioRequest) */
		D0 = A1;
		return 0;
	case 474: /* WaitIO(a1 ioRequest) */
		if(!(hle_r8(m, A1 + IO_FLAGS) & IOF_QUICK) && hle_r8(m, A1 + LN_TYPE) == NT_REPLYMSG)
		{
			uint32_t port = hle_r32(m, A1 + MN_REPLYPORT), n;
			for(n = hle_r32(m, port + MP_MSGLIST + LH_HEAD); hle_r32(m, n + LN_SUCC); n = hle_r32(m, n + LN_SUCC))
			{
				if(n == A1)
				{
					hle_remove(m, A1);
					break;
				}
			}
		}
		D0 = hle_io_error(m, A1);
		return 0;
	case 480: /* AbortIO(a1 ioRequest) */
		D0 = 0;
		return 0;
	default:
		return -1;
	}
}
