#ifndef GAME_H
#define GAME_H

#include <stdint.h>

struct GameDisk
{
	const char *id;    /* role: "disk1", "save", … */
	const char *label; /* shown in messages */
	int writable;      /* 1: our file <pref>/<id>.adf, zero-filled when absent */
};
struct GameDef
{
	const char *id, *title;
	const struct GameDisk *disks; uint32_t disk_count;
	int drives[4];               /* role index in DF0–DF3 at boot, -1 empty */
	const int *df0_cycle; uint32_t df0_cycle_count; /* F11 order */
	int pointer_sprite;          /* sprite holding the game's mouse pointer, -1 none */
};
extern const struct GameDef game_def;

#endif
