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

extern const struct GameHack deuteros_hack_keep_pointer, deuteros_hack_space_advance;

static const struct GameHack *const deuteros_hacks[] = {&deuteros_hack_keep_pointer, &deuteros_hack_space_advance};

const struct GameDef game_def = {
	.id = "deuteros",
	.title = "Deuteros: The Next Millennium",
	.disks = deuteros_disks,
	.disk_count = 3,
	.drives = {0, 2, -1, -1},
	.drive_count = 2,
	.pointer_sprite = 0,
	.hacks = deuteros_hacks,
	.hack_count = 2,
};
