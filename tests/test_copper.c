#include "amiga.h"
#include "game.h"

#include <stdio.h>

static const struct GameDisk disks[] = {{.id = "disk1", .label = "Disk 1"}};

const struct GameDef game_def = {
	.id = "test",
	.title = "Test Game",
	.disks = disks,
	.disk_count = 1,
	.drive_count = 1,
};

static int failures;
static struct Amiga m;

/* A synthetic copper list at 0x1000 on a 5-plane lores screen whose window runs to line 300,
 * shaped as Superfrog's level ends its list (games/superfrog/docs/format.md, "Screens"): a WAIT
 * for line 255 at 0x7E, 17 MOVEs, a WAIT for `wait` (its line's low eight bits and 0xDE), then a
 * MOVE of 0x0ABC to COLOR01. The 17 MOVEs end in colour clock 0xDC of line 255, where the oracle
 * read that last WAIT's second word (docs/runtime.md, "Timing"). The line in which COLOR01
 * changes, or -1. */
static int test_wakes(uint16_t wait)
{
	uint32_t pc = 0x1000;
	int line, i;
	amiga_init(&m);
	mem_chip_write16(&m, pc, 0xFF7F), mem_chip_write16(&m, pc + 2, 0xFFFE), pc += 4;
	for(i = 0; i < 17; i++)
		mem_chip_write16(&m, pc, 0x180), mem_chip_write16(&m, pc + 2, 0), pc += 4;
	mem_chip_write16(&m, pc, wait), mem_chip_write16(&m, pc + 2, 0xFFFE), pc += 4;
	mem_chip_write16(&m, pc, 0x182), mem_chip_write16(&m, pc + 2, 0x0ABC), pc += 4;
	mem_chip_write16(&m, pc, 0xFFFF), mem_chip_write16(&m, pc + 2, 0xFFFE);
	custom_write(&m, 0x100, 0x5200, 0);
	custom_write(&m, 0x08E, 0x2C81, 0);
	custom_write(&m, 0x090, 0x2CC1, 0);
	custom_write(&m, 0x092, 0x0028, 0);
	custom_write(&m, 0x094, 0x00D8, 0);
	custom_write(&m, 0x080, 0x0000, 0);
	custom_write(&m, 0x082, 0x1000, 0);
	custom_write(&m, 0x096, 0x8380, 0);
	for(line = 0; line < AMIGA_LINES; line++)
	{
		sched_catch_up(&m, (uint64_t)(line + 1) * AMIGA_CYCLES_PER_LINE);
		if(m.denise.color[1])
			return line;
	}
	return -1;
}

static void check(int got, int want, const char *name)
{
	if(got == want)
		printf("ok %s\n", name);
	else
	{
		printf("FAIL %s: line %d, want %d\n", name, got, want);
		failures++;
	}
}

int main(void)
{
	check(test_wakes(0x04DF), 0x105, "a WAIT for line 0x104 read at the end of line 255 moves on line 0x105, not 256");
	check(test_wakes(0xFFDF), -1, "a WAIT for line 255 at 0xDE read at its end never matches");
	return failures != 0;
}
