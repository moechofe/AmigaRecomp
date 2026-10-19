#ifndef HLE_H
#define HLE_H

#include "amiga.h"

/* Guest-memory layout of the emulated Kickstart. ExecBase, its jump table, the boot task's
 * stack, the ROM's IORequest and ThisTask sit where Kickstart 1.3 had them at the bootblock
 * call (docs/runtime.md, "Kickstart handoff"); everything else the HLE
 * owns lives above 0xC02000, clear of them. The A1200's Kickstart 3.0 libraries have longer
 * jump tables (their measured negative sizes), so their tables and bases sit higher, in the
 * slow-RAM range that no A1200 MemHeader lists. */
#if AMIGA_A1200
#define HLE_EXEC_NEGSIZE 0x32Au
#define HLE_EXEC_POSSIZE 0x278u
#else
#define HLE_EXEC_NEGSIZE 0x276u
#define HLE_EXEC_POSSIZE 0x24Cu
#endif
#define HLE_EXEC_BASE (0xC00000u + HLE_EXEC_NEGSIZE)
#define HLE_SLOW_MEMHEADER 0xC004C2u
#define HLE_BOOT_USP 0xC014B2u
#define HLE_BOOT_IOREQ 0xC014E2u
#define HLE_THIS_TASK 0xC01570u
#define HLE_SSP 0xC80000u
#define HLE_CHIP_MEMHEADER 0x000400u

#if AMIGA_A1200
#define HLE_GFX_TABLE 0xC05000u
#define HLE_GFX_NEGSIZE 0x434u
#define HLE_GFX_POSSIZE 0x1F1u
#define HLE_INT_TABLE 0xC05800u
#define HLE_INT_NEGSIZE 0x33Cu
#define HLE_EXP_NEGSIZE 0xA4u
#define HLE_EXP_POSSIZE 0x186u
#else
#define HLE_GFX_TABLE 0xC02000u
#define HLE_GFX_NEGSIZE 0x28Eu
#define HLE_GFX_POSSIZE 0x148u
#define HLE_INT_TABLE 0xC03100u
#define HLE_INT_NEGSIZE 0x1D4u
#define HLE_EXP_NEGSIZE 0x96u
#define HLE_EXP_POSSIZE 0x1C8u
#endif
#define HLE_GFX_BASE (HLE_GFX_TABLE + HLE_GFX_NEGSIZE)
#define HLE_TD_TABLE 0xC02A00u
#define HLE_TD_NEGSIZE 0x24u
#define HLE_TD_BASE (HLE_TD_TABLE + HLE_TD_NEGSIZE)
#define HLE_TD_UNIT0 0xC02A80u
#define HLE_TD_UNIT1 0xC02AC0u
#define HLE_TD_UNIT2 0xC02B80u
#define HLE_TD_UNIT3 0xC02BC0u
#define HLE_SERVER_LISTS 0xC02B00u
#define HLE_STRINGS 0xC02C00u
#define HLE_BOOT_PORT 0xC02D00u
#define HLE_IN_TABLE 0xC02E00u
#define HLE_IN_NEGSIZE 0x24u
#define HLE_IN_BASE (HLE_IN_TABLE + HLE_IN_NEGSIZE)
#define HLE_INPUT_HANDLERS 0xC02E70u
#define HLE_INPUT_EVENTS_AT 0xC02F00u
#define HLE_INT_BASE (HLE_INT_TABLE + HLE_INT_NEGSIZE)
#define HLE_EXP_TABLE 0xC03400u
#define HLE_EXP_BASE (HLE_EXP_TABLE + HLE_EXP_NEGSIZE)
#define HLE_STUB_NEGSIZE 0xFFCu

/* dos.library: its Resident, jump table (on the A500 the V34 table, ending at LVO -222
 * Execute, NDK 3.2 FD/dos_lib.fd; on the A1200 Kickstart 3.0's), DosLibrary, RootNode and
 * DosInfo. */
