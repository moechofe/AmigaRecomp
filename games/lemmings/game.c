#include "game.h"

static const struct GameDisk lemmings_disks[] = {
	{"disk1", "Disk 1", 0},
	{"disk2", "Disk 2", 0},
};

static const int lemmings_df0_cycle[] = {0, 1};

const struct GameDef game_def = {
	.id = "lemmings",
	.title = "Lemmings",
	.disks = lemmings_disks,
	.disk_count = 2,
	.drives = {0, 1, -1, -1},
	.df0_cycle = lemmings_df0_cycle,
	.df0_cycle_count = 2,
	.pointer_sprite = 0,
};
