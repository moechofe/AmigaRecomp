#ifndef CPU_OPS_H
#define CPU_OPS_H

/* Exception processing and stack helpers for the HLE and the frame loop, so that an
 * interrupt the runtime delivers is taken exactly as Musashi takes one.
 *
 * Cycle counts follow Musashi (m68k_in.c and m68kcpu.c). Musashi is
 *
 *   Copyright Karl Stenerud. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this
 *   software and associated documentation files (the "Software"), to deal in the Software
 *   without restriction, including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
 *   to whom the Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies or
 *   substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 *   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 *   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 *   FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *   DEALINGS IN THE SOFTWARE. */

#include "amiga.h"
#include "m68k.h"

/* Musashi's cycle tables for the machine's CPU (AMIGA_CPU_TABLE): m68ki_cycles per opcode
 * (generated m68kops.c) and m68ki_exception_cycle_table per vector (m68kcpu.c), in CPU clocks. */
extern unsigned char m68ki_cycles[][0x10000];
extern const unsigned char m68ki_exception_cycle_table[][256];

/* An opcode's cost in time-base cycles (core/amiga.h, AMIGA_CPU_SHIFT). */
#define CPU_CYC(op) ((uint64_t)m68ki_cycles[AMIGA_CPU_TABLE][(op)] >> AMIGA_CPU_SHIFT)

static inline uint16_t cpu_get_sr(const struct Cpu *c)
{
	return (uint16_t)((c->sr & 0xA700) | (c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->c);
}

/* Sets the whole SR, swapping A7 with the inactive stack pointer on an S change. */
static inline void cpu_set_sr(struct Cpu *c, uint16_t sr)
{
	int was_s = (c->sr >> 13) & 1;
	int now_s = (sr >> 13) & 1;
	c->sr = sr & 0xA700;
	c->x = (sr >> 4) & 1;
	c->n = (sr >> 3) & 1;
	c->z = (sr >> 2) & 1;
	c->v = (sr >> 1) & 1;
	c->c = sr & 1;
	if(was_s != now_s)
	{
		uint32_t t = c->a[7];
		c->a[7] = c->sp_other;
		c->sp_other = t;
	}
}

static inline uint32_t cpu_ssp(const struct Cpu *c)
{
	return (c->sr & 0x2000) ? c->a[7] : c->sp_other;
}

static inline void cpu_push16(struct Amiga *m, uint16_t v)
{
	m->cpu.a[7] -= 2;
	mem_write16(m, m->cpu.a[7], v);
}

static inline void cpu_push32(struct Amiga *m, uint32_t v)
{
	m->cpu.a[7] -= 4;
	mem_write32(m, m->cpu.a[7], v);
}

static inline uint16_t cpu_pop16(struct Amiga *m)
{
	uint16_t v = mem_read16(m, m->cpu.a[7]);
	m->cpu.a[7] += 2;
	return v;
}

static inline uint32_t cpu_pop32(struct Amiga *m)
{
	uint32_t v = mem_read32(m, m->cpu.a[7]);
	m->cpu.a[7] += 4;
	return v;
}

/* An exception's stack frame: SR and PC, and on the 68020 below them the format-0 word, the
 * vector offset (MC68020 User's Manual, "Exception Stack Frame Formats"). */
static inline void cpu_push_frame(struct Amiga *m, uint16_t sr, uint32_t pc, int vector)
{
	if(AMIGA_A1200)
		cpu_push16(m, (uint16_t)((vector * 4) & 0x0FFF));
	cpu_push32(m, pc);
	cpu_push16(m, sr);
}

/* Autovectored interrupt at `level`: vector 24 + level from the vector base (0 on the 68000;
 * the 68020's VBR), mask raised to the level, and a stopped CPU resumes. */
static inline void cpu_interrupt(struct Amiga *m, int level)
{
	uint16_t old = cpu_get_sr(&m->cpu);
	uint32_t vbr = AMIGA_A1200 ? m68k_get_reg(0, M68K_REG_VBR) : 0;
	cpu_set_sr(&m->cpu, (uint16_t)(((old | 0x2000) & 0x78FF) | (level << 8)));
	cpu_push_frame(m, old, m->cpu.pc, 24 + level);
	m->cpu.pc = mem_read32(m, vbr + (uint32_t)(24 + level) * 4);
	m->cpu.stopped = 0;
	m->cycles += (uint64_t)m68ki_exception_cycle_table[AMIGA_CPU_TABLE][24 + level] >> AMIGA_CPU_SHIFT;
}

/* RTE from a frame cpu_push_frame built; on the 68020 only format 0 is taken. */
static inline void cpu_rte(struct Amiga *m)
{
	uint16_t sr = cpu_pop16(m);
	uint32_t pc = cpu_pop32(m);
	if(AMIGA_A1200)
	{
		uint16_t format = cpu_pop16(m);
		if(format >> 12)
			amiga_fatal(m, "RTE from a format %u stack frame is not implemented", (unsigned)(format >> 12));
	}
	cpu_set_sr(&m->cpu, sr);
	m->cpu.pc = pc;
}

#endif