#define HLE_DOS_RESIDENT 0xC03700u
#if AMIGA_A1200
#define HLE_DOS_TABLE 0xC06000u
#define HLE_DOS_NEGSIZE 0x414u
#else
#define HLE_DOS_TABLE 0xC03740u
#define HLE_DOS_NEGSIZE 0xDEu
#endif
#define HLE_DOS_BASE (HLE_DOS_TABLE + HLE_DOS_NEGSIZE)
#define HLE_DOS_ROOT 0xC03860u
#define HLE_DOS_INFO 0xC03880u

/* audio.device and layers.library: jump tables (devices: Open to AbortIO, exec/devices.i;
 * layers: on the A500 the V34 table, ending at LVO -174 InstallClipRegion, NDK 3.2
 * FD/layers_lib.fd; on the A1200 Kickstart 3.0's) and bases, and their names from
 * HLE_STRINGS2 in steps of 0x20. */
#define HLE_AUDIO_TABLE 0xC03900u
#define HLE_AUDIO_NEGSIZE 0x24u
#define HLE_AUDIO_BASE (HLE_AUDIO_TABLE + HLE_AUDIO_NEGSIZE)
#if AMIGA_A1200
#define HLE_LAYERS_TABLE 0xC06800u
#define HLE_LAYERS_NEGSIZE 0xD8u
#else
#define HLE_LAYERS_TABLE 0xC03980u
#define HLE_LAYERS_NEGSIZE 0xAEu
#endif
#define HLE_LAYERS_BASE (HLE_LAYERS_TABLE + HLE_LAYERS_NEGSIZE)
#define HLE_STRINGS2 0xC04A00u
/* Intuition's input handler node (an Interrupt), after layers.library's base. */
#define HLE_INT_INPUT_NODE 0xC03A60u

/* The libraries' versions and revisions: the A500's are Kickstart 1.3's (or ours, for dos,
 * layers and audio.device; docs/runtime.md, "HLE"), the A1200's those Kickstart 3.0 rev 39.106
 * lists (docs/runtime.md, "Kickstart handoff"). */
#if AMIGA_A1200
#define HLE_EXEC_VERSION 39
#define HLE_EXEC_REVISION 47
#define HLE_GFX_VERSION 39
#define HLE_GFX_REVISION 89
#define HLE_INT_VERSION 39
#define HLE_INT_REVISION 2084
#define HLE_EXP_VERSION 39
#define HLE_EXP_REVISION 7
#define HLE_DOS_VERSION 39
#define HLE_DOS_REVISION 23
#define HLE_LAYERS_VERSION 39
#define HLE_LAYERS_REVISION 61
#else
#define HLE_EXEC_VERSION 34
#define HLE_EXEC_REVISION 2
#define HLE_GFX_VERSION 34
#define HLE_GFX_REVISION 1
#define HLE_INT_VERSION 34
#define HLE_INT_REVISION 0
#define HLE_EXP_VERSION 34
#define HLE_EXP_REVISION 1
#define HLE_DOS_VERSION 34
#define HLE_DOS_REVISION 0
#define HLE_LAYERS_VERSION 34
#define HLE_LAYERS_REVISION 0
#endif
/* Internal entries in the HLE region, library 15. */
#define HLE_EXCEPTION(n) HLE_ADDR(HLE_LIB_INTERNAL, (n) * 4)
#define HLE_BOOT_RETURN HLE_ADDR(HLE_LIB_INTERNAL, 0x400)
#define HLE_SERVERS HLE_ADDR(HLE_LIB_INTERNAL, 0x404)
#define HLE_WAIT_TOF HLE_ADDR(HLE_LIB_INTERNAL, 0x408)
#define HLE_CONTINUE HLE_ADDR(HLE_LIB_INTERNAL, 0x40C)
#define HLE_GFX_VBLANK HLE_ADDR(HLE_LIB_INTERNAL, 0x410)
#define HLE_CIAA_SERVER HLE_ADDR(HLE_LIB_INTERNAL, 0x414)
#define HLE_CIAB_SERVER HLE_ADDR(HLE_LIB_INTERNAL, 0x418)
#define HLE_DOS_INIT HLE_ADDR(HLE_LIB_INTERNAL, 0x41C)
#define HLE_DOS_CLI_RETURN HLE_ADDR(HLE_LIB_INTERNAL, 0x420)
#define HLE_DOS_DELAY HLE_ADDR(HLE_LIB_INTERNAL, 0x424)
#define HLE_WAIT_PARK HLE_ADDR(HLE_LIB_INTERNAL, 0x428)
#define HLE_INT_HANDLER HLE_ADDR(HLE_LIB_INTERNAL, 0x42C)
#define HLE_AUDIO_INT HLE_ADDR(HLE_LIB_INTERNAL, 0x430)
/* On the A1200: keyboard.device's CIA-A serial-port interrupt, and the code of the ICR owners
 * (timer.device, graphics, disk.resource) whose work the HLE does elsewhere. */
