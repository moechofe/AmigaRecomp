#include "amiga.h"
#include "game.h"

#include <stdio.h>
#include <string.h>

/* Synthetic program, not game bytes: at 0x1000 a quick-move of 1 into d0, at 0x1002 an
 * add-quick of 1 to d1, at 0x1004 a short branch back to 0x1002. The hook replaces the first
 * instruction; the guard covers its two bytes. */
static const uint8_t test_program[] = {0x70, 0x01, 0x52, 0x81, 0x60, 0xFC};

static int test_runs, test_frames;

static int test_run(struct Amiga *m, int value)
{
	(void)value;
	test_runs++;
	m->cpu.d[0] = 2;
	m->cpu.pc = 0x1002;
	m->cycles += 4;
	return 1;
}

static void test_frame(struct Amiga *m, int value)
{
	(void)m;
	(void)value;
	test_frames++;
}

static struct GameAnchor test_guard = {0x1000, 2, 0};

static const struct GameHook test_hooks[] = {
	{0x1000, &test_guard, test_run},
};

static const struct GameHack test_hack_one = {
	{"one", "One", "", GameValueBool, 0, 0},
	test_hooks, 1, test_frame,
};

static const struct GameHack *const test_hacks[] = {&test_hack_one};

const struct GameDef game_def = {
	.id = "test",
	.title = "Test Game",
	.pointer_sprite = -1,
	.hacks = test_hacks,
	.hack_count = 1,
};

static int failures;
static struct Amiga m;

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

static void setup(int value, int patch)
{
	int frame;
	amiga_init(&m);
	interp_init(&m);
	memcpy(m.chip + 0x1000, test_program, sizeof test_program);
	if(patch)
		m.chip[0x1001] = 0x03;
	m.cpu.sr = 0x2700;
	m.cpu.a[7] = 0x7FF00;
	m.cpu.pc = 0x1000;
	test_runs = 0;
	test_frames = 0;
	game_hack_set(&m, 0, value);
	for(frame = 0; frame < 2; frame++)
		amiga_run_frame(&m);
}

int main(void)
{
	char detail[AMIGA_FATAL_LEN + 64];

	test_guard.crc = amiga_crc32(test_program, 2);

	setup(1, 0);
	snprintf(detail, sizeof detail, "d0 %u runs %d status %d frames %d fatal %s", (unsigned)m.cpu.d[0], test_runs,
		m.hacks.status[0], test_frames, m.fatal ? m.fatal_msg : "none");
	check(!m.fatal && m.cpu.d[0] == 2 && test_runs == 1 && m.hacks.status[0] == HackActive && test_frames == 2,
		"hook runs when the guard matches", detail);

	setup(1, 1);
	snprintf(detail, sizeof detail, "d0 %u runs %d status %d fatal %s", (unsigned)m.cpu.d[0], test_runs,
		m.hacks.status[0], m.fatal ? m.fatal_msg : "none");
	check(!m.fatal && m.cpu.d[0] == 3 && test_runs == 0 && m.hacks.status[0] == HackMismatch,
		"original instruction runs when the guard fails", detail);

	setup(0, 0);
	snprintf(detail, sizeof detail, "d0 %u runs %d frames %d status %d live %u fatal %s", (unsigned)m.cpu.d[0],
		test_runs, test_frames, m.hacks.status[0], (unsigned)m.hacks.live, m.fatal ? m.fatal_msg : "none");
	check(!m.fatal && m.cpu.d[0] == 1 && test_runs == 0 && test_frames == 0 && m.hacks.status[0] == HackIdle
			&& m.hacks.live == 0,
		"a hack at value 0 is the original game", detail);

	check(game_hack_set(&m, 0, 2) == -1 && game_hack_set(&m, 1, 1) == -1, "out-of-range values are refused", "accepted");

	return failures ? 1 : 0;
}
