#include "amiga.h"

/* The two 8520 CIAs, HRM appendix F. CIA-A interrupts on INTREQ PORTS (level 2), CIA-B on
 * EXTER (level 6). Timers count the E clock, one tick per 10 CPU cycles. */

#define CIA_ICR_TA 0x01
#define CIA_ICR_TB 0x02
#define CIA_ICR_ALRM 0x04
#define CIA_ICR_SP 0x08

static void cia_assert(struct Amiga *m, struct Cia *c)
{
	if(c->icr & c->icr_mask & 0x1F)
		custom_set_intreq(m, c == &m->cia_a ? 0x0008 : 0x2000);
}

void cia_reassert(struct Amiga *m)
{
	cia_assert(m, &m->cia_a);
	cia_assert(m, &m->cia_b);
}

/* The index pulse on CIA-B's /FLAG (ICR bit 4). */
void cia_flag(struct Amiga *m)
{
	m->cia_b.icr |= 0x10;
	cia_assert(m, &m->cia_b);
}

/* Counts `ticks` down; returns how many times the timer underflowed. */
static uint32_t cia_count(struct CiaTimer *t, uint32_t ticks)
{
	uint32_t under = 0;
	while(ticks && (t->cr & 1))
	{
		uint32_t left = t->counter ? t->counter : 0x10000u;
		if(ticks < left)
		{
			t->counter = (uint16_t)(left - ticks);
			break;
		}
		ticks -= left;
		under++;
		t->counter = t->latch;
		if(t->cr & 0x08)
		{
			t->cr &= (uint8_t)~1;
			break;
		}
	}
	return under;
}

static void cia_timers(struct Amiga *m, struct Cia *c, uint32_t ticks)
{
	uint32_t ua = cia_count(&c->ta, ticks), ub;
	if(ua)
		c->icr |= CIA_ICR_TA;
	if(((c->tb.cr >> 5) & 3) == 2)
		ub = cia_count(&c->tb, ua);
	else if(((c->tb.cr >> 5) & 3) == 0)
		ub = cia_count(&c->tb, ticks);
	else
		ub = 0;
	if(ub)
		c->icr |= CIA_ICR_TB;
	if(ua || ub)
		cia_assert(m, c);
}

void cia_run(struct Amiga *m, uint32_t cpu_cycles)
{
	uint32_t ticks;
	m->cia_a.eclock_frac += cpu_cycles;
	ticks = m->cia_a.eclock_frac / 10;
	m->cia_a.eclock_frac %= 10;
	if(!ticks)
		return;
	cia_timers(m, &m->cia_a, ticks);
	cia_timers(m, &m->cia_b, ticks);
}

void cia_tod_tick(struct Amiga *m, struct Cia *c)
{
	if(c->tod_stopped)
		return;
	c->tod = (c->tod + 1) & 0xFFFFFF;
	if(c->tod == c->tod_alarm)
	{
		c->icr |= CIA_ICR_ALRM;
		cia_assert(m, c);
	}
}

/* One queued key per frame through CIA-A's serial port: the keyboard sends the code rotated
 * left with the up flag last, inverted. HRM appendix H. */
void cia_frame(struct Amiga *m)
{
	uint8_t k;
	if(m->key_head == m->key_tail)
		return;
	k = m->keys[m->key_head];
	m->key_head = (m->key_head + 1) % AMIGA_KEY_QUEUE;
	m->cia_a.sdr = (uint8_t)~(((k & 0x7F) << 1) | (k >> 7));
	m->cia_a.icr |= CIA_ICR_SP;
	cia_assert(m, &m->cia_a);
}

static uint8_t cia_port_a(const struct Amiga *m, const struct Cia *c)
{
	if(c == &m->cia_a)
	{
		/* /FIR1 7 (right button, not wired), /FIR0 6 (left mouse); /CHNG 2, /WPRO 3, /TK0 4 and
		 * /RDY 5 come from the selected drives (disk_status); bits 1 and 0 are outputs. */
		uint8_t in = (uint8_t)((0xC0 & ~(m->paula.lmb ? 0x40 : 0)) | disk_status(m) | 0x03);
		return (uint8_t)((in & ~c->ddra) | (c->pra & c->ddra));
	}
	return (uint8_t)((0xFF & ~c->ddra) | (c->pra & c->ddra));
}