#define HLE_KBD_INT HLE_ADDR(HLE_LIB_INTERNAL, 0x434)
#define HLE_ICR_NOP HLE_ADDR(HLE_LIB_INTERNAL, 0x438)
/* On the A1200: a Read of the CON: window waiting for Return; an EasyRequestArgs requester
 * waiting for its answer. */
#define HLE_CON_PARK HLE_ADDR(HLE_LIB_INTERNAL, 0x43C)
#define HLE_REQ_PARK HLE_ADDR(HLE_LIB_INTERNAL, 0x440)
/* intuition/intuition.i */
#define IDCMP_DISKINSERTED 0x00008000u

/* Interrupt nodes the HLE owns: cia.resource's PORTS and EXTER servers. */
#define HLE_CIAA_NODE 0xC02D40u
#define HLE_CIAB_NODE 0xC02D60u

/* Kickstart's copper start-up list and null sprite, gb_copinit. */
#define HLE_COPINIT 0x000420u
/* gb_copinit's last instruction, a WAIT for a position never reached (hle.c, hle_copinit). */
#define HLE_COPINIT_END (HLE_COPINIT + 84u)
#define HLE_NULL_SPRITE 0x000478u

/* NDK 3.2 graphics/gfxbase.i, view.i, gfx.i, copper.i. */
#define GB_ACTIVIEW 0x22
#define GB_COPINIT 0x26
#define GB_LOFLIST 0x32
#define GB_SHFLIST 0x36
#define GB_VBSRV 0x4A
#define GB_DISPLAYFLAGS 0xCE
#define VP_UCOPINS 0x14
#define UCL_NEXT 0x00
#define UCL_FIRSTCOPLIST 0x04
#define UCL_COPLIST 0x08
#define CL_NEXT 0x00
#define CL_COPINS 0x0C
#define CL_MAXCOUNT 0x1E
#define V_VIEWPORT 0x00
#define V_LOFCPRLIST 0x04
#define V_SHFCPRLIST 0x08
#define V_DYOFFSET 0x0C
#define V_DXOFFSET 0x0E
#define V_MODES 0x10
#define V_SIZE 0x12
#define VP_NEXT 0x00
#define VP_COLORMAP 0x04
#define VP_DSPINS 0x08
#define VP_DWIDTH 0x18
#define VP_DHEIGHT 0x1A
#define VP_DXOFFSET 0x1C
#define VP_DYOFFSET 0x1E
#define VP_MODES 0x20
#define VP_SPRITEPRI 0x22
#define VP_RASINFO 0x24
#define VP_SIZE 0x28
#define RI_BITMAP 0x04
#define RI_RXOFFSET 0x08
#define RI_RYOFFSET 0x0A
#define BM_BYTESPERROW 0x00
#define BM_ROWS 0x02
#define BM_FLAGS 0x04
#define BM_DEPTH 0x05
#define BM_PLANES 0x08
#define BM_SIZE 0x28
/* The V34 ColorMap: Flags, Type, Count, ColorTable. */
#define CM_COUNT 0x02
#define CM_COLORTABLE 0x04
#define CM_SIZE 0x08
/* NDK 3.2 graphics/rastport.i, clip.i, text.i, regions.i; intuition/intuition.i. */
#define RP_LAYER 0x00
#define RP_BITMAP 0x04
#define RP_MASK 0x18
#define RP_FGPEN 0x19
#define RP_BGPEN 0x1A
#define RP_AOLPEN 0x1B
#define RP_DRAWMODE 0x1C
#define RP_FLAGS 0x20
#define RP_LINEPTRN 0x22
#define RP_CP_X 0x24
#define RP_CP_Y 0x26
#define RP_FONT 0x34
#define RP_ALGOSTYLE 0x38
#define RP_TXFLAGS 0x39
#define RP_TXHEIGHT 0x3A
#define RP_TXWIDTH 0x3C
#define RP_TXBASELINE 0x3E
#define RP_TXSPACING 0x40
#define RP_SIZE 0x64
#define LR_RP 0x0C
#define LR_MINX 0x10
#define LR_MINY 0x12
#define LR_MAXX 0x14
#define LR_MAXY 0x16
#define LR_WINDOW 0x28
#define LR_LAYERINFO 0x44
#define LR_CLIPREGION 0x7E
#define LR_SIZE 0xA0
#define IM_SIZE 0x34
#define CL_COUNT 0x1C
#define CL_LSTART 0x14
#define CL_COPPTR 0x10
#define CL_SIZE 0x22
#define CPR_START 0x04
#define CPR_MAXCOUNT 0x08

