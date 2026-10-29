#ifndef AMIGA_H
#define AMIGA_H

#include <stdint.h>

/* WinUAE's capture window: 320 x 256, first row = scanline 42, first column = DIW x 120.
 * docs/emulator.md, "Capture window". */
#define AMIGA_FRAME_W 320
#define AMIGA_FRAME_H 256
#define AMIGA_FRAME_LINE0 42
#define AMIGA_FRAME_DIW_X0 120
/* A frame row runs on past the capture window to the last column Denise draws, DIW x
 * 2 × AMIGA_CCK_PER_LINE (denise_render_to), so a picture the capture clips stays whole. */
#define AMIGA_FRAME_STRIDE (AMIGA_CCK_PER_LINE * 2 + 1 - AMIGA_FRAME_DIW_X0)
/* The first line after the PAL vertical blank: the first that sprite DMA fetches and the first
 * the frame buffer holds. Not measured; docs/runtime.md, "Timing". */
#define AMIGA_DISPLAY_LINE0 25
/* The frame buffer's rows are the lines from AMIGA_DISPLAY_LINE0 to the frame's last, so a
 * picture taller than the capture window stays whole; the capture window is AMIGA_FRAME_H of
 * them from row AMIGA_FRAME_CAPTURE_ROW. */
#define AMIGA_FRAME_ROWS (AMIGA_LINES - AMIGA_DISPLAY_LINE0)
#define AMIGA_FRAME_CAPTURE_ROW (AMIGA_FRAME_LINE0 - AMIGA_DISPLAY_LINE0)
/* The frame buffer holds each lores pixel as AMIGA_FRAME_XS columns, so a hires line keeps both
 * pixels of every pair, and each line as AMIGA_FRAME_YS rows, one per field of an interlaced
 * frame (decision 027; docs/runtime.md, "Timing"). Row and column counts above are in lines and
 * lores pixels. */
#define AMIGA_FRAME_XS 2
#define AMIGA_FRAME_YS 2

/* 1: the machine is an A1200 (68EC020, AGA, 2 MB chip RAM, Kickstart 3.0), chosen by the game's
 * CMakeLists (docs/game.md, item 2); 0: an A500. */
#ifndef AMIGA_A1200
#define AMIGA_A1200 0
#endif

/* 1: a disk DMA transfer takes the time the head takes to pass its words; 0: it is instant.
 * Chosen by the game's CMakeLists (docs/game.md, item 2; core/disk.c). */
#ifndef AMIGA_DISK_TIMED
#define AMIGA_DISK_TIMED 0
#endif

/* Chip RAM at 0x000000 and 512 KB slow RAM at 0xC00000: the A500 memory map, HRM "Memory Map".
 * Chip RAM is 512 KB behind the OCS Agnus, 1 MB behind the ECS Agnus (8372A) when the game's
 * CMakeLists asks for it (docs/game.md, item 2), or the A1200's 2 MB behind Alice. On the A1200
 * the slow-RAM range holds only the HLE's own structures: no MemHeader lists it. */
#ifndef AMIGA_CHIP_SIZE
#define AMIGA_CHIP_SIZE 0x80000u
#endif
#if AMIGA_A1200 ? AMIGA_CHIP_SIZE != 0x200000u : (AMIGA_CHIP_SIZE != 0x80000u && AMIGA_CHIP_SIZE != 0x100000u)
#error "AMIGA_CHIP_SIZE is 512 KB or 1 MB on an A500, 2 MB on an A1200"
#endif
#define AMIGA_SLOW_BASE 0xC00000u
#define AMIGA_SLOW_SIZE 0x80000u

/* VPOSR bits 14-8: VPOSR read 0x8000 at the bootblock call with the OCS Agnus, 0xA000 with
 * the PAL ECS Agnus and 0xA300 with the A1200's PAL Alice (docs/runtime.md, "Kickstart
 * handoff"). */
#define AMIGA_AGNUS_ID (AMIGA_A1200 ? 0x23u : AMIGA_CHIP_SIZE == 0x100000u ? 0x20u : 0u)

/* PAL: 313 lines of 227 colour clocks, 454 time-base cycles a line, 142 102 a frame --
 * docs/runtime.md, "Timing". A time-base cycle is a clock of the A500's 68000, two per colour
 * clock. */
