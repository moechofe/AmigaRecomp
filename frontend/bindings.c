#include "bindings.h"

#include "amiga.h"
#include "game.h"
#include "settings.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const bind_labels[SdlCommandCount] = {"Joystick up", "Joystick down", "Joystick left", "Joystick right", "Joystick fire", "Joystick button 2"};
static const char *const bind_ids[SdlCommandCount] = {"joy_up", "joy_down", "joy_left", "joy_right", "joy_fire", "joy_fire2"};
static const uint8_t bind_joy[SdlCommandCount] = {AmigaJoyUp, AmigaJoyDown, AmigaJoyLeft, AmigaJoyRight, AmigaJoyFire, AmigaJoyFire2};

static const struct SdlBinding bind_defaults[] = {
	{SDL_SCANCODE_UP, SdlCommandJoyUp},
	{SDL_SCANCODE_DOWN, SdlCommandJoyDown},
	{SDL_SCANCODE_LEFT, SdlCommandJoyLeft},
	{SDL_SCANCODE_RIGHT, SdlCommandJoyRight},
	{SDL_SCANCODE_RCTRL, SdlCommandJoyFire},
	{SDL_SCANCODE_LCTRL, SdlCommandJoyFire},
	{SDL_SCANCODE_SPACE, SdlCommandJoyFire2},
	{SDL_BIND_PAD(SDL_GAMEPAD_BUTTON_SOUTH), SdlCommandJoyFire},
	{SDL_BIND_PAD(SDL_GAMEPAD_BUTTON_EAST), SdlCommandJoyFire2},
	{SDL_BIND_PAD(SDL_GAMEPAD_BUTTON_WEST), SdlCommandJoyFire},
	{SDL_BIND_PAD(SDL_GAMEPAD_BUTTON_NORTH), SdlCommandJoyFire},
};
#define BIND_DEFAULTS (int)(sizeof bind_defaults / sizeof bind_defaults[0])
SDL_COMPILE_TIME_ASSERT(bind_pad, SDL_BIND_PAD(0) >= SDL_SCANCODE_COUNT);

static struct SdlBinding bind_table[SDL_BINDINGS_MAX];
static int bind_count = -1;  /* -1: the defaults, not yet copied */
static uint8_t bind_held[SDL_SCANCODE_COUNT];

int sdl_menu_key(void)
{
	return game_def.menu_key ? game_def.menu_key : SDL_SCANCODE_F1;
}

int sdl_commands_first(void)
{
	return game_def.joystick ? 0 : SdlCommandCount;
}

int sdl_commands_end(void)
{
	return SdlCommandCount + (int)game_def.command_count;
}

const char *sdl_command_label(int command)
{
	return command < SdlCommandCount ? bind_labels[command] : game_def.commands[command - SdlCommandCount].label;
}

int sdl_command_pad(int command)
{
	return command < SdlCommandCount;
}

static const char *bind_id(int command)
{
	return command < SdlCommandCount ? bind_ids[command] : game_def.commands[command - SdlCommandCount].id;
}

/* Kind 0 is the keys, stored under `bind`; kind 1 the gamepad buttons, under `bind_pad`. */
static const char *const bind_keys[2] = {"bind", "bind_pad"};

static int bind_kind(int input)
{
	return input >= SDL_BIND_PAD(0);
}

/* Appends `input` bound to `command` to t[*n], unless t already holds it. */
static void bind_default(int input, int command, struct SdlBinding *t, int *n)
{
	int i;
	for(i = 0; i < *n && t[i].input != input; i++)
		;
	if(i < *n || *n == SDL_BINDINGS_MAX)
		return;
	t[*n].input = input;
	t[*n].command = command;
	(*n)++;
}

