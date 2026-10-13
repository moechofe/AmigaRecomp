#include "amiga.h"
#include "game.h"

static const uint32_t deuteros_stage2[] = {0x0FA5E766u, 0xA2FB4C87u, 0x36063CE7u, 0xDB749C89u};

static int deuteros_identify_disk1(const uint8_t *adf, uint32_t size)
{
	uint32_t crc, i;
	if(size != AMIGA_ADF_SIZE)
		return 0;
	crc = amiga_crc32(adf + 0x2C00, 0x1600);
	for(i = 0; i < sizeof deuteros_stage2 / sizeof deuteros_stage2[0]; i++)
		if(crc == deuteros_stage2[i])
			return 1;
	return 0;
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

extern const struct GameHack deuteros_hack_keep_pointer, deuteros_hack_space_advance, deuteros_hack_system_cursor;

static const struct GameHack *const deuteros_hacks[] = {&deuteros_hack_keep_pointer, &deuteros_hack_space_advance, &deuteros_hack_system_cursor};

const struct GameDef game_def = {
	.id = "deuteros",
	.title = "Deuteros: The Next Millennium",
	.disks = deuteros_disks,
	.disk_count = 3,
	.drives = {0, 2, -1, -1},
	.drive_count = 2,
	.pointer_sprites = 0x01,
	.view = {129, 56, 320, 200},
	.hacks = deuteros_hacks,
	.hack_count = 3,
	.system_cursor = &deuteros_hack_system_cursor,
};