/* NDK 3.2 exec/nodes.i, lists.i, libraries.i, execbase.i, interrupts.i, ports.i, io.i,
 * tasks.i, memory.i. */
#define LN_SUCC 0x00
#define LN_PRED 0x04
#define LN_TYPE 0x08
#define LN_PRI 0x09
#define LN_NAME 0x0A
#define LH_HEAD 0x00
#define LH_TAIL 0x04
#define LH_TAILPRED 0x08
#define LH_TYPE 0x0C
#define LIB_FLAGS 0x0E
#define LIB_NEGSIZE 0x10
#define LIB_POSSIZE 0x12
#define LIB_VERSION 0x14
#define LIB_REVISION 0x16
#define LIB_IDSTRING 0x18
#define LIB_OPENCNT 0x20
#define LIB_SIZE 0x22
#define EB_CHKBASE 0x26
#define EB_SYSSTKUPPER 0x36
#define EB_SYSSTKLOWER 0x3A
#define EB_MAXLOCMEM 0x3E
#define EB_MAXEXTMEM 0x4E
#define EB_INTVECTS 0x54
#define IV_DATA 0x00
#define IV_CODE 0x04
#define IV_NODE 0x08
#define IV_SIZE 12
#define EB_THISTASK 0x114
#define EB_IDNESTCNT 0x126
#define EB_TDNESTCNT 0x127
#define EB_ATTNFLAGS 0x128
#define EB_MEMLIST 0x142
#define EB_DEVICELIST 0x15E
#define EB_LIBLIST 0x17A
#define EB_RESLIST 0x150
#define EB_PORTLIST 0x188
#define EB_VBLANKFREQ 0x212
#define EB_POWERFREQ 0x213
#define IS_DATA 0x0E
#define IS_CODE 0x12
#define MP_FLAGS 0x0E
#define MP_SIGBIT 0x0F
#define MP_SIGTASK 0x10
#define MP_MSGLIST 0x14
#define MN_REPLYPORT 0x0E
#define IO_DEVICE 0x14
#define IO_UNIT 0x18
#define IO_COMMAND 0x1C
#define IO_FLAGS 0x1E
#define IO_ERROR 0x1F
#define IO_ACTUAL 0x20
#define IO_LENGTH 0x24
#define IO_DATA 0x28
#define IO_OFFSET 0x2C
#define IOTD_COUNT 0x30
#define IOF_QUICK 0x01
#define TC_SIGALLOC 0x12
#define TC_SIGWAIT 0x16
#define TC_SIGRECVD 0x1A
#define TC_SIZE 0x5C
#define MH_ATTRIBUTES 0x0E
#define MH_FIRST 0x10
#define MH_LOWER 0x14
#define MH_UPPER 0x18
#define MH_FREE 0x1C
#define MH_SIZE 0x20
#define MC_NEXT 0x00
#define MC_BYTES 0x04
#define NT_TASK 1
#define NT_DEVICE 3
#define NT_MSGPORT 4
#define NT_MESSAGE 5
#define NT_FREEMSG 6
#define NT_REPLYMSG 7
#define NT_LIBRARY 9
#define NT_MEMORY 10
#define NT_INTERRUPT 2
#define NT_RESOURCE 8
#define MEMF_PUBLIC 0x1u
#define MEMF_CHIP 0x2u
#define MEMF_FAST 0x4u
#define MEMF_CLEAR 0x10000u
#define NT_PROCESS 13
#define NT_SIGNALSEM 15

