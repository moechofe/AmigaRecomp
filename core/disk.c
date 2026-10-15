#include "amiga.h"
#include "game.h"

#include <stddef.h>

/* Floppy hardware: Paula's disk DMA, the drive control on CIA-B PRB and the drive status on
 * CIA-A PRA. HRM "Disk controller" chapter and appendix F (CIA pins); track layout per the ADF
 * FAQ (Laurent Clevy), "MFM track format". A disk is an ADF image, so every track is the
 * standard format: 11 sectors, and the image is the only source of data.
 *
 * Transfers are instant, like trackdisk and the blitter (docs/runtime.md, "Timing"). */

/* One revolution is 200 ms (300 rpm); at 2 us per MFM cell a 16-cell word takes 32 us, so a
 * track is 200 ms / 32 us = 6250 words. 32 us is 226.99 CPU cycles at 7 093 790 Hz. */
#define DISK_TRACK_WORDS 6250
#define DISK_WORD_CYCLES 227
/* ADF FAQ: a sector is 544 MFM words (sync and header 2 + 2 + 4 + 16 + 4 + 4, data 512). */
#define DISK_SECTOR_WORDS 544
#define DISK_SECTORS 11
#define DISK_CYLINDERS 80

/* DSKLEN, DSKPTH, DSKPTL, DSKSYNC (HRM, Appendix A). */
#define REG_DSKPTH 0x020
#define REG_DSKPTL 0x022
#define REG_DSKLEN 0x024
#define REG_DSKSYNC 0x07E

/* CIA-B PRB pin bits (HRM, "CIA Resources"): /STEP 0, DIR 1, /SIDE 2, /SEL0-3 3-6, /MTR 7. */
#define PRB_STEP 0x01
#define PRB_DIR 0x02
#define PRB_SIDE 0x04
#define PRB_MTR 0x80

/* CIA-A PRA status bits: /CHNG 2, /WPRO 3, /TK0 4, /RDY 5. */
#define PRA_CHNG 0x04
#define PRA_WPRO 0x08
#define PRA_TK0 0x10
#define PRA_RDY 0x20

/* DMACON: DMAEN 0x0200, DSKEN 0x0010. ADKCON: WORDSYNC 0x0400, FAST 0x0100. INTREQ: DSKBLK
 * 0x0002, DSKSYN 0x1000. DSKLEN: DMAEN 0x8000, WRITE 0x4000. */
#define DMACON_DISK 0x0210
#define ADKCON_WORDSYNC 0x0400
#define ADKCON_FAST 0x0100

/* One long as MFM: a clock bit sits between data bits and is set when both neighbours are 0.
 * `data` holds the data bits at the even positions (the odd or even half of a value); `*prev`
 * is the last data bit of the previous long. */
static uint32_t mfm_long(uint32_t data, uint32_t *prev)
{
	uint32_t d = data & 0x55555555u;
	uint32_t clk = ~((d << 1) | (d >> 1) | (*prev << 31)) & 0xAAAAAAAAu;
	*prev = d & 1;
	return d | clk;
}

static void put_long(uint16_t *out, int *w, uint32_t v)
{
	out[(*w)++] = (uint16_t)(v >> 16);
	out[(*w)++] = (uint16_t)v;
}

/* A value as its odd bits, then its even bits, each an MFM long. */
static void put_odd_even(uint16_t *out, int *w, uint32_t v, uint32_t *prev)
{
	put_long(out, w, mfm_long((v >> 1) & 0x55555555u, prev));
	put_long(out, w, mfm_long(v & 0x55555555u, prev));
}

