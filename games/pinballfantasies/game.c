#include "amiga.h"
#include "game.h"

/* CRC-32 of ADF 0x006E00-0x0083FF, track 5 (docs/format.md, "Identification"). */
static int pinballfantasies_track5(const uint8_t *adf, uint32_t size, uint32_t crc)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x6E00, 0x1600) == crc;
}

/* The release and the 4-disk Skid Row Disk 1, the pre-release one, and the Two Live Crew one. */
static int pinballfantasies_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return pinballfantasies_track5(adf, size, 0x3918D3ABu) || pinballfantasies_track5(adf, size, 0x10DF738Fu)
		|| pinballfantasies_track5(adf, size, 0x628A1EFEu);
}

/* Every Disk 2 but the Two Live Crew one, and that one. */
static int pinballfantasies_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return pinballfantasies_track5(adf, size, 0x4BE50DDDu) || pinballfantasies_track5(adf, size, 0xDDE24457u);
}

static int pinballfantasies_identify_disk3(const uint8_t *adf, uint32_t size)
{
	return pinballfantasies_track5(adf, size, 0x9B9FDFACu);
}

static const struct GameDisk pinballfantasies_disks[] = {
	{"disk1", "Disk 1", 0, pinballfantasies_identify_disk1},
	{"disk2", "Disk 2 (Partyland, Speed Devils)", 0, pinballfantasies_identify_disk2},
	{"disk3", "Disk 3 (Billion Dollar Gameshow, Stones 'N Bones)", 0, pinballfantasies_identify_disk3},
};

/* The game's keys F1-F8 pick the table and the number of players, so the menu is on F11 (USB
 * HID 0x44) and F5 and F9 reach the game; states are saved and loaded from the menu. The Amiga's
 * Help (raw 0x5F, HRM appendix H), with which the game changes course disk, has no key of its
 * own on a PC keyboard: Insert (0x49) and Home (0x4A) send it (docs/format.md, "Controls"). */
static const struct GameKey pinballfantasies_keys[] = {
	{0x49, 0x5F},
	{0x4A, 0x5F},
};

const struct GameDef game_def = {
	.id = "pinballfantasies",
	.title = "Pinball Fantasies",
	.disks = pinballfantasies_disks,
	.disk_count = 3,
	.drives = {0, -1, -1, -1},
	.drive_count = 2,
	.view = {129, 40, 320, 262},
	.menu_key = 0x44,
	.no_state_keys = 1,
	.keys = pinballfantasies_keys,
	.key_count = 2,
	.grab_keyboard = 1,
};
