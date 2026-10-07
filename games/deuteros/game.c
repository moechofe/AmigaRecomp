#include "amiga.h"
#include "game.h"

static int deuteros_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x2C00, 0x1600) == 0x0FA5E766u;
}

static int deuteros_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf, 0x400) == 0xE58D146Au;
}

static const struct GameDisk deuteros_disks[] = {
	{"disk1", "Disk 1", 0, deuteros_identify_disk1},
	{"disk2", "Disk 2", 0, deuteros_identify_disk2},
	{"save", "Save disk", 1, 0},
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
