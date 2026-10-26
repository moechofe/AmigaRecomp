#include "amiga.h"
#include "bindings.h"
#include "disks.h"
#include "desktop.h"
#include "game.h"
#include "menu.h"
#include "pad.h"
#include "png.h"
#include "script.h"
#include "settings.h"
#include "stereo.h"
#include "video.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>

/* The executable is a GUI-subsystem program, so a double click opens no console. Started from a
   terminal, it attaches to that terminal and points stdout and stderr at it unless they were
   redirected. The C runtime checks its standard handles at startup, before the console is
   attached, so a console handle passed by the shell leaves the stream without a descriptor even
   though GetStdHandle still returns it. */
static void sdl_attach_console(void)
{
	if(!AttachConsole(ATTACH_PARENT_PROCESS))
		return;
	if(_fileno(stdout) < 0 || _get_osfhandle(_fileno(stdout)) < 0)
		freopen("CONOUT$", "w", stdout);
	if(_fileno(stderr) < 0 || _get_osfhandle(_fileno(stderr)) < 0)
		freopen("CONOUT$", "w", stderr);
}
#endif

static struct Amiga machine;
static uint8_t sdl_state_buf[STATE_MAX + 1];
static struct Script sdl_script;
static char sdl_script_text[1 << 16];
static char sdl_pref[1024];
static int sdl_booted;
static int sdl_report_errors;
static int sdl_fatal_shown;

#define SDL_MAX_PATHS 16
#define SDL_MAX_SCALE 32  /* --scale: 32 x 320 = 10 240 pixels wide, beyond any display */

struct SdlOptions
{
	int headless;
	long frames;
	const char *script;
	const char *shot_dir;
	const char *state;
	const char *dump;
	int no_sprites;
	int no_crt;
	int scale;
};

static void sdl_log(const char *text)
{
	printf("%s\n", text);
	fflush(stdout);
}

static int sdl_usage(void)
{
	fprintf(stderr,
		"usage: %s [-h | --help] [--headless] [--frames N] [--script FILE] [--shot-dir DIR]\n"
		"          [--state PATH] [--pref DIR] [--dump FILE] [--no-sprites] [--no-crt] [--scale N]\n"
		"          <image>...\n"
		"Run '%s --help' for what each argument does.\n",
		game_def.id, game_def.id);
	return 2;
}

static int sdl_help(void)
{
	uint32_t r;
	int n = 0;
	printf("%s, AmigaRecomp\n\n"
		"usage: %s [options] [image...]\n\n"
		"Images\n"
		"  Up to 16 disk images (.adf, .adz, .gz or .zip), in any order. Each is\n"
		"  identified by its CRC-32 and fills the disk it matches. This game reads:\n"
		"   ",
		game_def.title, game_def.id);
	for(r = 0; r < game_def.disk_count; r++)
		if(!game_def.disks[r].writable)
			printf("%s %s", n++ ? "," : "", game_def.disks[r].label);
	printf("\n");
	for(r = 0; r < game_def.disk_count; r++)
		if(game_def.disks[r].writable)
			printf("  %s is not given: it is kept in <pref>%s.adf.\n", game_def.disks[r].label, game_def.disks[r].id);
	printf("  In a window, images can also be dropped on it or opened from the menu\n"
		"  (%s), and the paths are remembered in <pref>settings.ini.\n\n"
		"Options\n"
		"  -h, --help       show this help and exit\n"
		"  --headless       no window, audio or pacing: run as fast as the host\n"
		"                   allows. Every image must be given and usable;\n"
		"                   settings.ini is neither read nor written and every hack\n"
		"                   is off unless the script turns it on\n"
		"  --frames N       headless: stop with exit 0 at frame N (0: no limit)\n"
		"  --script FILE    drive the game with an input script of at most 65535\n"
		"                   bytes (docs/runtime.md, \"Input scripts\"); in a window\n"
		"                   the script alone moves the pointer and hacks stay off\n"
		"  --shot-dir DIR   where screenshots go (F12, a script's 'shot');\n"
		"                   default <pref>shots\n"
		"  --state PATH     the save-state file (quick save / load, a script's 'state');\n"
		"                   default <pref>state.sav\n"
		"  --pref DIR       the folder for settings, writable disks, shots and\n"
		"                   states, created if missing; default the per-user folder\n"
		"                   SDL picks for AmigaRecomp/%s\n"
		"  --dump FILE      write chip RAM (512 KB, or 1 MB) to FILE at exit\n"
		"  --no-sprites     start with sprites hidden (a script's 'sprites on' shows\n"
		"                   them again)\n"
		"  --no-crt         window: draw the plain picture whatever the 'crt' setting\n"
		"                   says, until it is changed in the menu\n"
		"  --scale N        window: open at N times the game's view, N from 1 to %d,\n"
		"                   and draw at that whole multiple, or the largest that fits;\n"
		"                   overrides the 'scale' setting until it is changed in the menu\n\n"
		"Keys in the window\n"
		"  %-16s menu (disks, settings and hacks); pauses the game\n",
		SDL_GetScancodeName((SDL_Scancode)sdl_menu_key()), game_def.id, SDL_MAX_SCALE, SDL_GetScancodeName((SDL_Scancode)sdl_menu_key()));
	if(!game_def.no_state_keys)
		printf("  F5 / F9          save / load the state file\n");
	printf("  F12              screenshot into the shot directory\n"
		"  Alt+Enter        fullscreen on or off\n");
	if(game_def.joystick)
		printf("  bound keys       the joystick in port 2, bound on the menu's Controls page;\n"
			"                   by default the arrows, Ctrl (fire) and Space (button 2)\n"
			"  a controller     also the joystick in port 2: left stick or D-pad, and the\n"
			"                   bound buttons, by default A, X or Y fire and B is button 2\n"
			"                   (any button fires on a plain joystick)\n");
	if(game_def.joystick >= 2)
		printf("  2nd controller   the joystick in port 1, for player 2, in place of the mouse\n");
	printf("\n"
		"Exit codes\n"
		"  0 normal end, 1 fatal runtime error, 2 usage, image or script error,\n"
		"  3 script timeout\n");
	return 0;
}

