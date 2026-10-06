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

/* Musashi's 68000 cycle tables: m68ki_cycles[0] per opcode (generated m68kops.c) and
 * m68ki_exception_cycle_table[0] per vector (m68kcpu.c). */
extern unsigned char m68ki_cycles[][0x10000];
extern const unsigned char m68ki_exception_cycle_table[][256];

#define CPU_CYC(op) ((uint64_t)m68ki_cycles[0][(op)])

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

/* Autovectored interrupt at `level`: vector 24 + level, mask raised to the level, and a
 * stopped CPU resumes. */
static inline void cpu_interrupt(struct Amiga *m, int level)
{
	uint16_t old = cpu_get_sr(&m->cpu);
	cpu_set_sr(&m->cpu, (uint16_t)(((old | 0x2000) & 0x78FF) | (level << 8)));
	cpu_push32(m, m->cpu.pc);
	cpu_push16(m, old);
	m->cpu.pc = mem_read32(m, (uint32_t)(24 + level) * 4);
	m->cpu.stopped = 0;
	m->cycles += m68ki_exception_cycle_table[0][24 + level];
}

static inline void cpu_rte(struct Amiga *m)
{
	uint16_t sr = cpu_pop16(m);
	uint32_t pc = cpu_pop32(m);
	cpu_set_sr(&m->cpu, sr);
	m->cpu.pc = pc;
}

#endif
