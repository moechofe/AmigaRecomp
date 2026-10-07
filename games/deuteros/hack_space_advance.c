#include "amiga.h"
#include "cpu_ops.h"
#include "game.h"

/* docs/hacks.md, "space_advance". */

#define SPACE_START_PC 0x04063Eu  /* the main loop's test of the popped key */
#define SPACE_START_RESUME 0x040670u /* the main loop's next pass */
#define SPACE_STOP_PC 0x023FA0u   /* the head of Advance Time's wait loop */
#define SPACE_STOP_RESUME 0x023FBAu /* the wait loop's click exit */
#define CLICK_PATH 0x0222E8u      /* the panel click handler */
#define POP_KEY 0x01F238u         /* pops the head of the key queue into d0 */
#define SLOT_TABLE 0x0214D8u      /* the panel slot table */
#define KEY_QUEUE 0x01EEC0u
#define KEY_COUNT 0x01EED6u
#define MASK_POINTER 0x01301Au    /* pointer to the pointer to the slot-mask word */
#define HOVER_SOUND 0x03F77Cu     /* what hovering slot 0 writes */
#define HOVER_A 0x0222A0u
#define HOVER_SLOT 0x02229Eu
#define HOVER_ICON 0x022292u
#define HOVER_ICON_VALUE 0x1Au
#define KEY_SPACE 0x20u

static const struct GameAnchor space_start_guard = {SPACE_START_PC, 4, 0xFE929D58u};
static const struct GameAnchor space_stop_guard = {SPACE_STOP_PC, 6, 0x313D2691u};
static const struct GameAnchor space_slots = {SLOT_TABLE, 10, 0xB6B95F64u};

static int space_start_run(struct Amiga *m, int value)
{
	(void)value;
	if((m->cpu.d[0] & 0xFF) != KEY_SPACE)
		return 0;
	if(!game_anchor_ok(m, &space_slots))
		return 0;
	if(!(mem_read16(m, mem_read32(m, MASK_POINTER)) & 1))
		return 0;
	mem_write16(m, HOVER_SOUND, 4);
	mem_write16(m, HOVER_A, 0);
	mem_write16(m, HOVER_SLOT, 1);
	mem_write32(m, HOVER_ICON, HOVER_ICON_VALUE);
	m->cycles += CPU_CYC(0x4EB9);
	cpu_push32(m, SPACE_START_RESUME);
	m->cpu.pc = CLICK_PATH;
	return 1;
}

static int space_stop_run(struct Amiga *m, int value)
{
	(void)value;
	if(mem_read16(m, KEY_COUNT) == 0 || mem_read8(m, KEY_QUEUE) != KEY_SPACE)
		return 0;
	m->cycles += CPU_CYC(0x4EB9);
	cpu_push32(m, SPACE_STOP_RESUME);
	m->cpu.pc = POP_KEY;
	return 1;
}

static const struct GameHook space_advance_hooks[] = {
	{SPACE_START_PC, &space_start_guard, space_start_run},
	{SPACE_STOP_PC, &space_stop_guard, space_stop_run},
};

const struct GameHack deuteros_hack_space_advance = {
	{"space_advance", "Space starts and stops Advance Time",
		"On the main screen Space does what the top-left icon does; during Advance Time it stops it, like a click.",
		GameValueBool, 1, 0},
	space_advance_hooks, 2, 0,
};
