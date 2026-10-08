#include "amiga.h"

#include <stddef.h>
#include <string.h>

/* Save states: the emulated machine of a struct Amiga, taken between frames. A state is a header
 * and one named, sized section per restored member of struct Amiga. */

struct StateField
{
	const char *name;
	const char *mismatch;
	size_t offset;
	size_t size;
};

#define STATE_FIELD(f) {#f, "the " #f " section does not match this build", offsetof(struct Amiga, f), sizeof(((struct Amiga *)0)->f)}

static const struct StateField state_fields[] = {
	STATE_FIELD(chip), STATE_FIELD(slow), STATE_FIELD(cpu), STATE_FIELD(cycles), STATE_FIELD(insns),
	STATE_FIELD(done), STATE_FIELD(frame_count), STATE_FIELD(copper), STATE_FIELD(denise),
	STATE_FIELD(blit), STATE_FIELD(paula), STATE_FIELD(cia_a), STATE_FIELD(cia_b), STATE_FIELD(floppy),
	STATE_FIELD(hle), STATE_FIELD(drive_role), STATE_FIELD(loads), STATE_FIELD(load_count),
	STATE_FIELD(load_reads), STATE_FIELD(keys), STATE_FIELD(key_head), STATE_FIELD(key_tail),
	STATE_FIELD(frame),
};

typedef char state_sections_count[sizeof state_fields / sizeof state_fields[0] == STATE_SECTIONS ? 1 : -1];

static void state_crcs(const struct Amiga *m, uint32_t crc[AMIGA_ROLES])
{
	int r;
	memset(crc, 0, AMIGA_ROLES * sizeof crc[0]);
	for(r = 0; r < AMIGA_ROLES; r++)
		if(m->disks[r].image && !m->disks[r].rw)
			crc[r] = amiga_crc32(m->disks[r].image, AMIGA_ADF_SIZE);
}

uint32_t state_save(const struct Amiga *m, uint8_t *out)
{
	struct StateHeader h;
	struct StateSection sec;
	uint32_t n = 0;
	size_t i;

	memset(&h, 0, sizeof h);
	memcpy(h.magic, "ARSTATE", 8);
	h.version = STATE_VERSION;
	state_crcs(m, h.role_crc);
	memcpy(out + n, &h, sizeof h);
	n += (uint32_t)sizeof h;

	for(i = 0; i < STATE_SECTIONS; i++)
	{
		const struct StateField *f = &state_fields[i];
		memset(&sec, 0, sizeof sec);
		strcpy(sec.name, f->name);
		sec.size = (uint32_t)f->size;
		memcpy(out + n, &sec, sizeof sec);
		n += (uint32_t)sizeof sec;
		memcpy(out + n, (const uint8_t *)m + f->offset, f->size);
		n += (uint32_t)f->size;
	}
	return n;
}

/* Host bindings, run options, the log, disk images, the per-process LVO call counters and the
 * `booted` flag stay as they are in `m`. `m` is untouched unless every section checks out. */
const char *state_load(struct Amiga *m, const uint8_t *in, uint32_t len)
{
	struct StateHeader h;
	uint32_t want[AMIGA_ROLES];
	uint32_t calls[HLE_CALL_LIBS][256];
	uint32_t pos;
	size_t i;

	if(len < sizeof h)
		return "not an AmigaRecomp state";
	memcpy(&h, in, sizeof h);
	if(memcmp(h.magic, "ARSTATE", 8))
		return "not an AmigaRecomp state";
	if(h.version != STATE_VERSION)
		return "written in another state format";
	state_crcs(m, want);
	if(memcmp(h.role_crc, want, sizeof want))
		return "state was saved with different disk images";

	pos = (uint32_t)sizeof h;
	for(i = 0; i < STATE_SECTIONS; i++)
	{
		const struct StateField *f = &state_fields[i];
		struct StateSection sec;
		if(len - pos < sizeof sec)
			return "truncated or oversized";
		memcpy(&sec, in + pos, sizeof sec);
		if(strncmp(sec.name, f->name, sizeof sec.name) || sec.size != f->size)
			return f->mismatch;
		pos += (uint32_t)sizeof sec;
		if(len - pos < sec.size)
			return "truncated or oversized";
		pos += sec.size;
	}
	if(pos != len)
		return "truncated or oversized";

	memcpy(calls, m->hle.calls, sizeof calls);
	pos = (uint32_t)sizeof h;
	for(i = 0; i < STATE_SECTIONS; i++)
	{
		const struct StateField *f = &state_fields[i];
		pos += (uint32_t)sizeof(struct StateSection);
		memcpy((uint8_t *)m + f->offset, in + pos, f->size);
		pos += (uint32_t)f->size;
	}
	memcpy(m->hle.calls, calls, sizeof calls);
	return 0;
}
