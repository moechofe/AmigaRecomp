#include "amiga.h"
#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

const struct GameDef game_def = {
	.id = "test",
	.title = "Test Game",
	.pointer_sprite = -1,
};

static int failures;
static struct Amiga a, b;
static uint8_t buf[STATE_MAX + 1];
static uint8_t image[AMIGA_ADF_SIZE];

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
	buf[n] = 0;
	check(why && !strcmp(why, "truncated or oversized") && (why = state_load(&b, buf, n + 1)) &&
			!strcmp(why, "truncated or oversized"),
		"truncated and oversized are refused", why ? why : "accepted");

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

	return failures ? 1 : 0;
}
