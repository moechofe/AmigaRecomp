#include "bindings.h"

#include "amiga.h"
#include "game.h"
#include "settings.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const sdl_command_labels[SdlCommandCount] = {"Joystick up", "Joystick down", "Joystick left", "Joystick right", "Joystick fire"};
static const char *const bind_ids[SdlCommandCount] = {"joy_up", "joy_down", "joy_left", "joy_right", "joy_fire"};
static const uint8_t bind_joy[SdlCommandCount] = {AmigaJoyUp, AmigaJoyDown, AmigaJoyLeft, AmigaJoyRight, AmigaJoyFire};

static const struct SdlBinding bind_defaults[] = {
	{SDL_SCANCODE_UP, SdlCommandJoyUp},
	{SDL_SCANCODE_DOWN, SdlCommandJoyDown},
	{SDL_SCANCODE_LEFT, SdlCommandJoyLeft},
	{SDL_SCANCODE_RIGHT, SdlCommandJoyRight},
	{SDL_SCANCODE_RCTRL, SdlCommandJoyFire},
	{SDL_SCANCODE_LCTRL, SdlCommandJoyFire},
};
#define BIND_DEFAULTS (int)(sizeof bind_defaults / sizeof bind_defaults[0])

static struct SdlBinding bind_table[SDL_BINDINGS_MAX];
static int bind_count = -1;  /* -1: the defaults, not yet copied */
static uint8_t bind_held[SDL_SCANCODE_COUNT];

int sdl_menu_key(void)
{
	return game_def.menu_key ? game_def.menu_key : SDL_SCANCODE_F1;
}

static void bind_ready(void)
{
	if(bind_count >= 0)
		return;
	memcpy(bind_table, bind_defaults, sizeof bind_defaults);
	bind_count = BIND_DEFAULTS;
}

static int bind_find(int scancode)
{
	int i;
	bind_ready();
	for(i = 0; i < bind_count; i++)
		if(bind_table[i].scancode == scancode)
			return i;
	return -1;
}

/* `<scancode>:<command id>`, separated by spaces; 0, or -1 with the table unchanged. */
static int bind_parse(const char *text)
{
	struct SdlBinding t[SDL_BINDINGS_MAX];
	const char *p = text;
	int n = 0;
	for(;;)
	{
		char *end;
		long sc;
		size_t len;
		int c, i;
		while(*p == ' ')
			p++;
		if(!*p)
			break;
		if(*p < '0' || *p > '9')
			return -1;
		sc = strtol(p, &end, 10);
		if(*end != ':' || sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT || n == SDL_BINDINGS_MAX)
			return -1;
		p = end + 1;
		len = strcspn(p, " ");
		for(c = 0; c < SdlCommandCount && (strlen(bind_ids[c]) != len || strncmp(p, bind_ids[c], len)); c++)
			;
		if(c == SdlCommandCount)
			return -1;
		for(i = 0; i < n; i++)
			if(t[i].scancode == (int)sc)
				return -1;
		t[n].scancode = (int)sc;
		t[n].command = (enum SdlCommand)c;
		n++;
		p += len;
	}
	memcpy(bind_table, t, (size_t)n * sizeof t[0]);
	bind_count = n;
	return 0;
}

static void bind_store(void)
{
	char text[SDL_BINDINGS_MAX * 16];
	size_t n = 0;
	int i;
	text[0] = 0;
	for(i = 0; i < bind_count; i++)
		n += (size_t)snprintf(text + n, sizeof text - n, "%s%d:%s", i ? " " : "", bind_table[i].scancode, bind_ids[bind_table[i].command]);
	if(sdl_settings_set("bind", text))
		sdl_settings_save();
}

void sdl_bindings_apply(void)
{
	const char *text = sdl_settings_get("bind");
	bind_count = -1;
	if(text && bind_parse(text))
		fprintf(stderr, "settings.ini: ignoring bind=%s\n", text);
	bind_ready();
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

int sdl_bindings_add(int scancode, enum SdlCommand command)
{
	int i = bind_find(scancode);
	if(scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT || (unsigned)command >= SdlCommandCount)
		return -1;
	if(i < 0)
	{
		if(bind_count == SDL_BINDINGS_MAX)
			return -1;
		i = bind_count++;
		bind_table[i].scancode = scancode;
	}
	bind_table[i].command = command;
	bind_store();
	return 0;
}

void sdl_bindings_remove(int scancode)
{
	int i = bind_find(scancode);
	if(i < 0)
		return;
	memmove(&bind_table[i], &bind_table[i + 1], (size_t)(bind_count - i - 1) * sizeof bind_table[0]);
	bind_count--;
	bind_store();
}

void sdl_bindings_reset(void)
{
	memcpy(bind_table, bind_defaults, sizeof bind_defaults);
	bind_count = BIND_DEFAULTS;
	bind_store();
}

int sdl_bindings_key(int scancode, int down)
{
	if(!game_def.joystick || bind_find(scancode) < 0)
		return 0;
	bind_held[scancode] = (uint8_t)(down != 0);
	return 1;
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
		if(bind_held[bind_table[i].scancode])
			joy |= bind_joy[bind_table[i].command];
	return joy;
}
