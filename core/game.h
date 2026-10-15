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
struct GameKey { uint16_t host; uint8_t amiga; }; /* a host key, by its USB HID usage ID, and the Amiga raw key code it sends */
struct GameDef
{
	const char *id, *title;
	const struct GameDisk *disks; uint32_t disk_count;
	int drives[4];               /* role index in DF0–DF3 at boot, -1 empty */
	const int *drives_optional;  /* NULL, or 4 role indexes (-1 empty): DF0–DF3 at boot in place of drives[] when every role they name has an image; drives[] alone decides when the machine boots */
	int drive_count;             /* DF0–DF(n-1): the machine's drives, which the Disks page offers, 1–4 */
	uint8_t pointer_sprites;     /* bit n: sprite n draws part of the game's mouse pointer; steering follows the lowest; 0 none */
	struct GameView view;        /* what the window shows; w 0: the capture window */
	int joystick;                /* joysticks the game reads: 1 port 2, worked by the keys bound on the menu's Controls page and game controllers; 2 also port 1, in place of the mouse, worked by the second controller; 0 none */
	const struct GameHack *const *hacks; uint32_t hack_count;
	const struct GameHack *system_cursor; /* the bool hack that, while on, shows the OS cursor over the running game in place of pointer_sprites; NULL: the OS cursor always shows, beside them */
	uint16_t menu_key;           /* USB HID usage ID of the host key that opens the menu; 0: F1 (0x3A) */
	int no_state_keys;           /* 1: F5 and F9 reach the game, and states are saved and loaded from the menu alone; 0: F5 saves, F9 loads */
	const struct GameKey *keys; uint32_t key_count; /* host keys that send another Amiga key than their own; NULL none */
	int grab_keyboard;           /* 1: while the game runs, the window grabs the keyboard, so desktop shortcuts such as Alt+Space reach the game */
};
extern const struct GameDef game_def;
int game_anchor_ok(const struct Amiga *m, const struct GameAnchor *a);

#endif
