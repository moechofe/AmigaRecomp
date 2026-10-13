#include "amiga.h"
#include "game.h"

static int pirates_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x6E1B0, 9) == 0x5BE9DB42u;
}

static int pirates_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x6E1B0, 9) == 0xC2E08AF8u;
}

static const struct GameDisk pirates_disks[] = {
	{"disk1", "Disk 1", 0, pirates_identify_disk1},
	{"disk2", "Disk 2", 0, pirates_identify_disk2},
};

const struct GameDef game_def = {
	.id = "pirates",
	.title = "Pirates!",
	.disks = pirates_disks,
	.disk_count = 2,
	.drives = {0, 1, -1, -1},
	.drive_count = 2,
	.pointer_sprites = 0x01,
	.view = {129, 64, 320, 200},
	.joystick = 1,
};
