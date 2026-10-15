#include "pad.h"

#include "amiga.h"
#include "game.h"

#include <stdio.h>

/* A gamepad SDL knows the layout of reads through SDL's gamepad mapping; any other device as a
 * plain joystick: axes 0 and 1, hat 0, and every button. */
struct SdlPad { SDL_JoystickID id; SDL_Gamepad *pad; SDL_Joystick *joy; int port; };

static struct SdlPad *pads;
static int pad_count, pad_room;

void sdl_pad_init(void)
{
	if(game_def.joystick && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		fprintf(stderr, "SDL_InitSubSystem(gamepad): %s\n", SDL_GetError());
}

/* Player k is game port 1 - k: player 1 port 2, player 2 port 1. */
static int pad_free_port(void)
{
	int k, i;
	for(k = 0; k < game_def.joystick && k < 2; k++)
	{
		for(i = 0; i < pad_count && pads[i].port != 1 - k; i++)
			;
		if(i == pad_count)
			return 1 - k;
	}
	return 1;
}

static void pad_add(SDL_JoystickID id)
{
	struct SdlPad p = {id, 0, 0, 0};
	const char *name;
	p.port = pad_free_port();
	if(SDL_IsGamepad(id))
		p.pad = SDL_OpenGamepad(id);
	else
		p.joy = SDL_OpenJoystick(id);
	if(!p.pad && !p.joy)
	{
		fprintf(stderr, "controller %u: %s\n", (unsigned)id, SDL_GetError());
		return;
	}
	if(pad_count == pad_room)
	{
		int room = pad_room ? pad_room * 2 : 4;
		struct SdlPad *grown = (struct SdlPad *)SDL_realloc(pads, (size_t)room * sizeof *pads);
		if(!grown)
		{
			if(p.pad)
				SDL_CloseGamepad(p.pad);
			else
				SDL_CloseJoystick(p.joy);
			return;
		}
		pads = grown;
		pad_room = room;
	}
	pads[pad_count++] = p;
	name = p.pad ? SDL_GetGamepadName(p.pad) : SDL_GetJoystickName(p.joy);
	printf("controller: %s (%s), player %d in port %d\n", name ? name : "unnamed", p.pad ? "gamepad" : "joystick", 2 - p.port, p.port + 1);
}

static void pad_remove(SDL_JoystickID id)
{
	int i;
	for(i = 0; i < pad_count; i++)
		if(pads[i].id == id)
		{
			if(pads[i].pad)
				SDL_CloseGamepad(pads[i].pad);
			else
				SDL_CloseJoystick(pads[i].joy);
			pads[i] = pads[--pad_count];
			return;
		}
}

int sdl_pad_event(const SDL_Event *e)
{
	if(e->type == SDL_EVENT_JOYSTICK_ADDED)
		pad_add(e->jdevice.which);
	else if(e->type == SDL_EVENT_JOYSTICK_REMOVED)
		pad_remove(e->jdevice.which);
	else
		return 0;
	return 1;
}

/* An axis leans a way once it is past half its range, the threshold SDL's gamepad mapping uses
 * for an axis bound to a button (SDL_gamepad.c). */
static uint8_t pad_axes(int x, int y)
{
	uint8_t joy = 0;
	if(x <= SDL_JOYSTICK_AXIS_MIN / 2)
		joy |= AmigaJoyLeft;
	else if(x >= SDL_JOYSTICK_AXIS_MAX / 2)
		joy |= AmigaJoyRight;
	if(y <= SDL_JOYSTICK_AXIS_MIN / 2)
		joy |= AmigaJoyUp;
	else if(y >= SDL_JOYSTICK_AXIS_MAX / 2)
		joy |= AmigaJoyDown;
	return joy;
}

static uint8_t pad_gamepad(SDL_Gamepad *g)
{
	uint8_t joy = pad_axes(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY));
	if(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP))
		joy |= AmigaJoyUp;
	if(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
		joy |= AmigaJoyDown;
	if(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT))
		joy |= AmigaJoyLeft;
	if(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
		joy |= AmigaJoyRight;
	if(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST)
		|| SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST) || SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_NORTH))
		joy |= AmigaJoyFire;
	return joy;
}

static uint8_t pad_joystick(SDL_Joystick *j)
{
	uint8_t joy = 0, hat;
	int i, n;
	if(SDL_GetNumJoystickAxes(j) >= 2)
		joy = pad_axes(SDL_GetJoystickAxis(j, 0), SDL_GetJoystickAxis(j, 1));
	if(SDL_GetNumJoystickHats(j) >= 1)
	{
		hat = SDL_GetJoystickHat(j, 0);
		if(hat & SDL_HAT_UP)
			joy |= AmigaJoyUp;
		if(hat & SDL_HAT_DOWN)
			joy |= AmigaJoyDown;
		if(hat & SDL_HAT_LEFT)
			joy |= AmigaJoyLeft;
		if(hat & SDL_HAT_RIGHT)
			joy |= AmigaJoyRight;
	}
	n = SDL_GetNumJoystickButtons(j);
	for(i = 0; i < n; i++)
		if(SDL_GetJoystickButton(j, i))
			joy |= AmigaJoyFire;
	return joy;
}

uint8_t sdl_pad_joystick(int port)
{
	uint8_t joy = 0;
	int i;
	for(i = 0; i < pad_count; i++)
		if(pads[i].port == port)
			joy |= pads[i].pad ? pad_gamepad(pads[i].pad) : pad_joystick(pads[i].joy);
	return joy;
}
