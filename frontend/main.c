#include "amiga.h"
#include "disks.h"
#include "game.h"
#include "menu.h"
#include "png.h"
#include "script.h"
#include "settings.h"
#include "video.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Amiga machine;
static struct Amiga sdl_state;
static struct Script sdl_script;
static char sdl_script_text[1 << 16];
static char sdl_pref[1024];
static int sdl_booted;
static int sdl_report_errors;
static int sdl_fatal_shown;

#define SDL_MAX_PATHS 16

struct SdlOptions
{
	int headless;
	long frames;
	const char *script;
	const char *shot_dir;
	const char *state;
	const char *dump;
	int no_sprites;
};

static void sdl_log(const char *text)
{
	printf("%s\n", text);
	fflush(stdout);
}

static int sdl_usage(void)
{
	fprintf(stderr,
		"usage: %s [--headless] [--frames N] [--script FILE] [--shot-dir DIR] [--state PATH]\n"
		"          [--pref DIR] [--dump FILE] [--no-sprites] <image>...\n"
		"images: .adf .adz .gz .zip\n",
		game_def.id);
	return 2;
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

static int sdl_shot(const struct SdlOptions *o, const char *name)
{
	char path[1280];
	SDL_CreateDirectory(o->shot_dir);
	snprintf(path, sizeof path, "%s/%s.png", o->shot_dir, name);
	if(sdl_png_write(path, machine.frame))
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

static int sdl_state_save(const char *path)
{
	struct StateHeader h;
	FILE *f;
	int ok;
	state_header(&machine, &h);
	f = fopen(path, "wb");
	ok = f && fwrite(&h, sizeof h, 1, f) == 1 && fwrite(&machine, sizeof machine, 1, f) == 1;
	if(f && fclose(f))
		ok = 0;
	if(!ok)
	{
		fprintf(stderr, "cannot write the state %s\n", path);
		return -1;
	}
	printf("state saved to %s (frame %u)\n", path, machine.frame_count);
	fflush(stdout);
	return 0;
}

/* `machine` is untouched unless the whole state reads back and matches this build and disks. */
static int sdl_state_load(const struct SdlOptions *o)
{
	struct StateHeader h;
	const char *why;
	FILE *f = fopen(o->state, "rb");
	if(!f)
	{
		fprintf(stderr, "no state at %s\n", o->state);
		return -1;
	}
	if(fread(&h, sizeof h, 1, f) != 1)
		why = "not an AmigaRecomp state";
	else
		why = state_check(&machine, &h);
	if(!why && (fread(&sdl_state, sizeof sdl_state, 1, f) != 1 || fgetc(f) != EOF))
		why = "truncated or oversized";
	fclose(f);
	if(why)
	{
		fprintf(stderr, "%s: %s\n", o->state, why);
		return -1;
	}
	state_restore(&machine, &sdl_state);
	printf("state loaded from %s (frame %u)\n", o->state, machine.frame_count);
	fflush(stdout);
	return 0;
}

static void sdl_stats(void)
{
	static const char *libs[4] = {"exec", "graphics", "trackdisk", "input"};
	int lib, i;
	printf("frames %u, instructions %llu, cycles %llu, reads %d, loads %d\n", machine.frame_count,
		(unsigned long long)machine.insns, (unsigned long long)machine.cycles, machine.load_reads, machine.load_count);
	for(lib = 0; lib < 4; lib++)
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
			if(sdl_shot(o, sdl_script.name))
				return 1;
			continue;
		case ScriptStateSave:
			if(sdl_state_save(o->state))
				return 2;
			continue;
		case ScriptStateLoad:
			if(sdl_state_load(o))
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

/* SDL scancode to Amiga raw keycode, HRM appendix H. */
static int sdl_amiga_key(SDL_Scancode sc)
{
	static const uint8_t letters[26] = {
		0x20, 0x35, 0x33, 0x22, 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28, 0x37,
		0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14, 0x16, 0x34, 0x11, 0x32, 0x15, 0x31};
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
		if(amiga_run_frame(&machine))
		{
			code = 1;
			break;
		}
		sdl_flush_disks();
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

/* The counts that move the game's pointer onto the OS cursor: the whole error, so the loop
 * closes for any pointer that moves at most two frame pixels per count. The bound keeps
 * JOY0DAT's 8-bit counter from wrapping (HRM), and hle_input_vblank reads the counters'
 * difference as a signed byte. */
static void sdl_pointer_step(const struct SdlPointer *p, SDL_Window *window, int *dx, int *dy)
{
	int px, py, ww = 0, wh = 0, tx, ty;
	*dx = *dy = 0;
	if(!p->inside || !denise_pointer(&machine, &px, &py) || !SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0)
		return;
	tx = sdl_clamp((int)(p->wx * AMIGA_FRAME_W / ww), 0, AMIGA_FRAME_W - 1);
	ty = sdl_clamp((int)(p->wy * AMIGA_FRAME_H / wh), 0, AMIGA_FRAME_H - 1);
	*dx = sdl_clamp(tx - px, -127, 127);
	*dy = sdl_clamp(ty - py, -127, 127);
}

/* F11: the next attached role in the game's DF0 order. */
static void sdl_cycle_df0(void)
{
	uint32_t i, step, at = game_def.df0_cycle_count - 1;
	for(i = 0; i < game_def.df0_cycle_count; i++)
		if(game_def.df0_cycle[i] == machine.drive_role[0])
			at = i;
	for(step = 1; step <= game_def.df0_cycle_count; step++)
	{
		int role = game_def.df0_cycle[(at + step) % game_def.df0_cycle_count];
		if(machine.disks[role].image && role != machine.drive_role[0])
		{
			amiga_insert(&machine, 0, role);
			printf("DF0: %s\n", game_def.disks[role].label);
			fflush(stdout);
			return;
		}
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

static void sdl_report_window(int error, const char *text)
{
	sdl_report_headless(error, text);
	sdl_menu_message(text);
	if(error && sdl_booted)
		sdl_menu_show(1);
}

static void sdl_banner_window(const char *text)
{
	sdl_log(text);
	sdl_menu_banner(text);
}

/* 0, or -1 with the fatal error printed. */
static int sdl_boot(void (*banner)(const char *text))
{
	int i;
	sdl_disks_attach(&machine, banner);
	for(i = 0; i < 4; i++)
		amiga_insert(&machine, i, game_def.drives[i]);
	if(machine.fatal || amiga_boot(&machine))
	{
		sdl_report_fatal();
		return -1;
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
	int running = 1, code = 0, lmb = 0, rmb = 0, cursor_shown = -1, i;

	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	window = SDL_CreateWindow(game_def.title, AMIGA_FRAME_W * 3, AMIGA_FRAME_H * 3, SDL_WINDOW_OPENGL);
	if(!window)
	{
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}
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
	if(sdl_menu_init(window))
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
		if(sdl_boot(sdl_banner_window))
		{
			running = 0;
			code = 1;
		}
		else
		{
			sdl_booted = 1;
			sdl_disks_remember();
		}
	}
	else
		sdl_menu_show(1);
	sdl_menu_refresh(sdl_booted);
	next = SDL_GetTicksNS();

	while(running)
	{
		SDL_Event e;
		int dx, dy, filled = 0;
		while(SDL_PollEvent(&e))
		{
			int open = sdl_menu_is_open();
			if(e.type == SDL_EVENT_QUIT)
				running = 0;
			else if(e.type == SDL_EVENT_DROP_FILE)
				filled += sdl_disks_offer(e.drop.data, SdlOfferGiven, sdl_report_window);
			else if(e.type == sdl_menu_path_event)
			{
				filled += sdl_disks_offer(e.user.data1, SdlOfferGiven, sdl_report_window);
				SDL_free(e.user.data1);
			}
			else if(e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE || e.type == SDL_EVENT_MOUSE_MOTION)
			{
				if(e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
					ptr.inside = 0;
				else
				{
					ptr.wx = e.motion.x;
					ptr.wy = e.motion.y;
					ptr.inside = 1;
				}
				if(open)
					sdl_menu_event(&e);
			}
			else if(open)
			{
				if(e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && sdl_booted
					&& (e.key.scancode == SDL_SCANCODE_F1 || e.key.scancode == SDL_SCANCODE_ESCAPE))
					sdl_menu_show(0);
				else
					sdl_menu_event(&e);
			}
			else if(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
			{
				int down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				if(e.button.button == SDL_BUTTON_LEFT)
					lmb = down;
				else if(e.button.button == SDL_BUTTON_RIGHT)
					rmb = down;
			}
			else if((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !e.key.repeat)
			{
				int down = e.type == SDL_EVENT_KEY_DOWN, k;
				if(e.key.scancode == SDL_SCANCODE_F1)
				{
					if(down)
						sdl_menu_show(1);
				}
				else if(e.key.scancode == SDL_SCANCODE_F11)
				{
					if(down)
						sdl_cycle_df0();
				}
				else if(e.key.scancode == SDL_SCANCODE_F12)
				{
					if(down)
					{
						char name[32];
						snprintf(name, sizeof name, "frame-%06u", machine.frame_count);
						sdl_shot(o, name);
					}
				}
				else if(e.key.scancode == SDL_SCANCODE_F5 || e.key.scancode == SDL_SCANCODE_F9)
				{
					if(down && e.key.scancode == SDL_SCANCODE_F5)
						sdl_state_save(o->state);
					else if(down)
						sdl_state_load(o);
				}
				else if((k = sdl_amiga_key(e.key.scancode)) >= 0)
					amiga_key(&machine, (uint8_t)k, !down);
			}
		}

		if(filled)
		{
			if(!sdl_booted && sdl_disks_ready())
			{
				if(sdl_boot(sdl_banner_window))
				{
					code = 1;
					break;
				}
				sdl_booted = 1;
				sdl_disks_remember();
				sdl_menu_show(0);
			}
			else if(sdl_booted)
			{
				sdl_disks_attach(&machine, sdl_banner_window);
				sdl_disks_remember();
			}
			sdl_menu_refresh(sdl_booted);
		}
		if(sdl_menu_is_open())
			lmb = rmb = 0;
		if(sdl_menu_is_open() != cursor_shown)
		{
			cursor_shown = sdl_menu_is_open();
			if(cursor_shown)
				SDL_ShowCursor();
			else
				SDL_HideCursor();
		}

		if(sdl_booted && !sdl_menu_is_open())
		{
			if(o->script)
				dx = dy = 0;
			else
				sdl_pointer_step(&ptr, window, &dx, &dy);
			amiga_mouse(&machine, dx, dy, lmb, rmb);

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
					SDL_PutAudioStreamData(audio, machine.paula.audio[at], (int)(n * 4));
					audio_read += n;
				}
			}
		}

		sdl_video_draw(window, machine.frame);
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

	memset(&o, 0, sizeof o);
	for(i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : 0;
		if(strncmp(a, "--", 2))
		{
			if(path_count == SDL_MAX_PATHS)
				return sdl_usage();
			paths[path_count++] = a;
		}
		else if(!strcmp(a, "--headless")) o.headless = 1;
		else if(!strcmp(a, "--no-sprites")) o.no_sprites = 1;
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
		code = sdl_window(&o, paths, path_count);
	}
	if(machine.fatal)
		sdl_report_fatal();
	sdl_stats();
	if(o.dump && sdl_write_file(o.dump, machine.chip, AMIGA_CHIP_SIZE))
		fprintf(stderr, "cannot write %s\n", o.dump);
	return code;
}
