#include "amiga.h"
#include "game.h"

#include <string.h>

/* Guards read the RAM arrays directly: a hook is only ever set on RAM. */
int game_anchor_ok(const struct Amiga *m, const struct GameAnchor *a)
{
	const uint8_t *bytes;
	if(!a || !a->len)
		return 0;
	if(a->addr < AMIGA_CHIP_SIZE && a->len <= AMIGA_CHIP_SIZE - a->addr)
		bytes = m->chip + a->addr;
	else if(a->addr >= AMIGA_SLOW_BASE && a->addr - AMIGA_SLOW_BASE < AMIGA_SLOW_SIZE
		&& a->len <= AMIGA_SLOW_SIZE - (a->addr - AMIGA_SLOW_BASE))
		bytes = m->slow + (a->addr - AMIGA_SLOW_BASE);
	else
		return 0;
	return amiga_crc32(bytes, a->len) == a->crc;
}

static int hack_value_ok(const struct GameSetting *s, int value)
{
	int n = 0;
	if(s->type == GameValueBool)
		return value == 0 || value == 1;
	while(s->choices && s->choices[n])
		n++;
	return value >= 0 && value < n;
}

static void hack_map_build(struct Amiga *m)
{
	uint32_t h, k;
	memset(m->hacks.map, 0, sizeof m->hacks.map);
	m->hacks.live = 0;
	for(h = 0; h < game_def.hack_count; h++)
	{
		const struct GameHack *hack = game_def.hacks[h];
		if(!m->hacks.value[h])
			continue;
		for(k = 0; k < hack->hook_count; k++)
		{
			uint32_t pc = hack->hooks[k].pc, w;
			if(pc & 1)
				w = 0xFFFFFFFFu;
			else if(pc < AMIGA_CHIP_SIZE)
				w = pc >> 1;
			else if(pc >= AMIGA_SLOW_BASE && pc < AMIGA_SLOW_BASE + AMIGA_SLOW_SIZE)
				w = (AMIGA_CHIP_SIZE + pc - AMIGA_SLOW_BASE) >> 1;
			else
				w = 0xFFFFFFFFu;
			if(w == 0xFFFFFFFFu)
			{
				amiga_fatal(m, "hack %s: hook pc 0x%06X is not an even RAM address", hack->setting.id, (unsigned)pc);
				continue;
			}
			m->hacks.map[w >> 5] |= 1u << (w & 31);
			m->hacks.live++;
		}
	}
}

int game_hack_set(struct Amiga *m, uint32_t hack, int value)
{
	if(hack >= game_def.hack_count || hack >= AMIGA_HACKS)
		return -1;
	if(!hack_value_ok(&game_def.hacks[hack]->setting, value))
		return -1;
	m->hacks.value[hack] = value;
	hack_map_build(m);
	return 0;
}

/* Called with m->cpu synced. */
int game_hook_run(struct Amiga *m, uint32_t pc)
{
	uint32_t h, k;
	for(h = 0; h < game_def.hack_count; h++)
	{
		const struct GameHack *hack = game_def.hacks[h];
		if(!m->hacks.value[h])
			continue;
		for(k = 0; k < hack->hook_count; k++)
		{
			const struct GameHook *hook = &hack->hooks[k];
			if(hook->pc != pc)
				continue;
			if(!game_anchor_ok(m, hook->guard))
			{
				if(m->hacks.status[h] != HackActive)
					m->hacks.status[h] = HackMismatch;
				continue;
			}
			m->hacks.status[h] = HackActive;
			if(hook->run(m, m->hacks.value[h]))
			{
				m->insns++;
				return 1;
			}
		}
	}
	return 0;
}

void game_hacks_frame(struct Amiga *m)
{
	uint32_t h;
	for(h = 0; h < game_def.hack_count; h++)
	{
		const struct GameHack *hack = game_def.hacks[h];
		if(m->hacks.value[h] && hack->frame)
			hack->frame(m, m->hacks.value[h]);
	}
}

/* 1 when game_def.system_cursor is one of the game's hacks and is on. */
int game_system_cursor(const struct Amiga *m)
{
	uint32_t h;
	for(h = 0; h < game_def.hack_count && h < AMIGA_HACKS; h++)
		if(game_def.hacks[h] == game_def.system_cursor)
			return m->hacks.value[h] != 0;
	return 0;
}
