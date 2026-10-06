#include "game.h"
#include "hle.h"

/* trackdisk.device units 0-3, commands per NDK 3.2 devices/trackdisk.i and exec/io.i. Unit n
 * reads drive DFn, which holds the role m->drive_role[n] names. Reads and writes are instant
 * copies to and from the image; every read is logged. */

#define TDU_COUNTER 0x3C
#define TDERR_NotSpecified 20
#define TDERR_WriteProt 28
#define TDERR_DiskChanged 29

const uint32_t hle_td_units[AMIGA_DRIVES] = {HLE_TD_UNIT0, HLE_TD_UNIT1, HLE_TD_UNIT2, HLE_TD_UNIT3};

static int hle_td_drive(struct Amiga *m, uint32_t io)
{
	uint32_t unit = hle_r32(m, io + IO_UNIT);
	int n;
	for(n = 0; n < AMIGA_DRIVES; n++)
		if(unit == hle_td_units[n])
			return n;
	amiga_fatal(m, "trackdisk: IORequest 0x%06X names unit 0x%06X, which is not a drive", io, unit);
	return 0;
}

/* Identical reads (the same track to the same buffer) share one load record, so the load log
 * holds each distinct transfer once. */
static void hle_td_record(struct Amiga *m, int drive, uint32_t off, uint32_t len, uint32_t dest)
{
	int i;
	struct LoadRecord *r;
	for(i = 0; i < m->load_count; i++)
	{
		r = &m->loads[i];
		if(r->drive == drive && r->offset == off && r->length == len && r->dest == dest)
			return;
	}
	if(m->load_count >= AMIGA_LOAD_LOG)
	{
		amiga_log_once(m, 0xD1000000u, "load log full; later loads are not recorded");
		return;
	}
	r = &m->loads[m->load_count++];
	r->drive = (uint8_t)drive;
	r->offset = off;
	r->length = len;
	r->dest = dest;
}

static void hle_td_read(struct Amiga *m, uint32_t io, int drive, uint32_t off, uint32_t len, uint32_t dest)
{
	int role = m->drive_role[drive];
	const uint8_t *img = m->disks[role].image;
	if(!img || off > AMIGA_ADF_SIZE || len > AMIGA_ADF_SIZE - off)
	{
		hle_w8(m, io + IO_ERROR, TDERR_NotSpecified);
		return;
	}
	hle_td_record(m, drive, off, len, dest);
	m->load_reads++;
	mem_dma_load(m, dest, img + off, len);
	if(m->log)
		amiga_logf(m, "load %d: DF%d=%s ADF 0x%06X +0x%05X -> RAM 0x%06X", m->load_reads, drive,
			game_def.disks[role].id, off, len, dest);
	hle_w32(m, io + IO_ACTUAL, len);
}

static void hle_td_write(struct Amiga *m, uint32_t io, int drive, uint32_t off, uint32_t len, uint32_t src)
{
	struct AmigaDisk *d = &m->disks[m->drive_role[drive]];
	uint32_t i;
	if(!d->rw)
	{
		hle_w8(m, io + IO_ERROR, TDERR_WriteProt);
		return;
	}
	if(off > AMIGA_ADF_SIZE || len > AMIGA_ADF_SIZE - off)
	{
		hle_w8(m, io + IO_ERROR, TDERR_NotSpecified);
		return;
	}
	for(i = 0; i < len; i++)
		d->rw[off + i] = mem_read8(m, src + i);
	d->dirty = 1;
	hle_w32(m, io + IO_ACTUAL, len);
}