/* exec/semaphores.i: a SignalSemaphore, and ExecBase's SemaphoreList. */
#define EB_SEMLIST 0x214
#define SS_NESTCOUNT 0x0E
#define SS_WAITQUEUE 0x10
#define SS_OWNER 0x28
#define SS_QUEUECOUNT 0x2C
#define SS_SIZE 0x2E
#define TC_SPREG 0x36
#define TC_SPLOWER 0x3A
#define TC_SPUPPER 0x3E
/* NDK 3.2 dos/dosextens.i: Process. */
#define PR_MSGPORT 0x5C
#define PR_STACKSIZE 0x84
#define PR_TASKNUM 0x8C
#define PR_RESULT2 0x94
#define PR_CURRENTDIR 0x98
#define PR_CIS 0x9C
#define PR_COS 0xA0
#define PR_CLI 0xAC
#define PR_WINDOWPTR 0xB8
/* The V34 Process on the A500; Kickstart 3.0's, with pr_HomeDir and the V36 fields after it, on
 * the A1200 (NDK 3.2 dos/dosextens.i). */
#define PR_HOMEDIR 0xBC
#define PR_SIZEOF (AMIGA_A1200 ? 0xE4 : 0xBC)
/* dos error codes, NDK 3.2 dos/dos.i. */
#define HLE_DOS_NO_FREE_STORE 103
#define HLE_DOS_OBJECT_IN_USE 202
#define HLE_DOS_OBJECT_NOT_FOUND 205
#define HLE_DOS_OBJECT_WRONG_TYPE 212
#define HLE_DOS_DEVICE_NOT_MOUNTED 218
#define HLE_DOS_SEEK_ERROR 219

enum HleFrameKind
{
	HleFrameHandler,
	HleFrameServer,
	HleFrameSoft,
	HleFrameInput,
	HleFrameLibInit,
	HleFrameIcr,
	HleFrameDosRequest,
	HleFrameFmt
};

/* hle.c */
uint32_t hle_r32(struct Amiga *m, uint32_t a);
uint16_t hle_r16(struct Amiga *m, uint32_t a);
uint8_t hle_r8(struct Amiga *m, uint32_t a);
void hle_w32(struct Amiga *m, uint32_t a, uint32_t v);
void hle_w16(struct Amiga *m, uint32_t a, uint16_t v);
void hle_w8(struct Amiga *m, uint32_t a, uint8_t v);
int hle_strcmp(struct Amiga *m, uint32_t a, const char *s);
void hle_new_list(struct Amiga *m, uint32_t list, uint8_t type);
void hle_add_tail(struct Amiga *m, uint32_t list, uint32_t node);
void hle_remove(struct Amiga *m, uint32_t node);
uint32_t hle_rem_head(struct Amiga *m, uint32_t list);
void hle_enqueue(struct Amiga *m, uint32_t list, uint32_t node);
void hle_put_msg(struct Amiga *m, uint32_t port, uint32_t msg);
void hle_jump_table(struct Amiga *m, uint32_t base, uint32_t negsize, int lib);
uint32_t hle_string(struct Amiga *m, uint32_t at, const char *s);
void hle_name(struct Amiga *m, uint32_t a, char out[64]);
void hle_call(struct Amiga *m, const struct HleFrame *f, uint32_t code);

/* hle_exec.c */
int hle_exec_call(struct Amiga *m, uint32_t lvo);
void hle_mem_init(struct Amiga *m);
uint32_t hle_alloc(struct Amiga *m, uint32_t size, uint32_t req, uint32_t at);
void hle_free(struct Amiga *m, uint32_t block, uint32_t size);
/* RawDoFmt's formatting into a buffer hle_alloc gave (hle_exec.c). */
uint32_t hle_fmt(struct Amiga *m, uint32_t fmt, uint32_t data, uint32_t *len, uint32_t *next);
void hle_mem_set(struct Amiga *m, uint32_t mh, const uint32_t *chunks, int count);
int32_t hle_exec_alloc_signal(struct Amiga *m, int32_t want);
void hle_exec_free_signal(struct Amiga *m, uint32_t bit);
void hle_exec_lib_continue(struct Amiga *m, struct HleFrame *f);
void hle_exec_fmt_continue(struct Amiga *m, struct HleFrame *f);
void hle_reply_msg(struct Amiga *m, uint32_t msg);
void hle_exec_io_done(struct Amiga *m, uint32_t io);