#define AMIGA_LINES 313
#define AMIGA_CCK_PER_LINE 227
#define AMIGA_CYCLES_PER_LINE 454
#define AMIGA_CYCLES_PER_FRAME (AMIGA_CYCLES_PER_LINE * AMIGA_LINES)
/* PAL colour clock, HRM appendix F. */
#define AMIGA_CCK_HZ 3546895u
#define AMIGA_AUDIO_HZ 48000u
#define AMIGA_AUDIO_FRAMES 4096
/* The CPU's clocks per time-base cycle, as a shift: the A1200's 68EC020 runs at 14.18758 MHz in
 * PAL, four clocks per colour clock (Commodore, A1200 specifications), so two of its clocks are
 * one time-base cycle. Musashi's cycle table for the CPU (m68kcpu.c, m68k_set_cpu_type). */
#define AMIGA_CPU_SHIFT (AMIGA_A1200 ? 1 : 0)
#define AMIGA_CPU_TABLE (AMIGA_A1200 ? 2 : 0)

/* The HLE region: a JMP slot of a library jump table points at
 * 0xF00000 + (lib << 12) + (-LVO), for 32 libraries. docs/runtime.md, "HLE". */
#define HLE_BASE 0xF00000u
#define HLE_END 0xF20000u
#define HLE_LIBS 32
#define HLE_ADDR(lib, off) (HLE_BASE + ((uint32_t)(lib) << 12) + (uint32_t)(off))
#define HLE_LIB_EXEC 0
#define HLE_LIB_GFX 1
#define HLE_LIB_TRACKDISK 2
#define HLE_LIB_INPUT 3
#define HLE_LIB_INTUITION 4
#define HLE_LIB_DOS 5
#define HLE_LIB_LAYERS 6
#define HLE_LIB_AUDIO 7
#define HLE_LIB_STUB 8
#define HLE_STUBS 7
#define HLE_CALL_LIBS 8
#define HLE_LIB_INTERNAL 15
/* Kickstart 3.0's resources and devices, built on the A1200 only (core/hle_res.c). */
#define HLE_LIB_POTGO 16
#define HLE_LIB_CIAA 17
#define HLE_LIB_CIAB 18
#define HLE_LIB_TIMER 19
#define HLE_LIB_KEYBOARD 20
#define HLE_LIB_UTILITY 21
#define HLE_LIB_DISK 22
#define HLE_TIMER_REQUESTS 16
#define HLE_INPUT_EVENTS 16

#define AMIGA_FATAL_LEN 512
#define AMIGA_LOAD_LOG 4096
#define AMIGA_KEY_QUEUE 64
#define AMIGA_LOG_ONCE 128

/* The most hacks game_def.hack_count may be. */
#define AMIGA_HACKS 16

enum HackStatus { HackIdle, HackActive, HackMismatch };

struct Hacks
{
	int value[AMIGA_HACKS];
	uint8_t status[AMIGA_HACKS];   /* enum HackStatus */
	uint32_t live;                 /* hooks set in map; 0 skips the per-instruction test */
	uint32_t map[(AMIGA_CHIP_SIZE + AMIGA_SLOW_SIZE) / 2 / 32]; /* one bit per RAM word: chip words, then slow words */
};

/* 80 cylinders x 2 heads x 11 sectors x 512 bytes: a double-density disk, HRM "Disk
 * Resource". */
#define AMIGA_ADF_SIZE 901120u
/* DF0-DF3: CIA-B PRB drive selects SEL0-SEL3, HRM "CIA Resources". */
#define AMIGA_DRIVES 4
/* The capacity of the role table, not a hardware value. */
#define AMIGA_ROLES 8
/* One revolution of a track as MFM words: 200 ms (300 rpm) at 32 us a word (core/disk.c). */
#define AMIGA_TRACK_WORDS 6250

struct Cpu
{
	uint32_t d[8];
	uint32_t a[8];
	uint32_t sp_other;
	uint16_t sr;
	uint8_t x, n, z, v, c;
	uint32_t pc;
	uint8_t stopped;
	/* Interrupt level pending when the last instruction began: a 68000 takes an interrupt at
	 * an instruction boundary only if it was already asserted then (docs/runtime.md, "Timing"). */
	uint8_t ipl_prev;
};

