#include "amiga.h"
#include "game.h"

static int lemmings_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0xCA000, 0x1200) == 0x2D67A380u;
}

static int lemmings_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x21000, 0x1600) == 0x59BD5285u;
}

static const struct GameDisk lemmings_disks[] = {
	{"disk1", "Disk 1", 0, lemmings_identify_disk1},
	{"disk2", "Disk 2", 0, lemmings_identify_disk2},
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