/* Appends the defaults of one kind to t[*n]: the joystick's, then the game's commands' keys. */
static void bind_defaults_add(int kind, struct SdlBinding *t, int *n)
{
	uint32_t c;
	int i;
	for(i = 0; game_def.joystick && i < BIND_DEFAULTS; i++)
		if(bind_kind(bind_defaults[i].input) == kind)
			bind_default(bind_defaults[i].input, bind_defaults[i].command, t, n);
	for(c = 0; kind == 0 && c < game_def.command_count; c++)
		for(i = 0; i < 2; i++)
			if(game_def.commands[c].keys[i])
				bind_default(game_def.commands[c].keys[i], SdlCommandCount + (int)c, t, n);
}

static void bind_defaults_all(void)
{
	bind_count = 0;
	bind_defaults_add(0, bind_table, &bind_count);
	bind_defaults_add(1, bind_table, &bind_count);
}

static void bind_ready(void)
{
	if(bind_count < 0)
		bind_defaults_all();
}

static int bind_find(int input)
{
	int i;
	bind_ready();
	for(i = 0; i < bind_count; i++)
		if(bind_table[i].input == input)
			return i;
	return -1;
}

int sdl_bindings_pad_button(int button)
{
	return button > SDL_GAMEPAD_BUTTON_INVALID && button < SDL_GAMEPAD_BUTTON_COUNT && button != SDL_GAMEPAD_BUTTON_GUIDE
		&& (button < SDL_GAMEPAD_BUTTON_DPAD_UP || button > SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
}

static int bind_valid(int input, int command)
{
	if(command < sdl_commands_first() || command >= sdl_commands_end())
		return 0;
	if(input >= SDL_BIND_PAD(0))
		return sdl_command_pad(command) && sdl_bindings_pad_button(input - SDL_BIND_PAD(0));
	return input > SDL_SCANCODE_UNKNOWN && input < SDL_SCANCODE_COUNT;
}

/* Appends `<input>:<command id>` pairs, separated by spaces, to t[*n]: an input is a scancode for
 * kind 0 and an SDL gamepad button name for kind 1. -1 on a bad pair, with *n unchanged. */
static int bind_parse(const char *text, int kind, struct SdlBinding *t, int *n)
{
	const char *p = text;
	int k = *n;
	for(;;)
	{
		char name[32];
		long input;
		size_t len;
		int c, i;
		while(*p == ' ')
			p++;
		if(!*p)
			break;
		len = strcspn(p, ": ");
		if(p[len] != ':' || len >= sizeof name || k == SDL_BINDINGS_MAX)
			return -1;
		memcpy(name, p, len);
		name[len] = 0;
		if(kind)
			input = SDL_BIND_PAD(SDL_GetGamepadButtonFromString(name));
		else
		{
			char *end;
			if(name[0] < '0' || name[0] > '9')
				return -1;
			input = strtol(name, &end, 10);
			if(*end)
				return -1;
		}
		if(bind_kind((int)input) != kind)
			return -1;
		p += len + 1;
		len = strcspn(p, " ");
		for(c = sdl_commands_first(); c < sdl_commands_end() && (strlen(bind_id(c)) != len || strncmp(p, bind_id(c), len)); c++)
			;
		if(!bind_valid((int)input, c))
			return -1;
		for(i = *n; i < k; i++)
			if(t[i].input == (int)input)
				return -1;
		t[k].input = (int)input;
		t[k].command = c;
		k++;
		p += len;
	}
	*n = k;
	return 0;
}

static void bind_store(void)
{
	int kind, changed = 0;
	for(kind = 0; kind < 2; kind++)
	{
		char text[SDL_BINDINGS_MAX * 32];
		size_t n = 0;
		int i;
		text[0] = 0;
		for(i = 0; i < bind_count; i++)
		{
			const int input = bind_table[i].input;
			if(bind_kind(input) != kind)
				continue;
			if(kind)
				n += (size_t)snprintf(text + n, sizeof text - n, "%s%s:%s", n ? " " : "",
					SDL_GetGamepadStringForButton((SDL_GamepadButton)(input - SDL_BIND_PAD(0))), bind_id(bind_table[i].command));
			else
				n += (size_t)snprintf(text + n, sizeof text - n, "%s%d:%s", n ? " " : "", input, bind_id(bind_table[i].command));
		}
		changed |= sdl_settings_set(bind_keys[kind], text) != 0;
	}
	if(changed)
		sdl_settings_save();
}

void sdl_bindings_apply(void)
{
	struct SdlBinding t[SDL_BINDINGS_MAX];
	int n = 0, kind;
	for(kind = 0; kind < 2; kind++)
	{
		const char *text = sdl_settings_get(bind_keys[kind]);
		if(text && bind_parse(text, kind, t, &n))
			fprintf(stderr, "settings.ini: ignoring %s=%s\n", bind_keys[kind], text);
		else if(text)
			continue;
		bind_defaults_add(kind, t, &n);
	}
	memcpy(bind_table, t, (size_t)n * sizeof t[0]);
	bind_count = n;
}

int sdl_bindings_count(void)
{
	bind_ready();
	return bind_count;
}

const struct SdlBinding *sdl_bindings_get(int i)
{
	bind_ready();
	return i >= 0 && i < bind_count ? &bind_table[i] : 0;
}

int sdl_bindings_add(int input, int command)
{
	int i = bind_find(input);
	if(!bind_valid(input, command))
		return -1;
	if(i < 0)
	{
		if(bind_count == SDL_BINDINGS_MAX)
			return -1;
		i = bind_count++;
		bind_table[i].input = input;
	}
	bind_table[i].command = command;
	bind_store();
	return 0;
}

void sdl_bindings_remove(int input)
{
	int i = bind_find(input);
	if(i < 0)
		return;
	memmove(&bind_table[i], &bind_table[i + 1], (size_t)(bind_count - i - 1) * sizeof bind_table[0]);
	bind_count--;
	bind_store();
}

void sdl_bindings_name(int input, char *out, int size)
{
	if(input >= SDL_BIND_PAD(0))
	{
		const char *name = SDL_GetGamepadStringForButton((SDL_GamepadButton)(input - SDL_BIND_PAD(0)));
		snprintf(out, (size_t)size, "Pad %s", name ? name : "?");
		if(name && !name[1] && size > 5)
			out[4] = (char)(out[4] - 'a' + 'A');
	}
	else
	{
		const char *name = SDL_GetScancodeName((SDL_Scancode)input);
		if(*name)
			snprintf(out, (size_t)size, "%s", name);
		else
			snprintf(out, (size_t)size, "Key %d", input);
	}
}

void sdl_bindings_reset(void)
{
	bind_defaults_all();
	bind_store();
}

int sdl_bindings_key(int scancode, int down)
{
	const int i = bind_find(scancode);
	if(i < 0 || bind_table[i].command >= SdlCommandCount)
		return 0;
	bind_held[scancode] = (uint8_t)(down != 0);
	return 1;
}

int sdl_bindings_amiga_key(int scancode)
{
	const int i = bind_find(scancode);
	return i >= 0 && bind_table[i].command >= SdlCommandCount ? game_def.commands[bind_table[i].command - SdlCommandCount].amiga : -1;
}

void sdl_bindings_release(void)
{
	memset(bind_held, 0, sizeof bind_held);
}

uint8_t sdl_bindings_joystick(void)
{
	uint8_t joy = 0;
	int i;
	bind_ready();
	for(i = 0; i < bind_count; i++)
		if(bind_table[i].input < SDL_SCANCODE_COUNT && bind_table[i].command < SdlCommandCount && bind_held[bind_table[i].input])
			joy |= bind_joy[bind_table[i].command];
	return joy;
}

uint8_t sdl_bindings_gamepad(SDL_Gamepad *pad)
{
	uint8_t joy = 0;
	int i;
	bind_ready();
	for(i = 0; i < bind_count; i++)
		if(bind_table[i].input >= SDL_BIND_PAD(0) && SDL_GetGamepadButton(pad, (SDL_GamepadButton)(bind_table[i].input - SDL_BIND_PAD(0))))
			joy |= bind_joy[bind_table[i].command];
	return joy;
}