struct Copper
{
	uint32_t lc1, lc2;
	uint32_t pc;
	uint16_t ir1, ir2;
	int hpos;
	int state;
	uint8_t danger;
};

/* Bitplanes and colour registers: 6 and 32 on OCS, 8 and 256 on AGA (AGA.guide, "Bitplanes" and
 * "Color Lookup Table"). */
#define AMIGA_PLANES (AMIGA_A1200 ? 8 : 6)
#define AMIGA_COLORS (AMIGA_A1200 ? 256 : 32)

struct Sprite
{
	uint32_t pt;
	uint16_t pos, ctl;
	uint64_t data, datb; /* the image's 16, 32 or 64 bits (FMODE), right-aligned */
	int vstart, vstop;
	int armed;
	int dma_state;
};

struct Denise
{
	uint16_t bplcon0, bplcon1, bplcon2, bplcon3, bplcon4;
	int16_t bpl1mod, bpl2mod;
	uint32_t bplpt[AMIGA_PLANES];
	uint16_t diwstrt, diwstop, ddfstrt, ddfstop;
	uint16_t diwhigh, fmode;
	uint8_t diwhigh_written; /* DIWHIGH written since the last DIWSTRT or DIWSTOP write */
	uint32_t color[AMIGA_COLORS]; /* 0x00RRGGBB */
	struct Sprite spr[8];
	uint32_t pointer_list;
	int line;
	int fetched;
	int mod_at;      /* the colour clock at which the fetched line's modulos are added; -1 added */
	int words;
	int first_x;
	int load_lead;   /* lores pixels from the plane-load grid to first_x (denise_fetch) */
	uint16_t plane[AMIGA_PLANES][64];
	int drawn_x;
	uint32_t linebuf[512 * AMIGA_FRAME_XS];
	int frame_done;
	uint8_t short_frame;  /* VPOSR's LOF reads 0; toggles each frame while BPLCON0's LACE is set */
	uint8_t laced;        /* LACE was set at this frame's start: each line fills its field's row alone */
	uint8_t capture_row;  /* the row of each line's pair the capture reads: the last frame's field */
};

struct Blitter
{
	uint16_t con0, con1, afwm, alwm;
	uint32_t apt, bpt, cpt, dpt;
	int16_t amod, bmod, cmod, dmod;
	uint16_t adat, bdat, cdat;
	uint16_t aold, bold;
	int zero;
};

struct AudioChannel
{
	uint32_t lc;
	uint16_t len, per, vol, dat;
	uint32_t ptr;
	uint32_t remain;
	int active;
	int byte;
	int8_t sample;
	uint32_t counter;
	uint16_t word;
};

struct Paula
{
	uint16_t intena, intreq, dmacon, adkcon;
	uint16_t potgo;
	struct AudioChannel aud[4];
	int16_t audio[AMIGA_AUDIO_FRAMES][2];
	uint32_t audio_write;
	uint32_t audio_write_frac;
	uint64_t audio_cck_frac;
	int16_t mouse_x, mouse_y;
	uint8_t lmb, rmb;
	uint8_t joy[2]; /* the joysticks, enum AmigaJoy bits: [1] port 2 (JOY1DAT), [0] port 1 (JOY0DAT) when game_def.joystick is 2 */
};

/* Paula's output stage (core/paula.c): the sums of the colour clocks the next 48 kHz frame
 * covers, and the A500 low-pass stages, Q8, per side. Host output, not part of a save state. */
struct PaulaOutput
{
	int32_t sum[2];
	uint32_t cck;
	int32_t rc[2][2];
};

struct CiaTimer
{
	uint16_t latch, counter;
	uint8_t cr;
	uint8_t running;
};

struct Cia
{
	uint8_t pra, prb, ddra, ddrb;
	struct CiaTimer ta, tb;
	uint32_t tod, tod_alarm, tod_latch;
	uint8_t tod_latched, tod_stopped;
	uint8_t sdr;
	uint8_t icr, icr_mask;
	uint32_t eclock_frac;
};

