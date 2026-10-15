#include "script.h"
#include "game.h"

#include <stdio.h>
#include <string.h>

enum ScriptOp
{
	OpNone,
	OpWait,
	OpUntil,
	OpKey,
	OpClick,
	OpMouse,
	OpFire
};

struct ScriptKey
{
	const char *name;
	uint8_t code;
};

/* Raw keycodes, HRM appendix H (keyboard). */
static const struct ScriptKey script_keys[] = {
	{"1", 0x01}, {"2", 0x02}, {"3", 0x03}, {"4", 0x04}, {"5", 0x05},
	{"6", 0x06}, {"7", 0x07}, {"8", 0x08}, {"9", 0x09}, {"0", 0x0A},
	{"q", 0x10}, {"w", 0x11}, {"e", 0x12}, {"r", 0x13}, {"t", 0x14},
	{"y", 0x15}, {"u", 0x16}, {"i", 0x17}, {"o", 0x18}, {"p", 0x19},
	{"a", 0x20}, {"s", 0x21}, {"d", 0x22}, {"f", 0x23}, {"g", 0x24},
	{"h", 0x25}, {"j", 0x26}, {"k", 0x27}, {"l", 0x28},
	{"z", 0x31}, {"x", 0x32}, {"c", 0x33}, {"v", 0x34}, {"b", 0x35},
	{"n", 0x36}, {"m", 0x37},
	{"space", 0x40}, {"return", 0x44}, {"esc", 0x45},
	{"f1", 0x50}, {"f2", 0x51}, {"f3", 0x52}, {"f4", 0x53}, {"f5", 0x54},
	{"f6", 0x55}, {"f7", 0x56}, {"f8", 0x57}, {"f9", 0x58}, {"f10", 0x59},
	{"shift", 0x60},
};

void script_init(struct Script *s, const char *text)
{
	memset(s, 0, sizeof *s);
	s->text = text;
}

/* One bit per 24-bit colour, all clear between calls: each call clears the bits it set. */
static uint8_t script_seen[(1u << 24) / 8];

int script_colours(const struct Amiga *m)
{
	int x, y, n = 0;
	for(y = 0; y < AMIGA_FRAME_H; y++)
	{
		for(x = 0; x < AMIGA_FRAME_W; x++)
		{
			uint32_t c = m->frame[AMIGA_FRAME_CAPTURE_ROW + y][x] & 0xFFFFFF;
			if(!(script_seen[c >> 3] & (1u << (c & 7))))
			{
				script_seen[c >> 3] |= (uint8_t)(1u << (c & 7));
				n++;
			}
		}
	}
	for(y = 0; y < AMIGA_FRAME_H; y++)
		for(x = 0; x < AMIGA_FRAME_W; x++)
		{
			uint32_t c = m->frame[AMIGA_FRAME_CAPTURE_ROW + y][x] & 0xFFFFFF;
			script_seen[c >> 3] &= (uint8_t)~(1u << (c & 7));
		}
	return n;
}

static int script_next_line(struct Script *s)
{
	for(;;)
	{
		size_t n = 0;
		char *hash;
		if(!s->text[s->pos])
			return 0;
		while(s->text[s->pos] && s->text[s->pos] != '\n')
		{
			if(n + 1 < sizeof s->line)
				s->line[n++] = s->text[s->pos];
			s->pos++;
		}
		if(s->text[s->pos] == '\n')
			s->pos++;
		s->line[n] = 0;
		s->lineno++;
		hash = strchr(s->line, '#');
		if(hash)
			*hash = 0;
		for(n = 0; s->line[n] == ' ' || s->line[n] == '\t'; n++)
			;
		if(s->line[n] && s->line[n] != '\r')
			return 1;
	}
}

static enum ScriptResult script_fail(struct Script *s, const char *why)
{
	snprintf(s->error, sizeof s->error, "line %d: %s: %s", s->lineno, why, s->line);
	return ScriptError;
}

