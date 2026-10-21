#include "amiga.h"
#include "cpu_ops.h"
#include "game.h"

/* docs/hacks.md, "jump_on_fire". */

#define JOY1DAT 0xDFF00Cu
#define POTGOR 0xDFF016u
#define CIAA_PRA 0xBFE001u
#define PRA_FIR1 0x80u    /* the port-2 fire button, active low (HRM appendix F) */
#define POTGOR_DATRY 0x4000u /* pin 9 of port 2: the second button, active low (HRM) */
#define JOY_READ_LEN 6    /* a word move from an absolute long address into a data register */
#define FIRE_TEST_LEN 8   /* a bit test of an immediate bit number on an absolute long address */

/* The port-2 stick as the game's level code should see it: its own left, right and down, and up
 * while the fire button is held. HRM, "Reading the joystick". */
static uint16_t jump_joy(struct Amiga *m)
{
	const uint16_t raw = mem_read16(m, JOY1DAT);
	const int left = (raw >> 9) & 1, right = (raw >> 1) & 1;
	const int up = !(mem_read8(m, CIAA_PRA) & PRA_FIR1);
	const int down = !up && (((raw >> 1) ^ raw) & 1);
	return (uint16_t)((left << 9) | ((left ^ up) << 8) | (right << 1) | (right ^ down));
}

static int jump_read_run(struct Amiga *m, int value)
{
	const uint16_t op = mem_read16(m, m->cpu.pc);
	const uint16_t v = jump_joy(m);
	uint32_t *d = &m->cpu.d[(op >> 9) & 7];
	(void)value;
	*d = (*d & 0xFFFF0000u) | v;
	m->cpu.n = (uint8_t)(v >> 15);
	m->cpu.z = (uint8_t)(v == 0);
	m->cpu.v = 0;
	m->cpu.c = 0;
	m->cycles += CPU_CYC(op);
	m->cpu.pc += JOY_READ_LEN;
	return 1;
}

/* The game tests its fire bit; it gets the second button's instead. */
static int jump_fire_run(struct Amiga *m, int value)
{
	(void)value;
	m->cpu.z = (uint8_t)!(mem_read16(m, POTGOR) & POTGOR_DATRY);
	m->cycles += CPU_CYC(mem_read16(m, m->cpu.pc));
	m->cpu.pc += FIRE_TEST_LEN;
	return 1;
}

static const struct GameAnchor jump_wall = {0xC0F0DEu, 16, 0x15490770u};
static const struct GameAnchor jump_pose = {0xC10DE2u, 16, 0xE75C8684u};
static const struct GameAnchor jump_idle = {0xC121B0u, 16, 0xCF4F7405u};
static const struct GameAnchor jump_rest = {0xC122C2u, 16, 0x635AA08Fu};
static const struct GameAnchor jump_rise = {0xC12314u, 16, 0x35ED815Fu};
static const struct GameAnchor jump_move = {0xC12468u, 16, 0x404D51F8u};
static const struct GameAnchor fire_spud = {0xC090B4u, 16, 0x60B93193u};
static const struct GameAnchor fire_throw = {0xC090DAu, 16, 0x36546C12u};
static const struct GameAnchor fire_pose = {0xC12034u, 16, 0x9CD48FFEu};
static const struct GameAnchor fire_up = {0xC125BAu, 16, 0x29075AFFu};
static const struct GameAnchor fire_up_right = {0xC1261Cu, 16, 0x72D234FDu};
static const struct GameAnchor fire_up_left = {0xC12690u, 16, 0xAC5B884Eu};

static const struct GameHook jump_on_fire_hooks[] = {
	{0xC0F0DEu, &jump_wall, jump_read_run},
	{0xC10DE2u, &jump_pose, jump_read_run},
	{0xC121B0u, &jump_idle, jump_read_run},
	{0xC122C2u, &jump_rest, jump_read_run},
	{0xC12314u, &jump_rise, jump_read_run},
	{0xC12468u, &jump_move, jump_read_run},
	{0xC090B4u, &fire_spud, jump_fire_run},
	{0xC090DAu, &fire_throw, jump_fire_run},
	{0xC12034u, &fire_pose, jump_fire_run},
	{0xC125BAu, &fire_up, jump_fire_run},
	{0xC1261Cu, &fire_up_right, jump_fire_run},
	{0xC12690u, &fire_up_left, jump_fire_run},
};

const struct GameHack superfrog_hack_jump_on_fire = {
	{"jump_on_fire", "Fire jumps, button 2 throws",
		"In the levels the fire button jumps and the joystick's button 2 throws, aimed with the stick as fire aimed it; up no longer jumps. By default A or Ctrl jumps and B or Space throws (Controls page).",
		GameValueBool, 1, 0},
	jump_on_fire_hooks, sizeof jump_on_fire_hooks / sizeof jump_on_fire_hooks[0], 0,
};
