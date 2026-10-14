#include "amiga.h"


#define CIA_REGION(a) (((a) & 0xFF0000u) == 0xBF0000u)
#define CUSTOM_REGION(a) (((a) & 0xFFF000u) == 0xDFF000u)


/* Pointer to RAM at a 24-bit address, or NULL if it is not RAM. Chip mirrors fold onto
 * AMIGA_CHIP_SIZE. */
uint8_t *mem_ram(struct Amiga *m, uint32_t addr)
{
	addr &= 0xFFFFFFu;
	if(addr < 0x200000u)
	{
		if(addr >= AMIGA_CHIP_SIZE)
			amiga_log_once(m, 0x10000000u | (addr >> 16), "chip RAM mirror access at 0x%06X", addr);
		return &m->chip[addr & (AMIGA_CHIP_SIZE - 1)];
	}
	if(addr >= AMIGA_SLOW_BASE && addr < AMIGA_SLOW_BASE + AMIGA_SLOW_SIZE)
		return &m->slow[addr - AMIGA_SLOW_BASE];
	return 0;
}

static uint32_t mem_canon(uint32_t addr)
{
	addr &= 0xFFFFFFu;
	if(addr < 0x200000u)
		return addr & (AMIGA_CHIP_SIZE - 1);
	return addr;
}

/* --- I/O ----------------------------------------------------------------------------------- */

static uint8_t mem_io_read8(struct Amiga *m, uint32_t addr)
{
	if(CIA_REGION(addr))
	{
		int reg = (addr >> 8) & 15;
		sched_catch_up(m, amiga_now(m));
		if((addr & 1) && !(addr & 0x1000))
			return cia_read(m, &m->cia_a, reg);
		if(!(addr & 1) && !(addr & 0x2000))
			return cia_read(m, &m->cia_b, reg);
		return 0xFF;
	}
	if(CUSTOM_REGION(addr))
	{
		uint16_t w;
		sched_catch_up(m, amiga_now(m));
		w = custom_read(m, addr & 0x1FE);
		return (addr & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
	}
	if(addr >= HLE_BASE && addr < HLE_END)
		return 0;
	amiga_log_once(m, 0x20000000u | (addr >> 12), "read from unmapped 0x%06X", addr);
	return 0;
}

static uint16_t mem_io_read16(struct Amiga *m, uint32_t addr)
{
	if(CUSTOM_REGION(addr) && !(addr & 1))
	{
		sched_catch_up(m, amiga_now(m));
		return custom_read(m, addr & 0x1FE);
	}
	return (uint16_t)((mem_io_read8(m, addr) << 8) | mem_io_read8(m, (addr + 1) & 0xFFFFFFu));
}

static void mem_io_write8(struct Amiga *m, uint32_t addr, uint8_t v)
{
	if(CIA_REGION(addr))
	{
		int reg = (addr >> 8) & 15;
		sched_catch_up(m, amiga_now(m));
		if((addr & 1) && !(addr & 0x1000))
			cia_write(m, &m->cia_a, reg, v);
		if(!(addr & 1) && !(addr & 0x2000))
			cia_write(m, &m->cia_b, reg, v);
		return;
	}
	if(CUSTOM_REGION(addr))
	{
		/* A byte write drives both halves of the data bus. */
		sched_catch_up(m, amiga_now(m));
		custom_write(m, addr & 0x1FE, (uint16_t)((v << 8) | v), 0);
		return;
	}
	amiga_log_once(m, 0x30000000u | (addr >> 12), "write to unmapped 0x%06X", addr);
}

static void mem_io_write16(struct Amiga *m, uint32_t addr, uint16_t v)
{
	if(CUSTOM_REGION(addr) && !(addr & 1))
	{
		sched_catch_up(m, amiga_now(m));
		custom_write(m, addr & 0x1FE, v, 0);
		return;
	}
	/* A word write to the CIAs reaches whichever chip is wired to each byte lane. */
	mem_io_write8(m, addr, (uint8_t)(v >> 8));
	mem_io_write8(m, (addr + 1) & 0xFFFFFFu, (uint8_t)v);
}

/* --- the bus -------------------------------------------------------------------------------- */
uint8_t mem_read8(struct Amiga *m, uint32_t addr)
{
	uint8_t *p = mem_ram(m, addr);
	if(p)
		return *p;
	return mem_io_read8(m, addr & 0xFFFFFFu);
}

uint16_t mem_read16(struct Amiga *m, uint32_t addr)
{
	uint8_t *p;
	addr &= 0xFFFFFFu;
	if(addr & 1)
		amiga_log_once(m, 0x40000000u, "word access at odd address 0x%06X", addr);
	p = mem_ram(m, addr);
	if(p && (mem_canon(addr) & 0xFFFF) != 0xFFFF)
		return (uint16_t)((p[0] << 8) | p[1]);
	if(p)
		return (uint16_t)((mem_read8(m, addr) << 8) | mem_read8(m, addr + 1));
	return mem_io_read16(m, addr);
}

uint32_t mem_read32(struct Amiga *m, uint32_t addr)
{
	return ((uint32_t)mem_read16(m, addr) << 16) | mem_read16(m, addr + 2);
}

void mem_write8(struct Amiga *m, uint32_t addr, uint8_t v)
{
	uint8_t *p = mem_ram(m, addr);
	if(p)
	{
		*p = v;
		return;
	}
	mem_io_write8(m, addr & 0xFFFFFFu, v);
}

void mem_write16(struct Amiga *m, uint32_t addr, uint16_t v)
{
	uint8_t *p;
	addr &= 0xFFFFFFu;
	if(addr & 1)
		amiga_log_once(m, 0x40000000u, "word access at odd address 0x%06X", addr);
	p = mem_ram(m, addr);
	if(p && (mem_canon(addr) & 0xFFFF) != 0xFFFF)
	{
		p[0] = (uint8_t)(v >> 8);
		p[1] = (uint8_t)v;
		return;
	}
	if(p)
	{
		mem_write8(m, addr, (uint8_t)(v >> 8));
		mem_write8(m, addr + 1, (uint8_t)v);
		return;
	}
	mem_io_write16(m, addr, v);
}

void mem_write32(struct Amiga *m, uint32_t addr, uint32_t v)
{
	mem_write16(m, addr, (uint16_t)(v >> 16));
	mem_write16(m, addr + 2, (uint16_t)v);
}

/* Chip-bus DMA: blitter, copper, Denise and Paula only see chip RAM. */
uint16_t mem_chip16(struct Amiga *m, uint32_t addr)
{
	addr &= AMIGA_CHIP_SIZE - 2;
	return (uint16_t)((m->chip[addr] << 8) | m->chip[addr + 1]);
}

void mem_chip_write16(struct Amiga *m, uint32_t addr, uint16_t v)
{
	addr &= AMIGA_CHIP_SIZE - 2;
	m->chip[addr] = (uint8_t)(v >> 8);
	m->chip[addr + 1] = (uint8_t)v;
}

/* A disk transfer into RAM. */
void mem_dma_load(struct Amiga *m, uint32_t dest, const uint8_t *src, uint32_t len)
{
	uint32_t i;
	for(i = 0; i < len; i++)
	{
		uint32_t a = (dest + i) & 0xFFFFFFu;
		uint8_t *p = mem_ram(m, a);
		if(!p)
		{
			amiga_fatal(m, "disk transfer to non-RAM address 0x%06X", a);
			return;
		}
		*p = src[i];
	}
}