/* Floppy drives and Paula's disk DMA (core/disk.c). */
struct Floppy
{
	uint32_t dskpt;
	uint16_t dsklen, dsksync;
	uint8_t cyl[AMIGA_DRIVES];     /* 0-79 */
	uint8_t motor[AMIGA_DRIVES];   /* latched when /SELn falls */
	uint8_t changed[AMIGA_DRIVES]; /* 1: /CHNG low */
	uint64_t index_rev;            /* the last revolution number that pulsed the index */
#if AMIGA_DISK_TIMED
	/* The disk DMA in progress: its words land as the head passes them. */
	uint16_t dma_track[AMIGA_TRACK_WORDS]; /* the track it reads, the index at word 0 */
	uint64_t dma_word0;            /* the revolution-word number (cycles / 227) of its first word */
	uint32_t dma_dest, dma_len, dma_done;
	uint8_t dma_on, dma_sync;      /* dma_sync: DSKSYN not raised yet */
#endif
};

/* A disk image a game role holds. image is what the game reads; rw is the same buffer when the
 * game may write it (a writable or an overlay role), else NULL; dump is the player's image (a
 * read-only or an overlay role), else NULL. */
struct AmigaDisk
{
	const uint8_t *image;
	uint8_t *rw;
	const uint8_t *dump;
	uint8_t dirty;
};

struct LoadRecord
{
	uint8_t drive;
	uint32_t offset, length, dest;
};

/* One interrupted piece of HLE work waiting for guest code to return into it. */
struct HleFrame
{
	int kind;
	int level;
	int bit;
	uint32_t node;
	uint32_t saved[6];
};

struct HleInput
{
	int open;
	int count;
	int delivering;
	uint16_t qualifier;
	uint8_t mouse_x, mouse_y, lmb, rmb;
};

/* dos.library's host side (core/hle_dos.c). The capacities are this runtime's, not AmigaDOS's. */
#define HLE_DOS_FILES 16
#define HLE_DOS_ASSIGNS 8
#define HLE_DOS_VOLUMES 8
#define HLE_DOS_OVERLAYS 4

/* An open file: index i is the FileHandle whose fh_Arg1 is i + 1; index 0 is the console. */
struct HleDosFile
{
	uint8_t used;
	int8_t drive;
	uint32_t header, size, pos;
	uint32_t fh;
};

struct HleDosAssign
{
	char name[8];
	int8_t drive;
	uint32_t block;
};

/* A DLT_VOLUME node, kept for as long as the machine runs so that locks on it stay valid. */
struct HleDosVolume
{
	char name[32];
	uint32_t node;
};

/* What LoadSeg of an overlaid file allocated besides the segments, freed by its UnLoadSeg. */
struct HleDosOverlay
{
	uint32_t seglist, table, size, hunks, hunks_size, stream;
};

struct HleDos
{
	uint8_t booted;
	uint8_t cli_ended;          /* ENDCLI has closed the Initial CLI's console window */
	uint8_t workbench;          /* Kickstart's Workbench screen is open */
	int8_t script_drive;
	uint32_t script_header, script_pos;
	struct HleDosFile files[HLE_DOS_FILES];
	struct HleDosAssign assigns[HLE_DOS_ASSIGNS];
	int assign_count;
	char volume[AMIGA_DRIVES][32];
	struct HleDosVolume volumes[HLE_DOS_VOLUMES];
	int volume_count;
	struct HleDosOverlay overlays[HLE_DOS_OVERLAYS];
	uint32_t process, cli;
	uint32_t cmd_seglist, cmd_stack, cmd_stack_size, cmd_args, cmd_args_size;
	uint32_t cmd_in, cmd_out;   /* the command's NIL: redirections (FileHandle BPTRs), 0 none */
	uint32_t handler[AMIGA_DRIVES]; /* DFn's handler MsgPort, which DeviceProc returns and packets go to */
	uint32_t delay_frame;
	uint64_t clock_ticks;       /* the system clock in 1/50 s ticks at frame clock_frame */
	uint32_t clock_frame;
#if AMIGA_A1200
	char request[32];           /* the volume a call that just failed asks the user for; "" none */
#endif
};

/* audio.device's channels (core/hle_audio.c): queue[0] is the write that plays. */
#define HLE_AUDIO_QUEUE 8

struct HleAudioChannel
{
	uint32_t queue[HLE_AUDIO_QUEUE];
	uint8_t count;
	uint8_t allocated, preloaded, finishing;
	int8_t prec;
	uint16_t key;
	uint32_t plays;
};

struct HleAudio
{
	struct HleAudioChannel ch[4];
	uint16_t next_key;
};

