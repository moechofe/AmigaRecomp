#include "amiga.h"
#include "game.h"

/* An AmigaDOS volume whose root block (block 880, docs/formats/adf.md) names it `name`: the
 * game's four disks are the volumes PF1 to PF4 (docs/format.md, "Disks"). */
static int pinballfantasiesaga_volume(const uint8_t *adf, uint32_t size, const char *name)
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

static int pinballfantasiesaga_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return pinballfantasiesaga_volume(adf, size, "PF1");
}

static int pinballfantasiesaga_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return pinballfantasiesaga_volume(adf, size, "PF2");
}

static int pinballfantasiesaga_identify_disk3(const uint8_t *adf, uint32_t size)
{
	return pinballfantasiesaga_volume(adf, size, "PF3");
}

static int pinballfantasiesaga_identify_disk4(const uint8_t *adf, uint32_t size)
{
	return pinballfantasiesaga_volume(adf, size, "PF4");
}

static const struct GameDisk pinballfantasiesaga_disks[] = {
	{"disk1", "Disk 1", 0, pinballfantasiesaga_identify_disk1},
	{"disk2", "Disk 2", 0, pinballfantasiesaga_identify_disk2},
	{"disk3", "Disk 3", 0, pinballfantasiesaga_identify_disk3},
	{"disk4", "Disk 4", 0, pinballfantasiesaga_identify_disk4},
};

/* The two-disk repacks hold every table on PF1 and PF2; the four-disk set needs PF3 and PF4 too,
 * each in a drive of its own (docs/format.md, "Disks and drives"). */
static const int pinballfantasiesaga_drives_all[4] = {0, 1, 2, 3};

/* A table's game starts on the function keys (F1: one player; docs/format.md, "Controls"), so
 * the menu is on F11 (USB HID 0x44), F5 and F9 reach the game, states are saved and loaded from
 * the menu, and the window grabs the keyboard so Alt reaches the flippers. */
const struct GameDef game_def = {
	.id = "pinballfantasiesaga",
	.title = "Pinball Fantasies AGA",
	.disks = pinballfantasiesaga_disks,
	.disk_count = 4,
	.drives = {0, 1, -1, -1},
	.drives_optional = pinballfantasiesaga_drives_all,
	.drive_count = 4,
	.view = {129, 28, 320, 274},
	.menu_key = 0x44,
	.no_state_keys = 1,
	.grab_keyboard = 1,
};
