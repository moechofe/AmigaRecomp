#include "hle.h"
#include "game.h"

/* Start the bootblock in the state Kickstart 1.3 hands it: docs/runtime.md, "Kickstart
 * handoff". */

#define BOOT_BUFFER 0x001558u
#define BOOT_BUFFER_SIZE 0x488u
#define BOOT_ENTRY (BOOT_BUFFER + 12)

/* Kickstart's cia.resource, keyboard.device and timer.device CIA state, measured at the
 * bootblock call and at stage 2's entry; only the times of day differ (docs/runtime.md,
 * "Kickstart handoff"). */
static void boot_cias(struct Amiga *m, uint32_t tod_a, uint32_t tod_b)
{
	m->cia_a.icr_mask = 0x0A;
	m->cia_a.tb.cr = 0x08;
	m->cia_a.ta.latch = m->cia_a.ta.counter = 0xFFFF;
	m->cia_a.tb.latch = m->cia_a.tb.counter = 0x02CB;
	m->cia_a.ddra = 0x03;
	m->cia_a.pra = 0xC0;
	m->cia_a.sdr = 0x3E;
	m->cia_a.tod = tod_a;
	m->cia_b.tb.cr = 0x80;
	m->cia_b.icr = 0x10;
	m->cia_b.icr_mask = 0x04;
	m->cia_b.ta.latch = m->cia_b.ta.counter = 0xFFFF;
	m->cia_b.tb.latch = m->cia_b.tb.counter = 0xFFFF;
	m->cia_b.pra = 0xFF;
	m->cia_b.prb = 0xFF;
	m->cia_b.ddra = 0xC0;
	m->cia_b.ddrb = 0xFF;
	m->cia_b.tod = tod_b;
}

int amiga_boot(struct Amiga *m)
{
	int role = m->drive_role[0];
	interp_init(m);
	hle_build(m);
	boot_cias(m, 0x64, 0x6B);

	m->paula.intena = 0x602C;
	m->paula.dmacon = 0x02D0;
	m->paula.intreq = 0x1000;

	m->cpu.sr = 0x0000;
	m->cpu.z = 1;
	m->cpu.a[7] = HLE_BOOT_USP;
	m->cpu.sp_other = HLE_SSP;
	m->cpu.a[1] = HLE_BOOT_IOREQ;
	m->cpu.a[6] = HLE_EXEC_BASE;

	if(game_def.hack_count > AMIGA_HACKS)
	{
		amiga_fatal(m, "game_def has %u hacks; the runtime holds %d", (unsigned)game_def.hack_count, AMIGA_HACKS);
		return -1;
	}
	if(role < 0 || !m->disks[role].image)
	{
		amiga_fatal(m, "DF0 is empty: nothing to boot");
		return -1;
	}
	/* Kickstart's read of the bootblock, logged like every other load. */
	m->loads[0].drive = 0;
	m->loads[0].offset = 0;
	m->loads[0].length = 0x400;
	m->loads[0].dest = BOOT_BUFFER;
	m->load_count = 1;
	m->load_reads = 1;
	mem_dma_load(m, BOOT_BUFFER, m->disks[role].image, 0x400);

	/* The JSR into the block has already pushed its return address at the A7 Kickstart left it with. */
	mem_write32(m, HLE_BOOT_USP, HLE_BOOT_RETURN);
	m->cpu.pc = BOOT_ENTRY;
	m->booted = 1;
	return m->fatal ? -1 : 0;
}

/* The bootblock returned. Kickstart's strap frees the boot buffer and jumps to a0 with A6 =
 * ExecBase (docs/runtime.md, "Kickstart handoff"). The free lists are whatever the HLE
 * allocator holds; no register is set that the strap does not define. */
void boot_return(struct Amiga *m)
{
	struct Cpu *c = &m->cpu;
	if(c->d[0] != 0)
	{
		amiga_fatal(m, "bootblock returned d0 = 0x%08X", c->d[0]);
		return;
	}
	hle_free(m, BOOT_BUFFER, BOOT_BUFFER_SIZE);
	c->pc = c->a[0];
	c->a[6] = HLE_EXEC_BASE;

	/* The strap turned bitplane DMA on (HRM, DMACON). */
	m->paula.dmacon |= 0x0100;

	boot_cias(m, 0x7A, 0x3E);
}