/* The gadgets an EasyRequestArgs requester may have; this runtime's capacity. */
#define HLE_REQ_GADGETS 8

/* The A1200's Workbench screen and its CON: window (core/hle_con.c). */
struct HleCon
{
	uint32_t bitmap, copper, view; /* the screen's planes and copper list (chip RAM) and View; 0 not open */
	uint8_t open, reading, ready, cursor_on, shift, esc, space;
	int wx0, wy0, wx1, wy1;     /* the window, on the screen */
	int x0, y0, cols, rows;     /* its text area: first pixel, and size in characters */
	int col, row;               /* the cursor */
	int args, arg[4];           /* the CSI sequence being read */
	char line[128];             /* typed and not yet read; ready once it ends with Return */
	int line_len;
	uint32_t read_buf, read_len; /* the parked Read's buffer and length */
	uint32_t req_save;          /* an EasyRequestArgs requester's backdrop, saved (guest memory); 0 none up */
	uint32_t req_idcmp;         /* its IDCMP_ptr, 0 none */
	uint32_t req_behind;        /* the View shown before it moved the screen to the front */
	int32_t req_answer;         /* its result, once req_done */
	uint8_t req_front, req_done, req_gadgets;
	int req_w, req_h;           /* its window, at the screen's top left; req_w a multiple of 8 */
	int req_gadget_y;           /* its gadgets' top, and each one's left and width */
	int16_t req_gadget_x[HLE_REQ_GADGETS], req_gadget_w[HLE_REQ_GADGETS];
	int8_t req_pressed;         /* the gadget the left button went down on, -1 none */
	uint8_t req_mx, req_my, req_lmb; /* the mouse counters and left button at the last frame */
	uint32_t sprite;            /* chip RAM: a null sprite, then the pointer's; 0 until the first requester */
	int ptr_x, ptr_y;           /* the pointer's hot spot on the screen */
};

struct Hle
{
	uint32_t change_count[AMIGA_DRIVES];
	struct HleFrame frames[8];
	int depth;
	uint32_t wait_tof_frame;
	uint32_t wait_mask, wait_port, wait_io;
	uint32_t softint_pending[16];
	int softint_count;
	uint8_t motor;
	uint32_t calls[HLE_CALL_LIBS][256];
	uint32_t stub_base[HLE_STUBS];
	int stub_count;
	struct HleInput input;
	uint32_t int_tick; /* Intuition's last IDCMP_INTUITICKS message, until it is freed; 0 none */
	struct HleDos dos;
	struct HleAudio audio;
#if AMIGA_A1200
	uint16_t potgo_bits; /* potgo.resource: the POTGO bits allocated */
	uint16_t potgo;      /* potgo.resource: the value WritePotgo last wrote */
	uint32_t timer_io[HLE_TIMER_REQUESTS];  /* timer.device: the queued TR_ADDREQUESTs */
	uint64_t timer_due[HLE_TIMER_REQUESTS]; /* and when each is due, in time-base cycles */
	int timer_count;
	uint8_t key_matrix[16]; /* keyboard.device: one bit per raw key code, set while held */
	uint32_t icr_vec[2][5]; /* ciaa/ciab.resource: each ICR bit's Interrupt, 0 free */
	struct HleCon con;
#endif
};

struct Amiga
{
	uint8_t chip[AMIGA_CHIP_SIZE];
	uint8_t slow[AMIGA_SLOW_SIZE];

	struct Cpu cpu;
	uint64_t cycles;
	uint64_t insns;
	uint64_t done;
	uint32_t frame_count;
	int in_interp;
	uint8_t cpu_clock_odd;   /* AMIGA_CPU_SHIFT 1: a CPU clock not yet counted in `cycles` */
	uint8_t booted;

	struct Copper copper;
	struct Denise denise;
	struct Blitter blit;
	struct Paula paula;
	struct Cia cia_a, cia_b;
	struct Floppy floppy;
	struct Hle hle;

	struct AmigaDisk disks[AMIGA_ROLES];
	int8_t drive_role[AMIGA_DRIVES];

	struct LoadRecord loads[AMIGA_LOAD_LOG];
	int load_count;
	int load_reads;
	uint32_t disk_accesses; /* one per disk transfer, any path; the frontend's drive light */
	struct PaulaOutput audio_out;