static long sdl_read_file(const char *path, void *buf, long cap)
{
	FILE *f = fopen(path, "rb");
	long n;
	if(!f)
		return -1;
	n = (long)fread(buf, 1, (size_t)cap, f);
	fclose(f);
	return n;
}

static int sdl_write_file(const char *path, const void *buf, long n)
{
	FILE *f = fopen(path, "wb");
	if(!f)
		return -1;
	if(fwrite(buf, 1, (size_t)n, f) != (size_t)n)
	{
		fclose(f);
		return -1;
	}
	return fclose(f) ? -1 : 0;
}

/* The oracle's capture window, which a script's shot writes for pixdiff. */
static const struct SdlView sdl_capture = {0, AMIGA_FRAME_CAPTURE_ROW, AMIGA_FRAME_W, AMIGA_FRAME_H};

/* v: the window's view for F12, sdl_capture for a script's shot. */
static int sdl_shot(const struct SdlOptions *o, const char *name, const struct SdlView *v)
{
	char path[1280];
	SDL_CreateDirectory(o->shot_dir);
	snprintf(path, sizeof path, "%s/%s.png", o->shot_dir, name);
	if(sdl_png_write(path, &machine, v))
	{
		fprintf(stderr, "cannot write %s\n", path);
		return -1;
	}
	printf("shot %s (frame %u, %d colours)\n", path, machine.frame_count, script_colours(&machine));
	return 0;
}

/* Every writable role whose image the game changed goes to <pref><id>.adf. */
static void sdl_flush_disks(void)
{
	uint32_t r;
	for(r = 0; r < game_def.disk_count; r++)
	{
		struct AmigaDisk *d = &machine.disks[r];
		char path[1280];
		if(!d->rw || !d->dirty)
			continue;
		snprintf(path, sizeof path, "%s%s.adf", sdl_pref, game_def.disks[r].id);
		if(sdl_write_file(path, d->rw, AMIGA_ADF_SIZE))
			fprintf(stderr, "cannot write %s\n", path);
		d->dirty = 0;
	}
}

static void sdl_report_headless(int error, const char *text)
{
	if(error)
	{
		fprintf(stderr, "%s\n", text);
		sdl_report_errors++;
	}
	else
		sdl_log(text);
}

static int sdl_state_save(const char *path, void (*report)(int error, const char *text))
{
	char line[1400];
	FILE *f;
	int ok;
	uint32_t n = state_save(&machine, sdl_state_buf);
	f = fopen(path, "wb");
	ok = f && fwrite(sdl_state_buf, 1, n, f) == n;
	if(f && fclose(f))
		ok = 0;
	if(!ok)
	{
		snprintf(line, sizeof line, "cannot write the state %.1000s", path);
		report(1, line);
		return -1;
	}
	snprintf(line, sizeof line, "state saved to %.1000s (frame %u)", path, machine.frame_count);
	report(0, line);
	return 0;
}

/* `machine` is untouched unless the whole state reads back and matches this build and disks.
 * 0, or why it was not loaded. */
