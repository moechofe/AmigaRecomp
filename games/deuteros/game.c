#include "game.h"

static const struct GameDisk deuteros_disks[] = {
	{"disk1", "Disk 1", 0},
	{"disk2", "Disk 2", 0},
	{"save", "Save disk", 1},
};

static const int deuteros_df0_cycle[] = {0, 1, 2};

const struct GameDef game_def = {
	.id = "deuteros",
	.title = "Deuteros: The Next Millennium",
	.disks = deuteros_disks,
	.disk_count = 3,
	.drives = {0, 2, -1, -1},
	.df0_cycle = deuteros_df0_cycle,
	.df0_cycle_count = 3,
	.pointer_sprite = 0,
};