	uint8_t keys[AMIGA_KEY_QUEUE];
	int key_head, key_tail;
	int sprites_enabled;
	int pointer_hidden;      /* 1: Denise does not draw game_def.pointer_sprites; set by the frontend each frame */
	int os_cursor;           /* 1: the OS cursor shows over the running game, so the HLE's own pointer is not drawn; set by the frontend each frame */
	int steered;             /* 1: this frame's mouse counts are the frontend's steering error, the distance from the game's pointer to the OS cursor, not the mouse's motion (docs/runtime.md, "Build and run"); set by the frontend each frame */
	int cursor_x, cursor_y;  /* while steered: the frame point the OS cursor is over, DIW x (lores) and scanline, held inside game_def.view */
	int cursor_dx, cursor_dy; /* while steered: cursor_x and cursor_y's change since the frame before, 0 when that frame was not steered; set by the frontend each frame */
	struct Hacks hacks;

	uint32_t frame[AMIGA_FRAME_ROWS * AMIGA_FRAME_YS][AMIGA_FRAME_STRIDE * AMIGA_FRAME_XS]; /* 0x00RRGGBB */

	int fatal;
	char fatal_msg[AMIGA_FATAL_LEN];
	uint32_t logged[AMIGA_LOG_ONCE];
	int logged_count;
	void (*log)(const char *text);
};

/* amiga.c */
void amiga_init(struct Amiga *m);
int amiga_boot(struct Amiga *m);
void amiga_attach(struct Amiga *m, int role, const uint8_t *dump, uint8_t *rw);
void amiga_insert(struct Amiga *m, int drive, int role);
uint32_t amiga_crc32(const uint8_t *p, uint32_t len);
int amiga_run_frame(struct Amiga *m);
void amiga_fatal(struct Amiga *m, const char *fmt, ...);
void amiga_log_once(struct Amiga *m, uint32_t key, const char *fmt, ...);
void amiga_logf(struct Amiga *m, const char *fmt, ...);
void amiga_key(struct Amiga *m, uint8_t code, int up);
void amiga_mouse(struct Amiga *m, int dx, int dy, int lmb, int rmb);
/* AmigaJoyFire2 is the second button of a two-button joystick, on the port's pin 9 (POTGOR). */
enum AmigaJoy { AmigaJoyUp = 1, AmigaJoyDown = 2, AmigaJoyLeft = 4, AmigaJoyRight = 8, AmigaJoyFire = 16, AmigaJoyFire2 = 32 };
void amiga_joystick(struct Amiga *m, int port, uint8_t joy);
uint64_t amiga_now(const struct Amiga *m);

/* mem.c */
uint8_t mem_read8(struct Amiga *m, uint32_t addr);
uint16_t mem_read16(struct Amiga *m, uint32_t addr);
uint32_t mem_read32(struct Amiga *m, uint32_t addr);
void mem_write8(struct Amiga *m, uint32_t addr, uint8_t v);
void mem_write16(struct Amiga *m, uint32_t addr, uint16_t v);
void mem_write32(struct Amiga *m, uint32_t addr, uint32_t v);
uint8_t *mem_ram(struct Amiga *m, uint32_t addr);
uint16_t mem_chip16(struct Amiga *m, uint32_t addr);
void mem_chip_write16(struct Amiga *m, uint32_t addr, uint16_t v);
void mem_dma_load(struct Amiga *m, uint32_t dest, const uint8_t *src, uint32_t len);

/* custom.c */
uint16_t custom_read(struct Amiga *m, uint32_t reg);
void custom_write(struct Amiga *m, uint32_t reg, uint16_t v, int from_copper);
int custom_ipl(const struct Amiga *m);
void custom_set_intreq(struct Amiga *m, uint16_t bits);

/* sched.c */
void sched_catch_up(struct Amiga *m, uint64_t target);
int sched_line(const struct Amiga *m, uint64_t t);
int sched_hpos(const struct Amiga *m, uint64_t t);

/* copper.c */
void copper_restart(struct Amiga *m, int list);
void copper_run(struct Amiga *m, int line, int to_hpos);

