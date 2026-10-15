#include "amiga.h"
#include "game.h"

static int lotus2_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x1600, 0x1600) == 0x61F4A0C7u;
}

static const struct GameDisk lotus2_disks[] = {
	{"disk1", "Disk", 0, lotus2_identify_disk1},
};

const struct GameDef game_def = {
	.id = "lotus2",
	.title = "Lotus Turbo Challenge 2",
	.disks = lotus2_disks,
	.disk_count = 1,
	.drives = {0, -1, -1, -1},
	.drive_count = 1,
	.view = {129, 44, 320, 200},
	.joystick = 2,
};
