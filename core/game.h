#ifndef GAME_H
#define GAME_H

#include <stdint.h>

struct Amiga;

struct GameAnchor { uint32_t addr; uint32_t len; uint32_t crc; }; /* CRC-32 (zlib) of the original bytes */
enum GameValueType { GameValueBool, GameValueChoice };
struct GameSetting
{
	const char *id;             /* settings.ini key, [a-z0-9_]+ */
	const char *label;
	const char *hint;
	enum GameValueType type;
	int def;                    /* default value; 0 is always the original behaviour */
	const char *const *choices; /* GameValueChoice: NULL-terminated ids, index 0 = original */
};
struct GameHook
{
	uint32_t pc;                        /* instruction the hook replaces */
	const struct GameAnchor *guard;     /* must match when pc is reached */
	int (*run)(struct Amiga *m, int value); /* 1: handled, PC and cycles set; 0: run the original */
};
struct GameHack
{
	struct GameSetting setting;
	const struct GameHook *hooks;
	uint32_t hook_count;
	void (*frame)(struct Amiga *m, int value); /* optional, after each frame; checks its own anchors */
};
struct GameDisk
{
	const char *id;    /* role: "disk1", "save", … */
	const char *label; /* shown in the menu and in messages */
	int writable;      /* 1: our file <pref>/<id>.adf, zero-filled when absent */
	int (*identify)(const uint8_t *adf, uint32_t size); /* unknown dumps: 1 = this role; NULL for writable roles */
};
struct GameView { int x, y, w, h; }; /* the game's picture: DIW x (lores) of its first column, its first scanline, its size */
struct GameDef
{
	const char *id, *title;
	const struct GameDisk *disks; uint32_t disk_count;
	int drives[4];               /* role index in DF0–DF3 at boot, -1 empty */
	int drive_count;             /* DF0–DF(n-1): the drives the Disks page offers, 1–4 */
	int pointer_sprite;          /* sprite holding the game's mouse pointer, -1 none */
	struct GameView view;        /* what the window shows; w 0: the capture window */
	const struct GameHack *const *hacks; uint32_t hack_count;
};
extern const struct GameDef game_def;
int game_anchor_ok(const struct Amiga *m, const struct GameAnchor *a);

#endif
