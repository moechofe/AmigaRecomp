#include "amiga.h"
#include "game.h"

/* An AmigaDOS volume whose root block (block 880, docs/formats/adf.md) names it `name`: the
 * game's five disks are the volumes Slam1 to Slam5 (docs/format.md, "Disks and drives"). */
static int slamtilt_volume(const uint8_t *adf, uint32_t size, const char *name)
{
	const uint8_t *root = adf + 880u * 512u;
	uint32_t i, len;
	if(size != AMIGA_ADF_SIZE || adf[0] != 'D' || adf[1] != 'O' || adf[2] != 'S')
		return 0;
	len = root[512 - 80];
	for(i = 0; name[i]; i++)
		if(i >= len || root[512 - 79 + i] != (uint8_t)name[i])
			return 0;
	return i == len;
}

static int slamtilt_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return slamtilt_volume(adf, size, "Slam1");
}

static int slamtilt_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return slamtilt_volume(adf, size, "Slam2");
}

static int slamtilt_identify_disk3(const uint8_t *adf, uint32_t size)
{
	return slamtilt_volume(adf, size, "Slam3");
}

static int slamtilt_identify_disk4(const uint8_t *adf, uint32_t size)
{
	return slamtilt_volume(adf, size, "Slam4");
}

static int slamtilt_identify_disk5(const uint8_t *adf, uint32_t size)
{
	return slamtilt_volume(adf, size, "Slam5");
}

static const struct GameDisk slamtilt_disks[] = {
	{"disk1", "Disk 1", 0, slamtilt_identify_disk1},
	{"disk2", "Disk 2 (Mean Machines)", 0, slamtilt_identify_disk2},
	{"disk3", "Disk 3 (The Pirate)", 0, slamtilt_identify_disk3},
	{"disk4", "Disk 4 (Ace of Space)", 0, slamtilt_identify_disk4},
	{"disk5", "Disk 5 (Night of the Demon)", 0, slamtilt_identify_disk5},
};

/* The oracle's four drives hold Disks 1 to 4; Disk 1 alone boots the game (docs/format.md,
 * "Disks and drives"). */
static const int slamtilt_drives_all[4] = {0, 1, 2, 3};

/* The Amiga's Help (raw 0x5F, HRM appendix H) switches a table to hires and has no key of its
 * own on a PC keyboard: Insert (0x49) and Home (0x4A) send it. Page Down (0x4E) sends Del (raw
 * 0x46), which switches it back to lores (docs/format.md, "Controls"). */
static const struct GameKey slamtilt_keys[] = {
	{0x49, 0x5F},
	{0x4A, 0x5F},
	{0x4E, 0x46},
};

/* F1 starts a game on a table (docs/format.md, "Controls"), so the menu is on F11 (USB HID 0x44),
 * F5 and F9 reach the game, states are saved and loaded from the menu, and the window grabs the
 * keyboard so the Amiga keys reach the nudges. */
const struct GameDef game_def = {
	.id = "slamtilt",
	.title = "Slam Tilt",
	.disks = slamtilt_disks,
	.disk_count = 5,
	.drives = {0, -1, -1, -1},
	.drives_optional = slamtilt_drives_all,
	.drive_count = 4,
	.view = {129, 44, 322, 256},
	.menu_key = 0x44,
	.no_state_keys = 1,
	.keys = slamtilt_keys,
	.key_count = 3,
	.grab_keyboard = 1,
};
