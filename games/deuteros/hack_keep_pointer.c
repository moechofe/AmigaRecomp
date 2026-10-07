#include "amiga.h"
#include "cpu_ops.h"
#include "game.h"

/* docs/hacks.md, "keep_pointer". */

#define KEEP_POINTER_PC 0x023FC6u /* the instruction that forces the pointer x */
#define KEEP_POINTER_LEN 8u       /* its length: execution resumes after it */
#define LAST_DX 0x01EE10u         /* last motion event's deltas */
#define LAST_DY 0x01EE12u

static const struct GameAnchor keep_pointer_guard = {KEEP_POINTER_PC, KEEP_POINTER_LEN, 0xD8A44675u};

static int keep_pointer_run(struct Amiga *m, int value)
{
	(void)value;
	m->cycles += CPU_CYC(0x33FC);
	m->cpu.pc = KEEP_POINTER_PC + KEEP_POINTER_LEN;
	mem_write16(m, LAST_DX, 0);
	mem_write16(m, LAST_DY, 0);
	return 1;
}

static const struct GameHook keep_pointer_hooks[] = {
	{KEEP_POINTER_PC, &keep_pointer_guard, keep_pointer_run},
};

const struct GameHack deuteros_hack_keep_pointer = {
	{"keep_pointer", "Keep the pointer when Advance Time ends",
		"Original: the click that ends Advance Time moves the pointer right. Modern: the pointer stays where the player left it.",
		GameValueBool, 1, 0},
	keep_pointer_hooks, 1, 0,
};
