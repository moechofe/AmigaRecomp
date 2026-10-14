#include "game.h"

/* docs/hacks.md, "system_cursor". */

const struct GameHack settlers_hack_system_cursor = {
	{"system_cursor", "Use the system cursor",
		"Original: the game draws its own pointer. Modern: the system cursor stays shown over the game and the game's pointer is not drawn. With Capture the mouse on, the game's pointer is drawn as in the original.",
		GameValueBool, 1, 0},
	0, 0, 0,
};