void hle_td_io(struct Amiga *m, uint32_t io)
{
	uint16_t cmd = hle_r16(m, io + IO_COMMAND);
	uint32_t len = hle_r32(m, io + IO_LENGTH), data = hle_r32(m, io + IO_DATA), off = hle_r32(m, io + IO_OFFSET);
	int drive = hle_td_drive(m, io);
	int role = m->drive_role[drive];
	uint32_t changes = m->hle.change_count[drive];
	hle_w8(m, io + IO_ERROR, 0);
	hle_w32(m, io + IO_ACTUAL, 0);
	/* An empty drive fails every command but the two that report it. */
	if(role < 0)
	{
		switch(cmd & 0x7FFF)
		{
		case 13: /* TD_CHANGENUM */
			hle_w32(m, io + IO_ACTUAL, changes);
			break;
		case 14: /* TD_CHANGESTATE: nonzero = no disk (NDK devices/trackdisk.i) */
			hle_w32(m, io + IO_ACTUAL, 1);
			break;
		default:
			hle_w8(m, io + IO_ERROR, TDERR_DiskChanged);
			break;
		}
		return;
	}
	/* An extended command fails once the disk has changed since iotd_Count. */
	if((cmd & 0x8000) && hle_r32(m, io + IOTD_COUNT) < changes)
	{
		hle_w8(m, io + IO_ERROR, TDERR_DiskChanged);
		return;
	}
	switch(cmd & 0x7FFF)
	{
	case 2: /* CMD_READ */
		hle_td_read(m, io, drive, off, len, data);
		return;
	case 3: /* CMD_WRITE */
	case 11: /* TD_FORMAT */
		hle_td_write(m, io, drive, off, len, data);
		return;
	case 4: /* CMD_UPDATE */
		if(m->disks[role].rw)
			m->disks[role].dirty = 1;
		return;
	case 5: /* CMD_CLEAR */
	case 10: /* TD_SEEK */
		return;
	case 9: /* TD_MOTOR */
		hle_w32(m, io + IO_ACTUAL, m->hle.motor);
		m->hle.motor = len != 0;
		return;
	case 13: /* TD_CHANGENUM */
		hle_w32(m, io + IO_ACTUAL, changes);
		return;
	case 14: /* TD_CHANGESTATE: 0 = a disk is in */
		return;
	case 15: /* TD_PROTSTATUS: nonzero = write-protected */
		hle_w32(m, io + IO_ACTUAL, m->disks[role].rw ? 0 : 1);
		return;
	case 18: /* TD_GETDRIVETYPE: DRIVE3_5 */
		hle_w32(m, io + IO_ACTUAL, 1);
		return;
	case 19: /* TD_GETNUMTRACKS */
		hle_w32(m, io + IO_ACTUAL, 160);
		return;
	case 20: /* TD_ADDCHANGEINT */
	case 21: /* TD_REMCHANGEINT */
		amiga_log_once(m, 0xD0000000u | cmd, "TD_%sCHANGEINT stored but never fired", cmd == 20 ? "ADD" : "REM");
		return;
	default:
		amiga_fatal(m, "trackdisk command %u (0x%04X) is not implemented", cmd & 0x7FFF, cmd);
		return;
	}
}

/* Put `role` (-1: none) in DFn. Once the machine has booted the unit's change counter moves
 * and the new count is written where a caller reads it (io_Unit's tdu_Counter). */
void amiga_insert(struct Amiga *m, int drive, int role)
{
	if(drive < 0 || drive >= AMIGA_DRIVES)
	{
		amiga_fatal(m, "DF%d: the machine has %d drives", drive, AMIGA_DRIVES);
		return;
	}
	if(role < -1 || role >= (int)game_def.disk_count || (role >= 0 && !m->disks[role].image))
	{
		amiga_fatal(m, "DF%d: role %d has no image", drive, role);
		return;
	}
	m->drive_role[drive] = (int8_t)role;
	if(m->booted)
	{
		m->hle.change_count[drive]++;
		hle_w32(m, hle_td_units[drive] + TDU_COUNTER, m->hle.change_count[drive]);
	}
}

/* The device's own vectors, for a caller that skips exec: BeginIO -30, AbortIO -36
 * (exec/devices.i). */
int hle_td_call(struct Amiga *m, uint32_t lvo)
{
	uint32_t io = m->cpu.a[1];
	switch(lvo)
	{
	case 30:
		hle_td_io(m, io);
		if(!(hle_r8(m, io + IO_FLAGS) & IOF_QUICK))
		{
			uint32_t port = hle_r32(m, io + MN_REPLYPORT);
			hle_w8(m, io + LN_TYPE, NT_REPLYMSG);
			if(port)
				hle_put_msg(m, port, io);
		}
		return 0;
	case 36:
		m->cpu.d[0] = 0;
		return 0;
	default:
		return -1;
	}
}