static const char *sdl_state_load(const char *path, void (*report)(int error, const char *text))
{
	char line[1400];
	const char *why;
	size_t n;
	FILE *f = fopen(path, "rb");
	if(!f)
	{
		snprintf(line, sizeof line, "no state at %.1000s", path);
		report(1, line);
		return "no state file";
	}
	n = fread(sdl_state_buf, 1, sizeof sdl_state_buf, f);
	fclose(f);
	why = state_load(&machine, sdl_state_buf, (uint32_t)n);
	if(why)
	{
		snprintf(line, sizeof line, "%.1000s: %s", path, why);
		report(1, line);
		return why;
	}
	snprintf(line, sizeof line, "state loaded from %.1000s (frame %u)", path, machine.frame_count);
	report(0, line);
	return 0;
}

static void sdl_stats(void)
{
	static const char *libs[HLE_CALL_LIBS] = {"exec", "graphics", "trackdisk", "input", "intuition", "dos", "layers", "audio"};
	int lib, i;
	printf("frames %u, instructions %llu, cycles %llu, reads %d, loads %d\n", machine.frame_count,
		(unsigned long long)machine.insns, (unsigned long long)machine.cycles, machine.load_reads, machine.load_count);
	for(lib = 0; lib < HLE_CALL_LIBS; lib++)
		for(i = 0; i < 256; i++)
			if(machine.hle.calls[lib][i])
				printf("  %s -%d: %u calls\n", libs[lib], i * 6, machine.hle.calls[lib][i]);
}