static enum ScriptResult script_start(struct Script *s, struct Amiga *m)
{
	char word[32], arg[64];
	int a = 0, b = 0, n;
	size_t k;
	n = sscanf(s->line, "%31s %63s", word, arg);
	s->phase = 0;
	s->count = 0;
	s->frames = 0;
	if(!strcmp(word, "wait") && n == 2 && sscanf(arg, "%d", &a) == 1)
	{
		s->active = OpWait;
		s->arg[0] = a;
		return ScriptRunFrame;
	}
	if(!strcmp(word, "until"))
	{
		char what[16];
		int timeout = 3000;
		if(sscanf(s->line, "%*s %15s %d %d", what, &a, &timeout) < 2 || strcmp(what, "colours"))
			return script_fail(s, "expected `until colours <n> [<timeout>]`");
		s->active = OpUntil;
		s->arg[0] = a;
		s->arg[1] = timeout;
		return ScriptRunFrame;
	}
	if(!strcmp(word, "key") && n == 2)
	{
		for(k = 0; k < sizeof script_keys / sizeof script_keys[0]; k++)
		{
			if(!strcmp(arg, script_keys[k].name))
			{
				s->active = OpKey;
				s->arg[0] = script_keys[k].code;
				return ScriptRunFrame;
			}
		}
		return script_fail(s, "unknown key");
	}
	if(!strcmp(word, "click"))
	{
		s->active = OpClick;
		s->arg[2] = 0;
		if(sscanf(s->line, "%*s %d %d", &a, &b) == 2)
		{
			s->arg[0] = a;
			s->arg[1] = b;
		}
		else
		{
			s->phase = 2;
		}
		return ScriptRunFrame;
	}
	if(!strcmp(word, "rclick") && n == 1)
	{
		s->active = OpClick;
		s->arg[2] = 1;
		s->phase = 2;
		return ScriptRunFrame;
	}
	if(!strcmp(word, "mouse") && sscanf(s->line, "%*s %d %d", &a, &b) == 2)
	{
		s->active = OpMouse;
		s->arg[0] = a;
		s->arg[1] = b;
		return ScriptRunFrame;
	}
	if(!strcmp(word, "fire") && n == 1)
	{
		s->active = OpFire;
		return ScriptRunFrame;
	}
	if(!strcmp(word, "disk"))
	{
		char drive[16], role[32];
		int d, r;
		if(sscanf(s->line, "%*s %15s %31s", drive, role) != 2)
			return script_fail(s, "expected `disk df<n> <role>`");
		if(strlen(drive) != 3 || strncmp(drive, "df", 2) || drive[2] < '0' || drive[2] >= '0' + AMIGA_DRIVES)
		{
			snprintf(s->error, sizeof s->error, "disk: unknown drive %s", drive);
			return ScriptError;
		}
		d = drive[2] - '0';
		for(r = 0; r < (int)game_def.disk_count; r++)
			if(!strcmp(role, game_def.disks[r].id))
				break;
		if(r == (int)game_def.disk_count)
		{
			snprintf(s->error, sizeof s->error, "disk: unknown role %s", role);
			return ScriptError;
		}
		amiga_insert(m, d, r);
		return ScriptRunFrame;
	}
	if(!strcmp(word, "sprites") && n == 2)
	{
		m->sprites_enabled = !strcmp(arg, "on");
		return ScriptRunFrame;
	}
	if(!strcmp(word, "state"))
	{
		if(n == 2 && !strcmp(arg, "save"))
			return ScriptStateSave;
		if(n == 2 && !strcmp(arg, "load"))
			return ScriptStateLoad;
		return script_fail(s, "expected `state save|load`");
	}
	if(!strcmp(word, "shot") && n == 2)
	{
		snprintf(s->name, sizeof s->name, "%s", arg);
		return ScriptShot;
	}
	if(!strcmp(word, "hack"))
	{
		char id[32], val[32];
		const struct GameSetting *set;
		uint32_t i;
		int v = 0;
		if(sscanf(s->line, "%*s %31s %31s", id, val) != 2)
			return script_fail(s, "expected `hack <hackid> on|off|<choice id>`");
		for(i = 0; i < game_def.hack_count; i++)
			if(!strcmp(game_def.hacks[i]->setting.id, id))
				break;
		if(i == game_def.hack_count)
		{
			snprintf(s->error, sizeof s->error, "hack: unknown hack %s", id);
			return ScriptError;
		}
		set = &game_def.hacks[i]->setting;
		if(set->type == GameValueBool)
		{
			if(!strcmp(val, "on"))
				v = 1;
			else if(strcmp(val, "off"))
			{
				snprintf(s->error, sizeof s->error, "hack: %s takes on or off", id);
				return ScriptError;
			}
		}
		else
		{
			while(set->choices && set->choices[v] && strcmp(set->choices[v], val))
				v++;
			if(!set->choices || !set->choices[v])
			{
				snprintf(s->error, sizeof s->error, "hack: %s has no choice %s", id, val);
				return ScriptError;
			}
		}
		if(game_hack_set(m, i, v))
		{
			snprintf(s->error, sizeof s->error, "hack: cannot set %s", id);
			return ScriptError;
		}
		return ScriptRunFrame;
	}
	if(!strcmp(word, "quit"))
		return ScriptQuit;
	return script_fail(s, "unknown directive");
}

