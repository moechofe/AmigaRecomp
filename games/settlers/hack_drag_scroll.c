#include "amiga.h"
#include "cpu_ops.h"
#include "game.h"

/* docs/hacks.md, "drag_scroll". */

#define SCROLL_ADD_PC 0xC073BEu  /* tst.l, beq, add.w, swap, add.w: the counts go to the scroll accumulators */
#define SCROLL_ADD_LEN 14u       /* execution resumes after them */
#define MOUSE_READ_PC 0xC072D8u  /* the mouse routine's head: A0 + 0x1B0 holds its port's JOYxDAT */
#define MOUSE_PORT 0x1B0u
#define ACC_X 0x10u
#define ACC_Y 0x12u
#define JOY0DAT 0xDFF00Au
/* One map step is 32 counts, and moves the map 32 pixels across or 40 lines down (measured). */
#define STEP_COUNTS 32
#define STEP_LINES 40

static const struct GameAnchor scroll_add_guard = {SCROLL_ADD_PC, SCROLL_ADD_LEN, 0xCB7A8A5Eu};
static const struct GameAnchor mouse_read = {MOUSE_READ_PC, 4, 0xBFC1A0FCu};

static void scroll_add(struct Amiga *m, uint32_t addr, int counts)
{
	mem_write16(m, addr, (uint16_t)(mem_read16(m, addr) + counts));
}

/* Counts for a cursor line; the view's lines are all positive, so the division floors. */
static int line_counts(int line)
{
	return line * STEP_COUNTS / STEP_LINES;
}

static int drag_scroll_run(struct Amiga *m, int value)
{
	const uint32_t a0 = m->cpu.a[0], d1 = m->cpu.d[1];
	(void)value;
	if(!m->steered || !game_anchor_ok(m, &mouse_read) || mem_read32(m, a0 + MOUSE_PORT) != JOY0DAT)
		return 0;
	scroll_add(m, a0 + ACC_X, -m->cursor_dx);
	scroll_add(m, a0 + ACC_Y, line_counts(m->cursor_y - m->cursor_dy) - line_counts(m->cursor_y));
	m->cycles += CPU_CYC(mem_read16(m, SCROLL_ADD_PC)) + CPU_CYC(mem_read16(m, SCROLL_ADD_PC + 2));
	if(d1)
	{
		m->cpu.d[1] = (d1 << 16) | (d1 >> 16);
		m->cycles += CPU_CYC(mem_read16(m, SCROLL_ADD_PC + 4)) + CPU_CYC(mem_read16(m, SCROLL_ADD_PC + 8))
			+ CPU_CYC(mem_read16(m, SCROLL_ADD_PC + 10));
	}
	m->cpu.pc = SCROLL_ADD_PC + SCROLL_ADD_LEN;
	return 1;
}

static const struct GameHook drag_scroll_hooks[] = {
	{SCROLL_ADD_PC, &scroll_add_guard, drag_scroll_run},
};

const struct GameHack settlers_hack_drag_scroll = {
	{"drag_scroll", "Drag the map with the right button",
		"Original: with Capture the mouse off, holding the right button scrolls the map on and on, by the cursor's distance from where the button went down: too fast to play. Modern: while the right button is held the map follows the cursor, as if the cursor were a hand holding it, a whole tile at a time. With Capture the mouse on, the mouse scrolls as in the original either way.",
		GameValueBool, 1, 0},
	drag_scroll_hooks, 1, 0,
};
