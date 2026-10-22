#include "amiga.h"
#include "game.h"

/* CRC-32 of ADF 0x004200-0x0057FF, track 3 (docs/format.md, "Identification"). */
static int superfrog_track3(const uint8_t *adf, uint32_t size, uint32_t crc)
{
	return size == AMIGA_ADF_SIZE && amiga_crc32(adf + 0x4200, 0x1600) == crc;
}

static int superfrog_identify_disk1(const uint8_t *adf, uint32_t size)
{
	return superfrog_track3(adf, size, 0xB94C2C45u);
}

static int superfrog_identify_disk2(const uint8_t *adf, uint32_t size)
{
	return superfrog_track3(adf, size, 0x31B807D3u);
}

static int superfrog_identify_disk3(const uint8_t *adf, uint32_t size)
{
	return superfrog_track3(adf, size, 0xB0C16D95u);
}

static int superfrog_identify_disk4(const uint8_t *adf, uint32_t size)
{
	return superfrog_track3(adf, size, 0x99ED3020u);
}

static const struct GameDisk superfrog_disks[] = {
	{"disk1", "Disk 1", 0, superfrog_identify_disk1},
	{"disk2", "Disk 2", 0, superfrog_identify_disk2},
	{"disk3", "Disk 3 (Intro)", 0, superfrog_identify_disk3},
	{"disk4", "Disk 4", 0, superfrog_identify_disk4},
};

extern const struct GameHack superfrog_hack_jump_on_fire;
static const struct GameHack *const superfrog_hacks[] = {&superfrog_hack_jump_on_fire};

const struct GameDef game_def = {
	.id = "superfrog",
	.title = "Superfrog",
	.disks = superfrog_disks,
	.disk_count = 4,
	.drives = {0, -1, -1, -1},
	.drive_count = 1,
	.view = {121, 39, 334, 261},
	.joystick = 1,
	.hacks = superfrog_hacks,
	.hack_count = 1,
};
