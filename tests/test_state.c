#include "amiga.h"
#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const struct GameDisk disks[] = {{.id = "disk1", .label = "Disk 1"}, {.id = "disk2", .label = "Disk 2"}};

const struct GameDef game_def = {
	.id = "test",
	.title = "Test Game",
	.disks = disks,
	.disk_count = 2,
	.drive_count = 2,
};

static int failures;
static struct Amiga a, b;
static uint8_t buf[STATE_MAX + 1], fresh[STATE_MAX + 1];
static uint8_t image[AMIGA_ADF_SIZE], image2[AMIGA_ADF_SIZE];

static void check(int ok, const char *name, const char *detail)
{
	if(ok)
		printf("ok %s\n", name);
	else
	{
		printf("FAIL %s: %s\n", name, detail);
		failures++;
	}
}

/* Offset of the named section's StateSection in a written state, or 0. */
static uint32_t section_offset(const char *name)
{
	uint32_t pos = (uint32_t)sizeof(struct StateHeader);
	int i;
	for(i = 0; i < STATE_SECTIONS; i++)
	{
		struct StateSection sec;
		memcpy(&sec, buf + pos, sizeof sec);
		if(!strncmp(sec.name, name, sizeof sec.name))
			return pos;
		pos += (uint32_t)sizeof sec + sec.size;
	}
	return 0;
}

int main(void)
{
	uint32_t n, off, size;
	const char *why;

	amiga_init(&a);
	amiga_init(&b);
	a.cpu.pc = 0x1234;
	a.chip[0x100] = 0x5A;
	a.frame_count = 77;
	a.hle.calls[0][1] = 7;
	b.hle.calls[0][1] = 3;
	n = state_save(&a, buf);
	why = state_load(&b, buf, n);
	check(!why && b.cpu.pc == 0x1234 && b.chip[0x100] == 0x5A && b.frame_count == 77 && b.hle.calls[0][1] == 3,
		"state round-trips", why ? why : "restored values differ");

	amiga_init(&b);
	off = section_offset("copper");
	memcpy(&size, buf + off + offsetof(struct StateSection, size), sizeof size);
	size++;
	memcpy(buf + off + offsetof(struct StateSection, size), &size, sizeof size);
	b.cpu.pc = 0x9999;
	why = state_load(&b, buf, n);
	check(off && why && !strcmp(why, "the copper section does not match this build") && b.cpu.pc == 0x9999,
		"a changed section size is refused by name", why ? why : "accepted");

	amiga_init(&b);
	n = state_save(&a, buf);
	why = state_load(&b, buf, n - 1);
	check(why && !strcmp(why, "truncated or oversized"), "a truncated state is refused", why ? why : "accepted");
	buf[n] = 0;
	why = state_load(&b, buf, n + 1);
	check(why && !strcmp(why, "truncated or oversized"), "an oversized state is refused", why ? why : "accepted");

	{
		uint32_t v = 1;
		memcpy(buf + 8, &v, sizeof v);
		why = state_load(&b, buf, n);
		check(why && !strcmp(why, "written in another state format"), "another format version is refused",
			why ? why : "accepted");
	}

	n = state_save(&a, buf);
	amiga_attach(&b, 0, image, 0);
	why = state_load(&b, buf, n);
	check(why && !strcmp(why, "state was saved with different disk images"), "different read-only disks are refused",
		why ? why : "accepted");

	/* A reset after a disk swap and some play is the cold boot of a machine started with the
	 * swapped disks. */
	memset(image, 0x11, sizeof image);
	memset(image2, 0x22, sizeof image2);
	amiga_init(&b);
	amiga_attach(&b, 0, image, 0);
	amiga_attach(&b, 1, image2, 1);
	amiga_insert(&b, 0, 1);
	amiga_insert(&b, 1, 0);
	amiga_insert(&b, 2, -1);
	amiga_insert(&b, 3, -1);
	amiga_boot(&b);
	b.hle.calls[0][1] = 9;
	n = state_save(&b, fresh);
	amiga_init(&a);
	amiga_attach(&a, 0, image, 0);
	amiga_attach(&a, 1, image2, 1);
	amiga_insert(&a, 0, 0);
	amiga_boot(&a);
	amiga_insert(&a, 0, 1);
	amiga_insert(&a, 1, 0);
	a.cpu.pc = 0x4321;
	a.chip[0x2000] = 0xA5;
	a.frame_count = 500;
	a.disks[1].dirty = 1;
	a.sprites_enabled = 0;
	a.hle.calls[0][1] = 9;
	check(!amiga_reset(&a) && state_save(&a, buf) == n && !memcmp(buf, fresh, n), "a reset is a cold boot with the drives' disks",
		a.fatal ? a.fatal_msg : "the state differs from a fresh boot");
	check(a.drive_role[0] == 1 && a.drive_role[1] == 0 && a.disks[1].rw == image2 && a.disks[1].dirty && !a.sprites_enabled
		&& a.hle.calls[0][1] == 9, "a reset keeps the disks and host fields", "a host field was cleared");

	return failures ? 1 : 0;
}