/* denise.c */
void denise_begin_frame(struct Amiga *m);
/* The pixel at x, y from the capture window's top-left (AMIGA_FRAME_DIW_X0, AMIGA_FRAME_LINE0) as
 * the oracle's lores capture shows it: the first hires pixel of each pair and the last frame's
 * field. Any pixel of the frame buffer: x from 0 to AMIGA_FRAME_STRIDE - 1, y from
 * -AMIGA_FRAME_CAPTURE_ROW to AMIGA_FRAME_ROWS - AMIGA_FRAME_CAPTURE_ROW - 1. */
uint32_t denise_capture(const struct Amiga *m, int x, int y);
void denise_begin_line(struct Amiga *m, int line);
void denise_render_to(struct Amiga *m, int hpos);
void denise_end_line(struct Amiga *m);
void denise_write(struct Amiga *m, uint32_t reg, uint16_t v);
int denise_pointer(struct Amiga *m, int *x, int *y);
int denise_copper_blocked(const struct Amiga *m, int hpos);
#if AMIGA_A1200
/* hle_con.c: an EasyRequestArgs requester's pointer, its hot spot at DIW x *x on line *y; 0 when
 * no requester is up. */
int hle_con_pointer(const struct Amiga *m, int *x, int *y);
#endif

/* blitter.c */
void blit_start(struct Amiga *m, uint16_t bltsize);

/* paula.c */
void paula_dmacon_changed(struct Amiga *m, uint16_t old);
void paula_audio_run(struct Amiga *m, int cck);
void paula_audio_write(struct Amiga *m, int ch, int reg, uint16_t v);

/* cia.c */
uint8_t cia_read(struct Amiga *m, struct Cia *c, int reg);
void cia_write(struct Amiga *m, struct Cia *c, int reg, uint8_t v);
void cia_run(struct Amiga *m, uint32_t cpu_cycles);
void cia_tod_tick(struct Amiga *m, struct Cia *c);
void cia_frame(struct Amiga *m);
void cia_reassert(struct Amiga *m);
void cia_flag(struct Amiga *m);

/* disk.c */
void disk_write_reg(struct Amiga *m, uint32_t reg, uint16_t v);
void disk_prb_changed(struct Amiga *m, uint8_t old_pins, uint8_t new_pins);
uint8_t disk_status(const struct Amiga *m);
void disk_line(struct Amiga *m);

/* hle_*.c */
int hle_dispatch(struct Amiga *m);
void hle_build(struct Amiga *m);

/* interp.c */
void interp_init(struct Amiga *m);
void interp_run(struct Amiga *m, uint32_t max_insns, uint64_t until);

/* game.c */
int game_hack_set(struct Amiga *m, uint32_t hack, int value);
int game_hook_run(struct Amiga *m, uint32_t pc);
void game_hacks_frame(struct Amiga *m);
int game_system_cursor(const struct Amiga *m);

/* True when an enabled hack hooks pc. Cheap enough to call once per instruction. */
static inline int game_hook_at(const struct Amiga *m, uint32_t pc)
{
	uint32_t w;
	if(!m->hacks.live)
		return 0;
	if(pc < AMIGA_CHIP_SIZE)
		w = pc >> 1;
	else if(pc >= AMIGA_SLOW_BASE && pc < AMIGA_SLOW_BASE + AMIGA_SLOW_SIZE)
		w = (AMIGA_CHIP_SIZE + pc - AMIGA_SLOW_BASE) >> 1;
	else
		return 0;
	return (m->hacks.map[w >> 5] >> (w & 31)) & 1;
}

/* state.c */
#define STATE_VERSION 3
#define STATE_SECTIONS 23

struct StateHeader
{
	char magic[8];                  /* "ARSTATE" */
	uint32_t version;               /* STATE_VERSION */
	uint32_t role_crc[AMIGA_ROLES]; /* CRC-32 of each attached role's dump, else 0 */
};

struct StateSection
{
	char name[12]; /* the struct Amiga member, NUL-padded */
	uint32_t size; /* its sizeof in the writing build */
};

/* The largest state this build writes: the sections are disjoint members of struct Amiga. */
#define STATE_MAX (sizeof(struct StateHeader) + STATE_SECTIONS * sizeof(struct StateSection) + sizeof(struct Amiga))

uint32_t state_save(const struct Amiga *m, uint8_t *out);
const char *state_load(struct Amiga *m, const uint8_t *in, uint32_t len);
int amiga_reset(struct Amiga *m);

#endif
