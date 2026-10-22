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

#define PLANE 0x2000u
#define ROW 40
#define SCRATCH 0x3000u
#define MARK 0x0008 /* x 12, in the line's first word */

static int failures;
static struct Amiga m;

static void check(int ok, const char *name)
{
	printf("%s %s\n", ok ? "ok" : "FAIL", name);
	if(!ok)
		failures++;
}

/* A filled-vector edge as the HRM's line mode draws it: (0,0) to (2,8), y major (octant code 0,
 * SIGN set by the negative initial error), one dot per row (SING), A XOR C, BLTCPT at the first
 * row and BLTDPT at `dpt`. Row 1 holds a dot at x 12 beforehand. */
static void draw_edge(uint32_t dpt)
{
	const int dx = 8, dy = 2;
	amiga_init(&m);
	mem_chip_write16(&m, PLANE + ROW, MARK);
	custom_write(&m, 0x096, 0x8240, 0);
	custom_write(&m, 0x040, 0x0B4A, 0);
	custom_write(&m, 0x042, 0x0043, 0);
	custom_write(&m, 0x044, 0xFFFF, 0);
	custom_write(&m, 0x046, 0xFFFF, 0);
	custom_write(&m, 0x074, 0x8000, 0);
	custom_write(&m, 0x072, 0xFFFF, 0);
	custom_write(&m, 0x062, (uint16_t)(4 * dy), 0);
	custom_write(&m, 0x064, (uint16_t)(4 * (dy - dx)), 0);
	custom_write(&m, 0x050, 0xFFFF, 0);
	custom_write(&m, 0x052, (uint16_t)(4 * dy - 2 * dx), 0);
	custom_write(&m, 0x060, ROW, 0);
	custom_write(&m, 0x066, ROW, 0);
	custom_write(&m, 0x048, 0, 0);
	custom_write(&m, 0x04A, PLANE, 0);
	custom_write(&m, 0x054, (uint16_t)(dpt >> 16), 0);
	custom_write(&m, 0x056, (uint16_t)dpt, 0);
	custom_write(&m, 0x058, (uint16_t)(((dx + 1) << 6) | 2), 0);
}

/* The x of the one line dot in row y's first word, -1 for none, -2 for more than one. */
static int dot(int y)
{
	uint16_t w = mem_chip16(&m, PLANE + (uint32_t)(y * ROW));
	int x, found = -1;
	if(y == 1)
		w &= (uint16_t)~MARK;
	for(x = 0; x < 16; x++)
		if(w & (0x8000 >> x))
			found = found == -1 ? x : -2;
	return found;
}

static int rows_ok(int first)
{
	int y, prev = 0;
	for(y = first; y <= 8; y++)
	{
		int x = dot(y);
		if(x < prev || x > 2)
			return 0;
		prev = x;
	}
	return dot(8) == 2;
}

int main(void)
{
	draw_edge(PLANE);
	check(dot(0) == 0 && rows_ok(0), "a SING line draws one dot on each of its 9 rows, from x 0 to x 2");
	check((mem_chip16(&m, PLANE) & MARK) == 0 && (mem_chip16(&m, PLANE + ROW) & MARK) != 0,
		"a dot already in the next row stays in its row");

	draw_edge(SCRATCH);
	check(mem_chip16(&m, SCRATCH) == 0x8000 && dot(0) == -1 && rows_ok(1),
		"BLTDPT elsewhere takes only the first dot");
	return failures != 0;
}
