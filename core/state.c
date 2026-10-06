#include "amiga.h"

#include <string.h>

/* Save states: the emulated machine of a struct Amiga, taken between frames. */

void state_header(const struct Amiga *m, struct StateHeader *h)
{
	int r;
	memset(h, 0, sizeof *h);
	memcpy(h->magic, "ARSTATE", 8);
	h->version = STATE_VERSION;
	h->size = (uint32_t)sizeof(struct Amiga);
	for(r = 0; r < AMIGA_ROLES; r++)
		if(m->disks[r].image && !m->disks[r].rw)
			h->role_crc[r] = amiga_crc32(m->disks[r].image, AMIGA_ADF_SIZE);
}

const char *state_check(const struct Amiga *m, const struct StateHeader *h)
{
	struct StateHeader want;
	state_header(m, &want);
	if(memcmp(h->magic, want.magic, sizeof want.magic))
		return "not an AmigaRecomp state";
	if(h->version != want.version || h->size != want.size)
		return "written by another build";
	if(memcmp(h->role_crc, want.role_crc, sizeof want.role_crc))
		return "state was saved with different disk images";
	return 0;
}

/* Host bindings, run options and the log stay as they are in `m`; so do the disk images, the
 * per-process LVO call counters and the `booted` flag. */
void state_restore(struct Amiga *m, const struct Amiga *from)
{
	uint32_t calls[4][256];
	memcpy(calls, m->hle.calls, sizeof calls);

	memcpy(m->chip, from->chip, sizeof m->chip);
	memcpy(m->slow, from->slow, sizeof m->slow);
	memcpy(m->loads, from->loads, sizeof m->loads);
	memcpy(m->keys, from->keys, sizeof m->keys);
	memcpy(m->frame, from->frame, sizeof m->frame);
	memcpy(m->drive_role, from->drive_role, sizeof m->drive_role);

	m->cpu = from->cpu;
	m->cycles = from->cycles;
	m->insns = from->insns;
	m->done = from->done;
	m->frame_count = from->frame_count;
	m->copper = from->copper;
	m->denise = from->denise;
	m->blit = from->blit;
	m->paula = from->paula;
	m->cia_a = from->cia_a;
	m->cia_b = from->cia_b;
	m->hle = from->hle;
	m->load_count = from->load_count;
	m->load_reads = from->load_reads;
	m->key_head = from->key_head;
	m->key_tail = from->key_tail;

	memcpy(m->hle.calls, calls, sizeof calls);
}
