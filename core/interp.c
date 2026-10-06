#include "amiga.h"

#include "m68k.h"
#include "m68kcpu.h"

/* Musashi is a global CPU; this is the one machine it runs against. Its IRQ line is never
 * used: interrupts are taken by cpu_interrupt() so both execution paths share them. */

static struct Amiga *interp_m;

unsigned int m68k_read_memory_8(unsigned int address)
{
	return mem_read8(interp_m, address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
	return mem_read16(interp_m, address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
	return mem_read32(interp_m, address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	mem_write8(interp_m, address, (uint8_t)value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	mem_write16(interp_m, address, (uint16_t)value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	mem_write32(interp_m, address, value);
}

static unsigned int interp_peek16(unsigned int address)
{
	uint8_t *p = mem_ram(interp_m, address);
	return p ? (unsigned int)((p[0] << 8) | p[1]) : 0;
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
	uint8_t *p = mem_ram(interp_m, address);
	return p ? *p : 0;
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
	return interp_peek16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
	return (interp_peek16(address) << 16) | interp_peek16(address + 2);
}

/* The bus time of an I/O access: the end of the instruction making it. A register a 68000
 * reads is read in the last bus cycles of the instruction, so a level change during the
 * instruction is seen by it and the interrupt is taken at the boundary after. */
uint64_t amiga_now(const struct Amiga *m)
{
	return m->cycles + (m->in_interp ? (uint64_t)CYC_INSTRUCTION[REG_IR] : 0);
}

void interp_init(struct Amiga *m)
{
	interp_m = m;
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	/* pulse_reset leaves 40 reset cycles that the next m68k_execute would spend instead of
	 * running an instruction. */
	RESET_CYCLES = 0;
}

static void interp_sync_in(struct Amiga *m)
{
	const struct Cpu *c = &m->cpu;
	int s = (c->sr >> 13) & 1;
	int i;
	m68k_set_reg(M68K_REG_SR, (unsigned int)((c->sr & 0xA700) | (c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->c));
	m68k_set_reg(M68K_REG_USP, s ? c->sp_other : c->a[7]);
	m68k_set_reg(M68K_REG_ISP, s ? c->a[7] : c->sp_other);
	for(i = 0; i < 8; i++)
		m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), c->d[i]);
	for(i = 0; i < 7; i++)
		m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), c->a[i]);
	m68k_set_reg(M68K_REG_PC, c->pc);
	if(c->stopped)
		m68ki_cpu.stopped |= STOP_LEVEL_STOP;
	else
		m68ki_cpu.stopped &= ~STOP_LEVEL_STOP;
}

static void interp_sync_out(struct Amiga *m)
{
	struct Cpu *c = &m->cpu;
	unsigned int sr = m68k_get_reg(0, M68K_REG_SR);
	uint32_t usp = m68k_get_reg(0, M68K_REG_USP), isp = m68k_get_reg(0, M68K_REG_ISP);
	int i;
	c->sr = (uint16_t)(sr & 0xA700);
	c->x = (sr >> 4) & 1;
	c->n = (sr >> 3) & 1;
	c->z = (sr >> 2) & 1;
	c->v = (sr >> 1) & 1;
	c->c = sr & 1;
	for(i = 0; i < 8; i++)
		c->d[i] = m68k_get_reg(0, (m68k_register_t)(M68K_REG_D0 + i));
	for(i = 0; i < 7; i++)
		c->a[i] = m68k_get_reg(0, (m68k_register_t)(M68K_REG_A0 + i));
	c->a[7] = (sr & 0x2000) ? isp : usp;
	c->sp_other = (sr & 0x2000) ? usp : isp;
	c->pc = m68k_get_reg(0, M68K_REG_PC);
	c->stopped = (m68ki_cpu.stopped & STOP_LEVEL_STOP) != 0;
}

/* Runs Musashi one instruction at a time until the PC enters the HLE region, an interrupt is
 * due (asserted above the mask since before the last instruction began), the CPU stops,
 * `max_insns` have run, or the cycle count reaches `until`. */
void interp_run(struct Amiga *m, uint32_t max_insns, uint64_t until)
{
	uint32_t n;
	interp_sync_in(m);
	for(n = 0; n < max_insns && m->cycles < until && !m->fatal; n++)
	{
		unsigned int pc = m68k_get_reg(0, M68K_REG_PC) & 0xFFFFFFu;
		int mask = (int)((m68k_get_reg(0, M68K_REG_SR) >> 8) & 7), ipl;
		int used;
		if(pc >= HLE_BASE && pc < HLE_END)
			break;
		sched_catch_up(m, m->cycles);
		ipl = custom_ipl(m);
		if(ipl > mask && m->cpu.ipl_prev > mask)
			break;
		m->cpu.ipl_prev = (uint8_t)ipl;
		m->in_interp = 1;
		used = m68k_execute(1);
		m->in_interp = 0;
		m->cycles += (uint64_t)used;
		m->insns++;
		if(m68ki_cpu.stopped)
			break;
	}
	interp_sync_out(m);
}