/* hle_gfx.c */
int hle_gfx_call(struct Amiga *m, uint32_t lvo);
void hle_gfx_vblank(struct Amiga *m);
void hle_gfx_make_vport(struct Amiga *m, uint32_t view, uint32_t vp);
void hle_gfx_mrgcop(struct Amiga *m, uint32_t view);
void hle_gfx_load_view(struct Amiga *m, uint32_t view);
void hle_gfx_init_vport(struct Amiga *m, uint32_t vp);
void hle_gfx_init_view(struct Amiga *m, uint32_t view);
void hle_gfx_init_rastport(struct Amiga *m, uint32_t rp);
uint32_t hle_gfx_color_map(struct Amiga *m, uint32_t entries);
void hle_gfx_free_color_map(struct Amiga *m, uint32_t cm);
uint32_t hle_gfx_alloc_raster(struct Amiga *m, uint32_t w, uint32_t h);
void hle_gfx_free_raster(struct Amiga *m, uint32_t p, uint32_t w, uint32_t h);
void hle_gfx_free_vport_coplists(struct Amiga *m, uint32_t vp);

/* hle_draw.c */
int hle_draw_call(struct Amiga *m, uint32_t lvo);

/* boot.c */
void boot_return(struct Amiga *m);

/* hle_disk.c */
void hle_td_io(struct Amiga *m, uint32_t ioreq);
int hle_td_call(struct Amiga *m, uint32_t lvo);
extern const uint32_t hle_td_units[AMIGA_DRIVES];

/* hle_input.c */
void hle_input_io(struct Amiga *m, uint32_t io);
void hle_input_key(struct Amiga *m, uint8_t sdr);
void hle_input_vblank(struct Amiga *m);
int hle_input_pending(struct Amiga *m);
void hle_input_take(struct Amiga *m);
int hle_in_call(struct Amiga *m, uint32_t lvo);

/* hle_intuition.c */
void hle_int_build(struct Amiga *m);
int hle_int_call(struct Amiga *m, uint32_t lvo);
void hle_int_input(struct Amiga *m);
void hle_int_vblank(struct Amiga *m);

/* hle_layers.c */
void hle_layers_build(struct Amiga *m);
int hle_layers_call(struct Amiga *m, uint32_t lvo);

/* hle_res.c: Kickstart 3.0's resources, on the A1200 only, in the slow-RAM range no A1200
 * MemHeader lists. */
#define HLE_POTGO_BASE 0xC07014u
#define HLE_CIAA_BASE 0xC07098u
#define HLE_CIAB_BASE 0xC07158u
#define HLE_RES_STRINGS 0xC07300u
#define HLE_ICR_NODES 0xC07700u
void hle_res_build(struct Amiga *m);
int hle_res_call(struct Amiga *m, int lib, uint32_t lvo);
uint32_t hle_res_icr_next(struct Amiga *m, int which, uint32_t *pending);

/* hle_timer.c: timer.device, on the A1200 only (hle_timer_vblank does nothing on the A500). */
#define HLE_TIMER_BASE 0xC07444u
void hle_timer_build(struct Amiga *m);
int hle_timer_open(struct Amiga *m, uint32_t unit);
int hle_timer_io(struct Amiga *m, uint32_t io);
void hle_timer_abort(struct Amiga *m, uint32_t io);
void hle_timer_vblank(struct Amiga *m);
int hle_timer_call(struct Amiga *m, uint32_t lvo);

/* hle_kbd.c: keyboard.device, on the A1200 only. */
#define HLE_KBD_BASE 0xC07624u
void hle_kbd_build(struct Amiga *m);
void hle_kbd_key(struct Amiga *m, uint8_t code);
void hle_kbd_io(struct Amiga *m, uint32_t io);