static uint32_t adf_long(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Encode track `track` (cylinder * 2 + head) of `adf` as the 6250 words the head reads, the index
 * at word 0. */
static void disk_track(const uint8_t *adf, int track, uint16_t out[DISK_TRACK_WORDS])
{
	uint32_t prev = 0;
	int w = 0, s, i;

	for(s = 0; s < DISK_SECTORS; s++)
	{
		const uint8_t *data = adf + (size_t)(track * DISK_SECTORS + s) * 512;
		uint32_t info = 0xFF000000u | (uint32_t)track << 16 | (uint32_t)s << 8 | (uint32_t)(DISK_SECTORS - s);
		uint32_t io = (info >> 1) & 0x55555555u, ie = info & 0x55555555u;
		uint32_t hsum = io ^ ie, dsum = 0;

		w = s * DISK_SECTOR_WORDS;
		/* The gap before the sync, then two sync words 0x4489 (an MFM 0xA1 with a missing
		 * clock), which end in a 1. */
		put_long(out, &w, mfm_long(0, &prev));
		out[w++] = 0x4489;
		out[w++] = 0x4489;
		prev = 1;
		put_odd_even(out, &w, info, &prev);
		/* The label: 16 bytes of 0, all odd halves, then all even halves. */
		for(i = 0; i < 8; i++)
			put_long(out, &w, mfm_long(0, &prev));
		put_odd_even(out, &w, hsum, &prev);
		for(i = 0; i < 128; i++)
			dsum ^= (adf_long(data + i * 4) >> 1) & 0x55555555u;
		for(i = 0; i < 128; i++)
			dsum ^= adf_long(data + i * 4) & 0x55555555u;
		put_odd_even(out, &w, dsum & 0x55555555u, &prev);
		for(i = 0; i < 128; i++)
			put_long(out, &w, mfm_long(adf_long(data + i * 4) >> 1, &prev));
		for(i = 0; i < 128; i++)
			put_long(out, &w, mfm_long(adf_long(data + i * 4), &prev));
	}
	/* The rest of the revolution is gap. */
	w = DISK_SECTORS * DISK_SECTOR_WORDS;
	while(w < DISK_TRACK_WORDS)
		put_long(out, &w, mfm_long(0, &prev));
}

/* CIA-B PRB as the drive sees it: an input pin reads high. */
static uint8_t disk_pins(const struct Amiga *m)
{
	return (uint8_t)((m->cia_b.prb & m->cia_b.ddrb) | (uint8_t)~m->cia_b.ddrb);
}

static int disk_selected(uint8_t pins, int *drive)
{
	int n, count = 0;
	for(n = 0; n < AMIGA_DRIVES; n++)
		if(!(pins & (0x08 << n)))
		{
			count++;
			*drive = n;
		}
	return count;
}

static void disk_dma(struct Amiga *m, uint16_t v)
{
	uint8_t pins = disk_pins(m);
	uint16_t adkcon = m->paula.adkcon;
	uint16_t buf[DISK_TRACK_WORDS];
	uint32_t dest = m->floppy.dskpt;
	uint32_t len = v & 0x3FFF;
	uint32_t i, pos;
	int n = 0, count, role, head, cyl;

	if(v & 0x4000)
	{
		amiga_fatal(m, "hardware disk DMA write (DSKLEN 0x%04X) is not implemented", v);
		return;
	}
	if((m->paula.dmacon & DMACON_DISK) != DMACON_DISK)
	{
		amiga_fatal(m, "disk DMA started with DMACON 0x%04X: DMAEN or DSKEN off", m->paula.dmacon);
		return;
	}
	count = disk_selected(pins, &n);
	if(count != 1)
	{
		amiga_fatal(m, "disk DMA with %d drives selected", count);
		return;
	}
	role = m->drive_role[n];
	if(role < 0)
	{
		amiga_fatal(m, "disk DMA from DF%d, which is empty", n);
		return;
	}
	if(!m->floppy.motor[n])
	{
		amiga_fatal(m, "disk DMA from DF%d with its motor off", n);
		return;
	}
	if(!(adkcon & ADKCON_FAST))
		amiga_log_once(m, 0xD2000000u, "disk DMA with ADKCON FAST clear; read as MFM");

	head = (pins & PRB_SIDE) ? 0 : 1;
	cyl = m->floppy.cyl[n];
	disk_track(m->disks[role].image, cyl * 2 + head, buf);
	pos = (uint32_t)((amiga_now(m) / DISK_WORD_CYCLES) % DISK_TRACK_WORDS);

	if(adkcon & ADKCON_WORDSYNC)
	{
		for(i = 0; i < DISK_TRACK_WORDS; i++)
			if(buf[(pos + i) % DISK_TRACK_WORDS] == m->floppy.dsksync)
				break;
		if(i == DISK_TRACK_WORDS)
		{
			amiga_fatal(m, "disk DMA: DF%d track %d holds no sync word 0x%04X", n, cyl * 2 + head,
				m->floppy.dsksync);
			return;
		}
		pos = (pos + i + 1) % DISK_TRACK_WORDS;
		custom_set_intreq(m, 0x1000);
	}
	for(i = 0; i < len; i++)
		mem_chip_write16(m, dest + 2 * i, buf[(pos + i) % DISK_TRACK_WORDS]);
	m->floppy.dskpt = dest + 2 * len;
	m->disk_accesses++;
	custom_set_intreq(m, 0x0002);
	if(m->log)
		amiga_logf(m, "disk DMA: DF%d=%s cylinder %d head %d +%u words -> RAM 0x%06X", n,
			game_def.disks[role].id, cyl, head, len, dest);
}

void disk_write_reg(struct Amiga *m, uint32_t reg, uint16_t v)
{
	struct Floppy *f = &m->floppy;
	switch(reg)
	{
	case REG_DSKPTH:
		f->dskpt = (f->dskpt & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16);
		return;
	case REG_DSKPTL:
		f->dskpt = (f->dskpt & 0xFFFF0000u) | (v & 0xFFFE);
		return;
	case REG_DSKSYNC:
		f->dsksync = v;
		return;
	case REG_DSKLEN:
		/* HRM: DMA starts when DMAEN is written twice in a row. */
		if((v & 0x8000) && (f->dsklen & 0x8000))
		{
			disk_dma(m, v);
			f->dsklen = v & 0x7FFF;
		}
		else
			f->dsklen = v;
		return;
	default:
		return;
	}
}

void disk_prb_changed(struct Amiga *m, uint8_t old_pins, uint8_t new_pins)
{
	struct Floppy *f = &m->floppy;
	int n;
	for(n = 0; n < AMIGA_DRIVES; n++)
	{
		uint8_t sel = (uint8_t)(0x08 << n);
		/* /MTR is latched as /SELn falls (HRM "Disk drive control"). */
		if((old_pins & sel) && !(new_pins & sel))
			f->motor[n] = !(new_pins & PRB_MTR);
		/* A step pulse ends on /STEP's rising edge. DIR high steps toward cylinder 0. */
		if(!(new_pins & sel) && !(old_pins & PRB_STEP) && (new_pins & PRB_STEP))
		{
			if(new_pins & PRB_DIR)
			{
				if(f->cyl[n] > 0)
					f->cyl[n]--;
			}
			else if(f->cyl[n] < DISK_CYLINDERS - 1)
				f->cyl[n]++;
			if(m->drive_role[n] >= 0)
				f->changed[n] = 0;
		}
	}
}

/* CIA-A PRA bits 2-5 for the selected drives: each drive pulls its lines low, wired-OR. /RDY
 * with the motor on: a disk is in; with it off, the drive's serial ID, active low, of which
 * DF0–DF(drive_count-1) send the standard 3.5" drive's $FFFFFFFF and the rest, absent, $00000000
 * (HRM appendix H, "RDY-" and "Device I.D."). */
uint8_t disk_status(const struct Amiga *m)
{
	uint8_t pins = disk_pins(m);
	uint8_t bits = 0x3C;
	int n;
	for(n = 0; n < AMIGA_DRIVES; n++)
	{
		int role = m->drive_role[n];
		if(pins & (0x08 << n))
			continue;
		if(m->floppy.motor[n] ? role >= 0 : n < game_def.drive_count)
			bits &= (uint8_t)~PRA_RDY;
		if(m->floppy.cyl[n] == 0)
			bits &= (uint8_t)~PRA_TK0;
		if(role >= 0 && !m->disks[role].rw)
			bits &= (uint8_t)~PRA_WPRO;
		if(m->floppy.changed[n])
			bits &= (uint8_t)~PRA_CHNG;
	}
	return bits;
}

/* The index pulse: /FLAG on CIA-B once a revolution, while a spinning disk is selected. */
void disk_line(struct Amiga *m)
{
	uint64_t rev = m->done / ((uint64_t)DISK_TRACK_WORDS * DISK_WORD_CYCLES);
	if(rev == m->floppy.index_rev)
		return;
	m->floppy.index_rev = rev;
	{
		uint8_t pins = disk_pins(m);
		int n;
		for(n = 0; n < AMIGA_DRIVES; n++)
			if(!(pins & (0x08 << n)) && m->floppy.motor[n] && m->drive_role[n] >= 0)
			{
				cia_flag(m);
				return;
			}
	}
}
