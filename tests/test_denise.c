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

/* Line 0x50's frame row for field f (0 the long frame's, 1 the short frame's). */
#define ROW(f) ((0x50 - AMIGA_DISPLAY_LINE0) * AMIGA_FRAME_YS + (f))
/* The column of the first pixel at DIW x 0x81, the first of the standard window. */
#define COL (0x81 - AMIGA_FRAME_DIW_X0) * AMIGA_FRAME_XS

static void check(int ok, const char *name)
{
	printf("%s %s\n", ok ? "ok" : "FAIL", name);
	failures += !ok;
}

static void run_frame(int n)
{
	sched_catch_up(&m, (uint64_t)n * AMIGA_CYCLES_PER_FRAME + 1);
}

static void window(uint16_t bplcon0)
{
	custom_write(&m, 0x100, bplcon0, 0);
	custom_write(&m, 0x08E, 0x2C81, 0);
	custom_write(&m, 0x090, 0x2CC1, 0);
}

/* One hires plane of alternating bits from DDFSTRT 0x3C, whose first pixel is at DIW 0x81
 * (docs/runtime.md, "Timing"): the frame keeps both pixels of each pair, the capture the first. */
static void test_hires(void)
{
	uint32_t a;
	amiga_init(&m);
	for(a = 0x2000; a < 0x12000; a += 2)
		mem_chip_write16(&m, a, 0xAAAA);
	window(0x9200);
	custom_write(&m, 0x092, 0x003C, 0);
	custom_write(&m, 0x094, 0x00D4, 0);
	custom_write(&m, 0x0E0, 0x0000, 0);
	custom_write(&m, 0x0E2, 0x2000, 0);
	custom_write(&m, 0x180, 0x0000, 0);
	custom_write(&m, 0x182, 0x0FFF, 0);
	custom_write(&m, 0x096, 0x8300, 0);
	run_frame(1);
	check(m.frame[ROW(0)][COL] == 0xFFFFFF && m.frame[ROW(0)][COL + 1] == 0 && m.frame[ROW(0)][COL + 2] == 0xFFFFFF,
		"a hires line keeps both pixels of each pair");
	check(m.frame[ROW(1)][COL + 1] == m.frame[ROW(0)][COL + 1], "a line of a frame that is not interlaced fills both rows");
	check(denise_capture(&m, 0x81 - AMIGA_FRAME_DIW_X0, 0x50 - AMIGA_FRAME_LINE0) == 0xFFFFFF
		&& denise_capture(&m, 0x82 - AMIGA_FRAME_DIW_X0, 0x50 - AMIGA_FRAME_LINE0) == 0xFFFFFF,
		"the capture shows the first pixel of each hires pair");
}

/* No planes, LACE set: each frame paints the window in COLOR00. The first frame starts with LACE
 * set, so LOF toggles to the short frame and its lines fill the lower rows. */
static void test_lace(void)
{
	amiga_init(&m);
	window(0x0204);
	custom_write(&m, 0x180, 0x0F00, 0);
	run_frame(0);
	check(!(custom_read(&m, 0x004) & 0x8000), "LOF reads 0 in the first frame with LACE set");
	run_frame(1);
	check(m.frame[ROW(1)][COL] == 0xFF0000 && m.frame[ROW(0)][COL] == 0, "the short frame fills the lower rows alone");
	check(custom_read(&m, 0x004) & 0x8000, "LOF reads 1 in the next frame");
	check(denise_capture(&m, 0x81 - AMIGA_FRAME_DIW_X0, 0x50 - AMIGA_FRAME_LINE0) == 0xFF0000,
		"the capture after the short frame shows its field");
	custom_write(&m, 0x180, 0x00F0, 0);
	run_frame(2);
	check(m.frame[ROW(0)][COL] == 0x00FF00 && m.frame[ROW(1)][COL] == 0xFF0000, "the long frame fills the upper rows and keeps the other field");
	check(denise_capture(&m, 0x81 - AMIGA_FRAME_DIW_X0, 0x50 - AMIGA_FRAME_LINE0) == 0x00FF00,
		"the capture after the long frame shows its field");
	custom_write(&m, 0x100, 0x0200, 0);
	run_frame(3);
	run_frame(4);
	check(m.frame[ROW(0)][COL] == 0x00FF00 && m.frame[ROW(1)][COL] == 0x00FF00, "with LACE clear both rows are written again");
	check(!(custom_read(&m, 0x004) & 0x8000), "with LACE clear LOF keeps the value of the last interlaced frame, the short one");
}

int main(void)
{
	test_hires();
	test_lace();
	return failures != 0;
}