/* utility.library, on the A1200 only (hle_res.c): its table from 0xC06C00. */
#define HLE_UTIL_BASE 0xC06D28u

/* hle_con.c: the Workbench screen and a CON: window, on the A1200 only; hle_font.c: the font. */
extern const uint8_t hle_font8[96][8];
int hle_con_open(struct Amiga *m, const char *spec);
void hle_con_close(struct Amiga *m);
void hle_con_write(struct Amiga *m, uint32_t buf, uint32_t len);
int32_t hle_con_read(struct Amiga *m, uint32_t buf, uint32_t len);
int hle_con_park(struct Amiga *m);
void hle_con_key(struct Amiga *m, uint8_t code);
int hle_con_request(struct Amiga *m, uint32_t window, uint32_t es, uint32_t idcmp, uint32_t args);
int hle_con_request_park(struct Amiga *m);
void hle_con_disk_inserted(struct Amiga *m);
void hle_con_vblank(struct Amiga *m);

/* hle_audio.c */
void hle_audio_build(struct Amiga *m);
int hle_audio_call(struct Amiga *m, uint32_t lvo);
int8_t hle_audio_open(struct Amiga *m, uint32_t io);
int hle_audio_io(struct Amiga *m, uint32_t io);
void hle_audio_abort(struct Amiga *m, uint32_t io);
void hle_audio_interrupt(struct Amiga *m, int ch);

/* hle_dos.c */
void hle_dos_build(struct Amiga *m);
void hle_dos_boot(struct Amiga *m);
void hle_dos_mount(struct Amiga *m);
void hle_dos_cli_return(struct Amiga *m);
int hle_dos_call(struct Amiga *m, uint32_t lvo);
void hle_dos_request_done(struct Amiga *m, const struct HleFrame *f);
uint32_t hle_dos_loadseg(struct Amiga *m, const char *path);
int hle_dos_packet(struct Amiga *m, uint32_t port, uint32_t msg);
void hle_dos_time(struct Amiga *m, uint32_t *secs, uint32_t *micros);

/* hle_args.c: ReadArgs over `args_line` (0: none) and FreeArgs, with the CPU's registers. */
void hle_dos_readargs(struct Amiga *m, uint32_t args_line);
void hle_dos_freeargs(struct Amiga *m);

/* hle_ofs.c: the Old and Fast File Systems (DOS\0, DOS\1), read from an ADF image. */
struct HleOfsEntry
{
	int32_t type;        /* sec_type: 1 root, 2 directory, -3 file */
	uint32_t size, blocks, protect, parent;
	uint32_t days, mins, ticks;
	char name[32];
	char comment[80];
};
uint32_t hle_ofs_hash(const char *name);
int hle_ofs_volume(const uint8_t *adf, char name[32]);
void hle_ofs_volume_date(const uint8_t *adf, uint32_t *days, uint32_t *mins, uint32_t *ticks);
int32_t hle_ofs_lookup(const uint8_t *adf, uint32_t dir, const char *name);
uint32_t hle_ofs_read(const uint8_t *adf, uint32_t header, uint32_t pos, uint8_t *out, uint32_t n);
int hle_ofs_entry(const uint8_t *adf, uint32_t block, struct HleOfsEntry *e);

/* hle_hunk.c: AmigaDOS load files. `read` copies up to n bytes from the source's current
 * position and returns how many it copied. */
struct HleHunkSource
{
	uint32_t (*read)(struct Amiga *m, void *ctx, uint8_t *out, uint32_t n);
	void *ctx;
};
/* What LoadSeg of an overlaid file leaves: the overlay table and the hunk table it allocated,
 * and the stream the overlay manager reads the nodes from. table 0: not overlaid. */
struct HleHunkOverlay
{
	uint32_t table, size;
	uint32_t hunks, hunks_size;
	uint32_t stream;
};
uint32_t hle_hunk_load(struct Amiga *m, const struct HleHunkSource *src, uint32_t stream, int32_t *error,
	struct HleHunkOverlay *ov);
uint32_t hle_hunk_node(struct Amiga *m, const struct HleHunkSource *src, uint32_t table, int32_t *error);
void hle_hunk_unload(struct Amiga *m, uint32_t seglist);

#endif