uint8_t cia_read(struct Amiga *m, struct Cia *c, int reg)
{
	uint32_t tod;
	uint8_t v;
	switch(reg)
	{
	case 0x0: return cia_port_a(m, c);
	case 0x1: return (uint8_t)((0xFF & ~c->ddrb) | (c->prb & c->ddrb));
	case 0x2: return c->ddra;
	case 0x3: return c->ddrb;
	case 0x4: return (uint8_t)c->ta.counter;
	case 0x5: return (uint8_t)(c->ta.counter >> 8);
	case 0x6: return (uint8_t)c->tb.counter;
	case 0x7: return (uint8_t)(c->tb.counter >> 8);
	case 0x8:
		tod = c->tod_latched ? c->tod_latch : c->tod;
		c->tod_latched = 0;
		return (uint8_t)tod;
	case 0x9:
		tod = c->tod_latched ? c->tod_latch : c->tod;
		return (uint8_t)(tod >> 8);
	case 0xA:
		c->tod_latch = c->tod;
		c->tod_latched = 1;
		return (uint8_t)(c->tod >> 16);
	case 0xC: return c->sdr;
	case 0xD:
		v = (uint8_t)(c->icr | ((c->icr & c->icr_mask & 0x1F) ? 0x80 : 0));
		c->icr = 0;
		return v;
	case 0xE: return c->ta.cr;
	case 0xF: return c->tb.cr;
	default: return 0xFF;
	}
}

static void cia_write_tod(struct Cia *c, int shift, uint8_t v)
{
	uint32_t *t = (c->tb.cr & 0x80) ? &c->tod_alarm : &c->tod;
	*t = (*t & ~(0xFFu << shift)) | ((uint32_t)v << shift);
}

/* CIA-B PRB as the drives see it: an input pin reads high. */
static uint8_t cia_b_pins(const struct Cia *c)
{
	return (uint8_t)((c->prb & c->ddrb) | (uint8_t)~c->ddrb);
}

void cia_write(struct Amiga *m, struct Cia *c, int reg, uint8_t v)
{
	if(c == &m->cia_b && (reg == 0x1 || reg == 0x3))
	{
		uint8_t old_pins = cia_b_pins(c), new_pins;
		if(reg == 0x1)
			c->prb = v;
		else
			c->ddrb = v;
		new_pins = cia_b_pins(c);
		if(old_pins != new_pins)
			disk_prb_changed(m, old_pins, new_pins);
		return;
	}
	switch(reg)
	{
	case 0x0: c->pra = v; return;
	case 0x1: c->prb = v; return;
	case 0x2: c->ddra = v; return;
	case 0x3: c->ddrb = v; return;
	case 0x4: c->ta.latch = (uint16_t)((c->ta.latch & 0xFF00) | v); return;
	case 0x5:
		c->ta.latch = (uint16_t)((c->ta.latch & 0x00FF) | (v << 8));
		if(!(c->ta.cr & 1))
			c->ta.counter = c->ta.latch;
		if(c->ta.cr & 0x08)
		{
			c->ta.counter = c->ta.latch;
			c->ta.cr |= 1;
		}
		return;
	case 0x6: c->tb.latch = (uint16_t)((c->tb.latch & 0xFF00) | v); return;
	case 0x7:
		c->tb.latch = (uint16_t)((c->tb.latch & 0x00FF) | (v << 8));
		if(!(c->tb.cr & 1))
			c->tb.counter = c->tb.latch;
		if(c->tb.cr & 0x08)
		{
			c->tb.counter = c->tb.latch;
			c->tb.cr |= 1;
		}
		return;
	case 0x8:
		cia_write_tod(c, 0, v);
		if(!(c->tb.cr & 0x80))
			c->tod_stopped = 0;
		return;
	case 0x9: cia_write_tod(c, 8, v); return;
	case 0xA:
		cia_write_tod(c, 16, v);
		if(!(c->tb.cr & 0x80))
			c->tod_stopped = 1;
		return;
	case 0xC: c->sdr = v; return;
	case 0xD:
		if(v & 0x80)
			c->icr_mask |= v & 0x1F;
		else
			c->icr_mask &= (uint8_t)~(v & 0x1F);
		cia_assert(m, c);
		return;
	case 0xE:
		if(v & 0x10)
			c->ta.counter = c->ta.latch;
		c->ta.cr = v & (uint8_t)~0x10;
		return;
	case 0xF:
		if(v & 0x10)
			c->tb.counter = c->tb.latch;
		c->tb.cr = v & (uint8_t)~0x10;
		return;
	default:
		return;
	}
}
