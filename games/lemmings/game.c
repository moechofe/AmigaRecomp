#include "game.h"

static const struct GameDisk lemmings_disks[] = {
	{"disk1", "Disk 1", 0},
	{"disk2", "Disk 2", 0},
};

const struct GameDef game_def = {
	.id = "lemmings",
	.title = "Lemmings",
	.disks = lemmings_disks,
	.disk_count = 2,
	.drives = {0, 1, -1, -1},
	.drive_count = 2,
	.pointer_sprite = 0,
	.view = {129, 44, 320, 208},
};