static void sdl_report_fatal(void)
{
	const struct Cpu *c = &machine.cpu;
	if(sdl_fatal_shown)
		return;
	sdl_fatal_shown = 1;
	fprintf(stderr, "fatal: %s\n", machine.fatal_msg);
	fprintf(stderr, "  PC %06X SR %04X  frame %u\n", c->pc, c->sr | (c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->c,
		machine.frame_count);
	fprintf(stderr, "  D %08X %08X %08X %08X %08X %08X %08X %08X\n", c->d[0], c->d[1], c->d[2], c->d[3], c->d[4], c->d[5], c->d[6], c->d[7]);
	fprintf(stderr, "  A %08X %08X %08X %08X %08X %08X %08X %08X\n", c->a[0], c->a[1], c->a[2], c->a[3], c->a[4], c->a[5], c->a[6], c->a[7]);
}

/* Returns -1 to keep running, else the process exit code. */
static int sdl_script_step(const struct SdlOptions *o)
{
	for(;;)
	{
		enum ScriptResult r = script_tick(&sdl_script, &machine);
		switch(r)
		{
		case ScriptRunFrame:
			return -1;
		case ScriptShot:
			if(sdl_shot(o, sdl_script.name, &sdl_capture))
				return 1;
			continue;
		case ScriptStateSave:
			if(sdl_state_save(o->state, sdl_report_headless))
				return 2;
			continue;
		case ScriptStateLoad:
			if(sdl_state_load(o->state, sdl_report_headless))
				return 2;
			continue;
		case ScriptQuit:
			return 0;
		case ScriptTimeout:
			fprintf(stderr, "script timeout: %s\n", sdl_script.error);
			return 3;
		case ScriptError:
			fprintf(stderr, "script error: %s\n", sdl_script.error);
			return 2;
		}
	}
}

/* SDL scancode to Amiga raw keycode, HRM appendix H; game_def.keys first. */
static int sdl_amiga_key(SDL_Scancode sc)
{
	static const uint8_t letters[26] = {
		0x20, 0x35, 0x33, 0x22, 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28, 0x37,
		0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14, 0x16, 0x34, 0x11, 0x32, 0x15, 0x31};
	uint32_t i;
	for(i = 0; i < game_def.key_count; i++)
		if(game_def.keys[i].host == sc)
			return game_def.keys[i].amiga;
	if(sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
		return letters[sc - SDL_SCANCODE_A];
	if(sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
		return 0x01 + (sc - SDL_SCANCODE_1);
	if(sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10)
		return 0x50 + (sc - SDL_SCANCODE_F1);
	switch(sc)
	{
	case SDL_SCANCODE_SPACE: return 0x40;
	case SDL_SCANCODE_BACKSPACE: return 0x41;
	case SDL_SCANCODE_TAB: return 0x42;
	case SDL_SCANCODE_RETURN: return 0x44;
	case SDL_SCANCODE_ESCAPE: return 0x45;
	case SDL_SCANCODE_DELETE: return 0x46;
	case SDL_SCANCODE_UP: return 0x4C;
	case SDL_SCANCODE_DOWN: return 0x4D;
	case SDL_SCANCODE_RIGHT: return 0x4E;
	case SDL_SCANCODE_LEFT: return 0x4F;
	case SDL_SCANCODE_LSHIFT: return 0x60;
	case SDL_SCANCODE_RSHIFT: return 0x61;
	case SDL_SCANCODE_LCTRL: return 0x63;
	case SDL_SCANCODE_LALT: return 0x64;
	case SDL_SCANCODE_RALT: return 0x65;
	default: return -1;
	}
}

/* F5 or F9, unless the game takes them (game_def.no_state_keys). */
static int sdl_state_key(SDL_Scancode sc)
{
	return !game_def.no_state_keys && (sc == SDL_SCANCODE_F5 || sc == SDL_SCANCODE_F9);
}

/* The keys the window loop takes for itself while the game runs; they cannot be bound. */
static int sdl_frontend_key(SDL_Scancode sc)
{
	return sc == (SDL_Scancode)sdl_menu_key() || sdl_state_key(sc) || sc == SDL_SCANCODE_F12;
}

static int sdl_headless(const struct SdlOptions *o)
{
	int code = 0;
	for(;;)
	{
		if(o->script)
		{
			code = sdl_script_step(o);
			if(code >= 0)
				break;
		}
		if(o->frames && (long)machine.frame_count >= o->frames)
			break;
		machine.pointer_hidden = game_system_cursor(&machine);
		if(amiga_run_frame(&machine))
		{
			code = 1;
			break;
		}
		sdl_flush_disks();
		{ static uint32_t seen; if(machine.disk_accesses != seen) { fprintf(stderr, "ACCESS %u %u\n", machine.frame_count, machine.disk_accesses - seen); seen = machine.disk_accesses; } }
	}
	sdl_flush_disks();
	return code;
}

static void sdl_pace(uint64_t *next, uint64_t frame_ns)
{
	uint64_t now = SDL_GetTicksNS();
	*next += frame_ns;
	if(*next > now)
		SDL_DelayNS(*next - now);
	else if(now - *next > 10 * frame_ns)
		*next = now;
}

struct SdlPointer
{
	float wx, wy; /* the OS cursor, window coordinates */
	int inside;
};

static int sdl_clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* One axis of the counts that move the game's pointer onto the OS cursor, mapped into the drawn
 * frame: the whole error, so the loop closes for any pointer that moves at most two frame pixels
 * per count. A cursor on the view's first or last column (or row) pushes at least one count a
 * frame outward, so the pointer rests at the game's own limit even when that limit lies past the
 * edge, as a mouse pushed against it would. The bound keeps JOY0DAT's 8-bit counter from
 * wrapping (HRM), and hle_input_vblank reads the counters' difference as a signed byte. */
static int sdl_pointer_counts(int target, int at, int first, int last)
{
	int d = sdl_clamp(target - at, -127, 127);
	if(target == first && d > -1)
		return -1;
	if(target == last && d < 1)
		return 1;
	return d;
}

/* 0 when the game's pointer is not on screen, and nothing steers it. *tx, *ty: the frame point the
 * OS cursor is over, held inside the view. */
static int sdl_pointer_step(const struct SdlPointer *p, SDL_Window *window, int *dx, int *dy, int *tx, int *ty)
{
	struct SdlVideoRect r;
	int px, py, ww = 0, wh = 0;
	*dx = *dy = 0;
	if(!p->inside || !denise_pointer(&machine, &px, &py) || !SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0
		|| sdl_video_rect(window, &r))
		return 0;
	*tx = sdl_clamp(r.view.x + (int)SDL_floorf((p->wx * r.window_w / ww - r.x) * r.view.w / r.w), r.view.x, r.view.x + r.view.w - 1);
	*ty = sdl_clamp(r.view.y + (int)SDL_floorf((p->wy * r.window_h / wh - r.y) * r.view.h / r.h), r.view.y, r.view.y + r.view.h - 1);
	*dx = sdl_pointer_counts(*tx, px, r.view.x, r.view.x + r.view.w - 1);
	*dy = sdl_pointer_counts(*ty, py, r.view.y, r.view.y + r.view.h - 1);
	return 1;
}

/* Accepted-image and state lines go to stdout only: the Disks page already lists each disk. */
static void sdl_report_window(int error, const char *text)
{
	sdl_report_headless(error, text);
	if(!error)
		return;
	sdl_menu_message(text);
	if(sdl_booted)
		sdl_menu_show(1);
}

static void sdl_banner_window(const char *text)
{
	sdl_log(text);
	sdl_menu_banner(text);
}

/* A disk given while the window is up (dropped, Open Disk…, Load save disk…): its lines also
 * show as notifications. */
static void sdl_report_given(int error, const char *text)
{
	sdl_report_window(error, text);
	sdl_menu_notify(text, error);
}

/* F5 and the menu's save buttons; the outcome also shows as a notification. */
static void sdl_state_save_window(const char *path, void (*report)(int error, const char *text))
{
	int failed = sdl_state_save(path, report);
	sdl_menu_notify(failed ? "State not saved" : "State saved", failed);
}

/* F9 and the menu's load buttons; the outcome also shows as a notification. */
static const char *sdl_state_load_window(const char *path, void (*report)(int error, const char *text))
{
	char text[200];
	const char *why = sdl_state_load(path, report);
	snprintf(text, sizeof text, "State not loaded: %.150s", why ? why : "");
	sdl_menu_notify(why ? text : "State loaded", why != 0);
	return why;
}

/* Boots, or cold-boots a machine that has run, with the boot layout of sdl_disks_boot_drives.
 * 0, or -1 with the fatal error printed. */
static int sdl_boot(void (*banner)(const char *text))
{
	const int *drives = sdl_disks_boot_drives();
	int i;
	sdl_disks_attach(&machine, banner);
	for(i = 0; i < 4; i++)
		amiga_insert(&machine, i, drives[i]);
	if(machine.fatal || (machine.booted ? amiga_reset(&machine) : amiga_boot(&machine)))
	{
		sdl_report_fatal();
		return -1;
	}
	return 0;
}

/* sdl_boot for the window: once booted, a notification names the menu key. */
static int sdl_boot_window(void)
{
	char text[64];
	if(sdl_boot(sdl_banner_window))
		return -1;
	sdl_booted = 1;
	sdl_disks_remember();
	snprintf(text, sizeof text, "Press %s for the menu", SDL_GetScancodeName((SDL_Scancode)sdl_menu_key()));
	sdl_menu_notify(text, 0);
	return 0;
}

/* One action from the menu; returns the number of roles filled, and sets *swapped when the
 * machine's disks were replaced. */
static int sdl_menu_action_run(const struct SdlOptions *o, const SDL_UserEvent *u, int *swapped)
{
	const char *path = u->data1 ? (const char *)u->data1 : o->state;
	char text[160];
	int role, n;
	switch((enum SdlMenuAction)u->code)
	{
	case SdlMenuOpenDisk:
		return sdl_disks_open(&machine, u->data1, swapped, sdl_report_given);
	case SdlMenuImportDisk:
		role = (int)(intptr_t)u->data2;
		if(sdl_disks_import(role, u->data1, sdl_report_given) == 0 && sdl_roles[role].attached)
			for(n = 0; n < AMIGA_DRIVES; n++)
				if(machine.drive_role[n] == role)
					amiga_insert(&machine, n, role);
		return 0;
	case SdlMenuStateSave:
		sdl_state_save_window(path, sdl_report_window);
		return 0;
	case SdlMenuStateLoad:
		if(!sdl_state_load_window(path, sdl_report_window))
			sdl_menu_show(0);
		return 0;
	case SdlMenuReset:
		role = machine.drive_role[0];
		if(role < 0)
		{
			sdl_menu_notify("Not reset: DF0 is empty", 1);
			return 0;
		}
		if(role != game_def.drives[0] && !(game_def.drives_optional && role == game_def.drives_optional[0]))
		{
			snprintf(text, sizeof text, "Not reset: the game does not boot from %.60s in DF0", game_def.disks[role].label);
			sdl_menu_notify(text, 1);
			return 0;
		}
		snprintf(text, sizeof text, "reset at frame %u", machine.frame_count);
		if(!amiga_reset(&machine))
		{
			sdl_log(text);
			sdl_menu_notify("Machine reset", 0);
		}
		sdl_menu_show(0);
		return 0;
	}
	return 0;
}

static int sdl_window(const struct SdlOptions *o, const char *const *paths, int path_count)
{
	/* 142 102 CPU cycles a frame at 7 093 790 Hz (twice the PAL colour clock). */
	const uint64_t frame_ns = (uint64_t)AMIGA_CYCLES_PER_FRAME * 1000000000ull / (2ull * AMIGA_CCK_HZ);
	SDL_Window *window;
	SDL_GLContext gl;
	SDL_AudioStream *audio;
	SDL_AudioSpec spec;
	struct SdlPointer ptr;
	uint64_t next;
	uint32_t audio_read = 0;
	int running = 1, code = 0, lmb = 0, rmb = 0, cursor_shown = -1, captured = 0, grabbed = 0, fullscreen, want, i;
	float fx = 0, fy = 0;
	struct SdlView view;

	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	sdl_pad_init();
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	i = o->scale ? o->scale : 3;
	if(sdl_video_view(&view))
	{
		fprintf(stderr, "video: game_def.view %d,%d %dx%d lies outside the frame buffer\n", game_def.view.x, game_def.view.y, game_def.view.w, game_def.view.h);
		return 1;
	}
	fullscreen = sdl_setting(SdlSettingFullscreen);
	window = SDL_CreateWindow(game_def.title, view.w * i, view.h * i, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
	if(!window)
	{
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}
	SDL_SetWindowMinimumSize(window, view.w, view.h);
	sdl_desktop_window_icon(window);
	gl = SDL_GL_CreateContext(window);
	if(!gl)
	{
		fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
		return 1;
	}
	if(!SDL_GL_MakeCurrent(window, gl))
	{
		fprintf(stderr, "SDL_GL_MakeCurrent: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetSwapInterval(0);
	if(sdl_video_init())
		return 1;
	if(sdl_menu_init(window, &machine))
		return 1;
	ptr.inside = SDL_GetMouseFocus() == window;
	SDL_GetMouseState(&ptr.wx, &ptr.wy);
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	spec.freq = AMIGA_AUDIO_HZ;
	audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, 0, 0);
	if(audio)
		SDL_ResumeAudioStreamDevice(audio);

	for(i = 0; i < path_count; i++)
		sdl_disks_offer(paths[i], SdlOfferGiven, sdl_report_window);
	sdl_disks_offer_remembered(sdl_report_window);
	if(sdl_disks_ready())
	{
		if(sdl_boot_window())
		{
			running = 0;
			code = 1;
		}
	}
	else
		sdl_menu_show(1);
	sdl_menu_refresh(sdl_booted);
	next = SDL_GetTicksNS();

	while(running)
	{
		SDL_Event e;
		int dx, dy, filled = 0, swapped = 0;
		while(SDL_PollEvent(&e))
		{
			int open = sdl_menu_is_open();
			if(e.type == SDL_EVENT_QUIT)
				running = 0;
			else if(e.type == SDL_EVENT_DROP_FILE)
				filled += sdl_disks_open(&machine, e.drop.data, &swapped, sdl_report_given);
			else if(sdl_pad_event(&e))
				;
			else if(e.type == sdl_menu_action_event)
			{
				filled += sdl_menu_action_run(o, &e.user, &swapped);
				SDL_free(e.user.data1);
			}
			else if(e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE || e.type == SDL_EVENT_MOUSE_MOTION)
			{
				if(e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
					ptr.inside = 0;
				else
				{
					int ww = 0, wh = 0;
					struct SdlVideoRect r;
					ptr.wx = e.motion.x;
					ptr.wy = e.motion.y;
					ptr.inside = 1;
					if(SDL_GetWindowSize(window, &ww, &wh) && ww > 0 && wh > 0 && !sdl_video_rect(window, &r))
					{
						fx += e.motion.xrel * r.window_w / (float)ww * r.view.w / (float)r.w;
						fy += e.motion.yrel * r.window_h / (float)wh * r.view.h / (float)r.h;
					}
				}
				if(open || !captured)
					sdl_menu_event(&e);
			}
			else if((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && (e.key.mod & SDL_KMOD_ALT)
				&& (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER))
			{
				if(e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
				{
					sdl_settings_choose(&machine, 0, SdlSettingFullscreen, !sdl_setting(SdlSettingFullscreen));
					sdl_menu_refresh(sdl_booted);
				}
			}
			else if(open)
			{
				if(e.type == SDL_EVENT_KEY_DOWN && sdl_menu_binding())
				{
					if(!e.key.repeat)
						sdl_menu_bind(sdl_frontend_key(e.key.scancode) || e.key.scancode == SDL_SCANCODE_ESCAPE ? -1 : (int)e.key.scancode);
				}
				else if(e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && sdl_menu_binding())
				{
					if(sdl_bindings_pad_button(e.gbutton.button))
						sdl_menu_bind(SDL_BIND_PAD(e.gbutton.button));
				}
				else if(e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && sdl_booted
					&& (e.key.scancode == (SDL_Scancode)sdl_menu_key() || e.key.scancode == SDL_SCANCODE_ESCAPE))
					sdl_menu_show(0);
				else
					sdl_menu_event(&e);
			}
			else if(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
			{
				int down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, card = !captured && sdl_menu_welcome_hover();
				if(card || (!down && !captured))
					sdl_menu_event(&e);
				if(card && down)
					continue;
				if(e.button.button == SDL_BUTTON_LEFT)
					lmb = down;
				else if(e.button.button == SDL_BUTTON_RIGHT)
					rmb = down;
			}
			else if((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !e.key.repeat)
			{
				int down = e.type == SDL_EVENT_KEY_DOWN, k;
				if(e.key.scancode == (SDL_Scancode)sdl_menu_key())
				{
					if(down)
						sdl_menu_show(1);
				}
				else if(e.key.scancode == SDL_SCANCODE_F12)
				{
					if(down)
					{
						char name[32];
						snprintf(name, sizeof name, "frame-%06u", machine.frame_count);
						sdl_shot(o, name, &view);
					}
				}
				else if(sdl_state_key(e.key.scancode))
				{
					if(down && e.key.scancode == SDL_SCANCODE_F5)
						sdl_state_save_window(o->state, sdl_report_headless);
					else if(down)
						sdl_state_load_window(o->state, sdl_report_headless);
				}
				else if(!sdl_bindings_key(e.key.scancode, down) && (k = sdl_amiga_key(e.key.scancode)) >= 0)
					amiga_key(&machine, (uint8_t)k, !down);
			}
		}

		if(swapped)
		{
			char text[64];
			snprintf(text, sizeof text, "disks replaced at frame %u", machine.frame_count);
			sdl_log(text);
			sdl_booted = 0;
		}
		if(filled || swapped)
		{
			if(!sdl_booted && sdl_disks_ready())
			{
				if(sdl_boot_window())
				{
					code = 1;
					break;
				}
				if(swapped)
					sdl_menu_notify("Machine reset with the new disks", 0);
				sdl_menu_show(0);
			}
			else if(sdl_booted)
			{
				sdl_disks_attach(&machine, sdl_banner_window);
				sdl_disks_remember();
			}
			else if(swapped)
				sdl_menu_show(1);
			sdl_menu_refresh(sdl_booted);
		}
		if(sdl_menu_is_open())
		{
			lmb = rmb = 0;
			sdl_bindings_release();
		}
		want = sdl_menu_is_open() || (!captured && (!game_def.system_cursor || game_system_cursor(&machine) || sdl_menu_welcome_hover()));
		if(want != cursor_shown)
		{
			cursor_shown = want;
			if(cursor_shown)
				SDL_ShowCursor();
			else
				SDL_HideCursor();
		}
		if(sdl_setting(SdlSettingFullscreen) != fullscreen)
		{
			fullscreen = !fullscreen;
			SDL_SetWindowFullscreen(window, fullscreen != 0);
		}

		want = sdl_booted && !sdl_menu_is_open() && !o->script && sdl_setting(SdlSettingMouseCapture);
		if(want != captured)
		{
			SDL_SetWindowRelativeMouseMode(window, want != 0);
			captured = want != 0;
			fx = fy = 0;
		}
		want = sdl_booted && !sdl_menu_is_open() && !o->script && game_def.grab_keyboard;
		if(want != grabbed)
		{
			SDL_SetWindowKeyboardGrab(window, want != 0);
			grabbed = want != 0;
		}
		if(sdl_booted && !sdl_menu_is_open())
		{
			int was_steered = machine.steered, tx, ty;
			machine.steered = 0;
			machine.cursor_dx = machine.cursor_dy = 0;
			if(o->script)
				dx = dy = 0;
			else if(!captured && sdl_pointer_step(&ptr, window, &dx, &dy, &tx, &ty))
			{
				fx = fy = 0;
				if(was_steered)
				{
					machine.cursor_dx = tx - machine.cursor_x;
					machine.cursor_dy = ty - machine.cursor_y;
				}
				machine.cursor_x = tx;
				machine.cursor_y = ty;
				machine.steered = 1;
			}
			else
			{
				dx = sdl_clamp((int)fx, -127, 127);
				dy = sdl_clamp((int)fy, -127, 127);
				fx -= (float)dx;
				fy -= (float)dy;
			}
			amiga_mouse(&machine, dx, dy, lmb, rmb);
			amiga_joystick(&machine, 1, (uint8_t)(sdl_bindings_joystick() | sdl_pad_joystick(1)));
			if(game_def.joystick >= 2)
				amiga_joystick(&machine, 0, sdl_pad_joystick(0));
			machine.pointer_hidden = game_system_cursor(&machine) && !captured;
			machine.os_cursor = !captured && (!game_def.system_cursor || game_system_cursor(&machine));

			if(o->script)
			{
				code = sdl_script_step(o);
				if(code >= 0)
					break;
			}
			if(amiga_run_frame(&machine))
			{
				code = 1;
				break;
			}
			sdl_flush_disks();

			if(audio)
			{
				if(machine.paula.audio_write - audio_read > AMIGA_AUDIO_FRAMES)
					audio_read = machine.paula.audio_write;
				while(audio_read != machine.paula.audio_write)
				{
					uint32_t at = audio_read % AMIGA_AUDIO_FRAMES;
					uint32_t n = machine.paula.audio_write - audio_read;
					if(n > AMIGA_AUDIO_FRAMES - at)
						n = AMIGA_AUDIO_FRAMES - at;
					SDL_PutAudioStreamData(audio, sdl_stereo_mix(machine.paula.audio[at], n, sdl_setting(SdlSettingStereo) == SdlStereoModern), (int)(n * 4));
					audio_read += n;
				}
			}
		}
		else
			fx = fy = 0;

		sdl_video_draw(window, &machine);
		sdl_menu_render(window);
		SDL_GL_SwapWindow(window);

		sdl_pace(&next, frame_ns);
	}
	sdl_flush_disks();
	if(audio)
		SDL_DestroyAudioStream(audio);
	sdl_menu_quit();
	sdl_video_quit();
	SDL_GL_DestroyContext(gl);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return code;
}

int main(int argc, char **argv)
{
	struct SdlOptions o;
	const char *paths[SDL_MAX_PATHS];
	const char *pref_arg = 0;
	char state_path[1024], shot_dir[1024];
	int i, n, code, path_count = 0;
	char *pref = 0;

#ifdef _WIN32
	sdl_attach_console();
#endif
	memset(&o, 0, sizeof o);
	/* The app id names the window to the desktop, which matches it with games/<id>/packaging/<id>.desktop. */
	SDL_SetAppMetadata(game_def.title, 0, game_def.id);
	for(i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : 0;
		if(!strcmp(a, "-h") || !strcmp(a, "--help"))
			return sdl_help();
		if(strncmp(a, "--", 2))
		{
			if(path_count == SDL_MAX_PATHS)
				return sdl_usage();
			paths[path_count++] = a;
		}
		else if(!strcmp(a, "--headless")) o.headless = 1;
		else if(!strcmp(a, "--no-sprites")) o.no_sprites = 1;
		else if(!strcmp(a, "--no-crt")) o.no_crt = 1;
		else if(!strcmp(a, "--scale") && v)
		{
			char *end;
			o.scale = (int)strtol(v, &end, 10);
			if(end == v || *end || o.scale < 1 || o.scale > SDL_MAX_SCALE)
				return sdl_usage();
			i++;
		}
		else if(!strcmp(a, "--frames") && v) { o.frames = strtol(v, 0, 10); i++; }
		else if(!strcmp(a, "--script") && v) { o.script = v; i++; }
		else if(!strcmp(a, "--shot-dir") && v) { o.shot_dir = v; i++; }
		else if(!strcmp(a, "--dump") && v) { o.dump = v; i++; }
		else if(!strcmp(a, "--state") && v) { o.state = v; i++; }
		else if(!strcmp(a, "--pref") && v) { pref_arg = v; i++; }
		else return sdl_usage();
	}

	if(pref_arg)
		snprintf(sdl_pref, sizeof sdl_pref, "%s", pref_arg);
	else
	{
		pref = SDL_GetPrefPath("AmigaRecomp", game_def.id);
		if(!pref)
		{
			fprintf(stderr, "no preference directory: %s\n", SDL_GetError());
			return 2;
		}
		snprintf(sdl_pref, sizeof sdl_pref, "%s", pref);
		SDL_free(pref);
	}
	n = (int)strlen(sdl_pref);
	if(n && sdl_pref[n - 1] != '/' && sdl_pref[n - 1] != '\\' && n + 1 < (int)sizeof sdl_pref)
	{
		sdl_pref[n] = '/';
		sdl_pref[n + 1] = 0;
	}
	SDL_CreateDirectory(sdl_pref);
	if(!o.shot_dir)
	{
		snprintf(shot_dir, sizeof shot_dir, "%sshots", sdl_pref);
		o.shot_dir = shot_dir;
	}
	if(!o.state)
	{
		snprintf(state_path, sizeof state_path, "%sstate.sav", sdl_pref);
		o.state = state_path;
	}

	if(o.script)
	{
		long len = sdl_read_file(o.script, sdl_script_text, (long)sizeof sdl_script_text - 1);
		if(len < 0)
		{
			fprintf(stderr, "cannot read %s\n", o.script);
			return 2;
		}
		sdl_script_text[len] = 0;
		script_init(&sdl_script, sdl_script_text);
	}

	amiga_init(&machine);
	machine.log = sdl_log;
	if(o.no_sprites)
		machine.sprites_enabled = 0;
	if(sdl_disks_init(sdl_pref))
		return 2;
	if(o.headless)
	{
		char missing[256];
		for(i = 0; i < path_count; i++)
			sdl_disks_offer(paths[i], SdlOfferGiven, sdl_report_headless);
		if(sdl_report_errors)
			return 2;
		if(!sdl_disks_ready())
		{
			sdl_disks_missing(missing, sizeof missing);
			fprintf(stderr, "%s is missing: %s\n", game_def.title, missing);
			return 2;
		}
		if(sdl_boot(sdl_log))
			return 1;
		code = sdl_headless(&o);
	}
	else
	{
		char settings_path[1100];
		snprintf(settings_path, sizeof settings_path, "%ssettings.ini", sdl_pref);
		if(sdl_settings_load(settings_path))
			return 2;
		sdl_settings_apply(&machine, o.script == 0);
		sdl_bindings_apply();
		if(o.no_crt)
			sdl_setting_override(SdlSettingCrt, 0);
		if(o.scale)
			sdl_scale_force(o.scale);
		code = sdl_window(&o, paths, path_count);
	}
	if(machine.fatal)
		sdl_report_fatal();
	sdl_stats();
	if(o.dump && sdl_write_file(o.dump, machine.chip, AMIGA_CHIP_SIZE))
		fprintf(stderr, "cannot write %s\n", o.dump);
	return code;
}
