#include "amiga.h"

/* The copper: MOVE, WAIT and SKIP, HRM chapter 2. It runs at colour-clock granularity against
 * the beam position of the line being drawn: an instruction is two word fetches, each in an even
 * colour clock that bitplane DMA leaves free (denise_copper_blocked), so four colour clocks
 * without contention. Colour clock 0xE0 is never the copper's. A WAIT then spends one more such
 * colour clock before it compares, and in the line's last colour clock it compares against
 * position 0, so a WAIT that has not matched by then does not match on that line (FS-UAE 3.1.66
 * custom.cpp, update_copper: COP_wait_in2, and the compare position of the last cycle;
 * copper_cant_read2: cycle maxhpos - 3; docs/runtime.md, "Timing"). */

enum CopperState
{
	CopperFetch,
	CopperWait,
	CopperStopped,
	CopperFetch2, /* the first word is fetched */
	CopperWaitIn  /* a WAIT is fetched and has not compared yet */
};

void copper_restart(struct Amiga *m, int list)
{
	m->copper.pc = list == 1 ? m->copper.lc1 : m->copper.lc2;
	m->copper.state = CopperFetch;
}

/* Beam (line, hpos) has reached the position in a WAIT or SKIP. The vertical MSB is always
 * compared; the blitter-finished bit is moot with an instant blitter. */
static int copper_reached(int line, int hpos, uint16_t ir1, uint16_t ir2)
{
	int vmask = ((ir2 >> 8) & 0x7F) | 0x80;
	int hmask = ir2 & 0xFE;
	int vb = line & 0xFF & vmask, vw = (ir1 >> 8) & vmask;
	int hb = hpos & hmask, hw = ir1 & hmask;
	if(vb != vw)
		return vb > vw;
	return hb >= hw;
}

void copper_run(struct Amiga *m, int line, int to_hpos)
{
	struct Copper *c = &m->copper;
	if((m->paula.dmacon & 0x0280) != 0x0280)
	{
		if(c->hpos < to_hpos)
			c->hpos = to_hpos;
		return;
	}
	while(c->hpos < to_hpos && !m->fatal)
	{
		if(c->state == CopperStopped)
		{
			c->hpos = to_hpos;
			return;
		}
		if(c->state == CopperWait)
		{
			int vmask = ((c->ir2 >> 8) & 0x7F) | 0x80;
			if(copper_reached(line, c->hpos < AMIGA_CCK_PER_LINE - 1 ? c->hpos : 0, c->ir1, c->ir2))
			{
				c->state = CopperFetch;
				c->hpos += 2;
				continue;
			}
			if(((line & 0xFF) & vmask) != (((c->ir1 >> 8) & 0xFF) & vmask))
			{
				c->hpos = to_hpos;
				return;
			}
			c->hpos += 2;
			continue;
		}
		if(c->hpos == AMIGA_CCK_PER_LINE - 3 || denise_copper_blocked(m, c->hpos))
		{
			c->hpos += 2;
			continue;
		}
		if(c->state == CopperWaitIn)
		{
			c->state = CopperWait;
			c->hpos += 2;
			continue;
		}
		if(c->state == CopperFetch)
		{
			c->ir1 = mem_chip16(m, c->pc);
			c->state = CopperFetch2;
			c->hpos += 2;
			continue;
		}
		c->ir2 = mem_chip16(m, c->pc + 2);
		c->pc = (c->pc + 4) & 0x1FFFFE;
		c->state = CopperFetch;
		if(!(c->ir1 & 1))
		{
			uint32_t reg = c->ir1 & 0x1FE;
			/* OCS: below 0x40 never, below 0x80 only with COPCON's danger bit. HRM. */
			if(reg < 0x40 || (reg < 0x80 && !c->danger))
			{
				amiga_log_once(m, 0x70000000u | reg, "copper stopped on a MOVE to 0x%03X", (unsigned)reg);
				c->state = CopperStopped;
				continue;
			}
			custom_write(m, reg, c->ir2, 1);
		}
		else if(!(c->ir2 & 1))
		{
			c->state = CopperWaitIn;
		}
		else if(copper_reached(line, c->hpos, c->ir1, c->ir2))
		{
			c->pc = (c->pc + 4) & 0x1FFFFE;
		}
		c->hpos += 2;
	}
}