/* One frame of an active directive. Returns 1 while it wants another frame. */
static int script_continue(struct Script *s, struct Amiga *m, enum ScriptResult *res)
{
	int dx, dy;
	*res = ScriptRunFrame;
	switch(s->active)
	{
	case OpWait:
		return s->count++ < s->arg[0];
	case OpUntil:
		if(script_colours(m) == s->arg[0])
			return 0;
		if(s->count++ >= s->arg[1])
		{
			snprintf(s->error, sizeof s->error, "line %d: %d colours never reached in %d frames (%d on screen)",
				s->lineno, s->arg[0], s->arg[1], script_colours(m));
			*res = ScriptTimeout;
			return 0;
		}
		return 1;
	case OpKey:
		amiga_key(m, (uint8_t)s->arg[0], s->count != 0);
		return s->count++ == 0;
	case OpMouse:
		if(s->count++)
			return 0;
		amiga_mouse(m, s->arg[0], s->arg[1], m->paula.lmb, m->paula.rmb);
		return 1;
	case OpFire:
		/* The port-2 fire button held 3 frames, then one frame released, as a click. */
		if(s->count < 3)
			amiga_joystick(m, 1, (uint8_t)(m->paula.joy[1] | AmigaJoyFire));
		else
			amiga_joystick(m, 1, (uint8_t)(m->paula.joy[1] & ~AmigaJoyFire));
		return s->count++ < 4;
	case OpClick:
		/* Park in the top-left corner for 8 frames, move at most 100 counts a frame, hold
		 * the button (arg[2]: 0 left, 1 right) 3 frames, release. */
		if(s->phase == 0)
		{
			amiga_mouse(m, -100, -100, 0, 0);
			if(++s->count == 8)
			{
				s->phase = 1;
				s->count = 0;
			}
			return 1;
		}
		if(s->phase == 1)
		{
			dx = s->arg[0] > 100 ? 100 : s->arg[0];
			dy = s->arg[1] > 100 ? 100 : s->arg[1];
			s->arg[0] -= dx;
			s->arg[1] -= dy;
			amiga_mouse(m, dx, dy, 0, 0);
			if(!s->arg[0] && !s->arg[1])
				s->phase = 2;
			return 1;
		}
		if(s->phase == 2)
		{
			amiga_mouse(m, 0, 0, !s->arg[2], s->arg[2]);
			if(++s->frames == 3)
				s->phase = 3;
			return 1;
		}
		if(s->phase == 3)
		{
			amiga_mouse(m, 0, 0, 0, 0);
			s->phase = 4;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

enum ScriptResult script_tick(struct Script *s, struct Amiga *m)
{
	for(;;)
	{
		enum ScriptResult r;
		if(s->active)
		{
			if(script_continue(s, m, &r))
				return r;
			s->active = OpNone;
			if(r != ScriptRunFrame)
				return r;
		}
		if(!script_next_line(s))
			return ScriptQuit;
		r = script_start(s, m);
		if(s->active)
			continue;
		if(r == ScriptRunFrame)
			continue;
		return r;
	}
}
