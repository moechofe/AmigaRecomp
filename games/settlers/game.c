#include "amiga.h"
#include "game.h"

/* CRC-32 of the root block's volume name, its length byte and 14 characters
 * (ADF 0x6E1B0; docs/format.md, "Identification"). */
static int settlers_volume(const uint8_t *adf, uint32_t size, uint32_t crc)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x6E1B0, 15) == crc;
}

static int settlers_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return settlers_volume(adf, size, 0x6F4FFD80u) || settlers_volume(adf, size, 0xED768688u);
}

static int settlers_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return settlers_volume(adf, size, 0xF646AC3Au);
}

static int settlers_identify_disk3(const uint8_t *adf, uint32_t size)
{
	return settlers_volume(adf, size, 0x81419CACu);
}

static const struct GameDisk settlers_disks[] = {
	{"disk1", "Disk 1 (Intro)", 0, settlers_identify_disk1},
	{"disk2", "Disk 2", 0, settlers_identify_disk2},
	{"disk3", "Disk 3 (Game)", 0, settlers_identify_disk3},
};

/* One drive: every program reads only DF0 and asks there for the disk it wants. Disk 3 starts
 * the game, which with 1 MB of chip RAM asks for Disk 2 after the title, for its music. Disk 1
 * is the intro: given it, the game boots from Disk 1, the intro asks for Disk 2 and then Disk 3,
 * and its loader starts the game from Disk 3 (docs/format.md, "Boot chains"). */
static const int settlers_intro_drives[4] = {0, -1, -1, -1};

extern const struct GameHack settlers_hack_system_cursor;

static const struct GameHack *const settlers_hacks[] = {&settlers_hack_system_cursor};

const struct GameDef game_def = {
	.id = "settlers",
	.title = "The Settlers",
	.disks = settlers_disks,
	.disk_count = 3,
	.drives = {2, -1, -1, -1},
	.drives_optional = settlers_intro_drives,
	.drive_count = 1,
	.pointer_sprites = 0x01,
	.view = {120, 32, 335, 273},
	.hacks = settlers_hacks,
	.hack_count = 1,
	.system_cursor = &settlers_hack_system_cursor,
};
