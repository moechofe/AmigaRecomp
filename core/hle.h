#ifndef HLE_H
#define HLE_H

#include "amiga.h"

/* Guest-memory layout of the emulated Kickstart. ExecBase, its jump table, the boot task's
 * stack, the ROM's IORequest and ThisTask sit where Kickstart 1.3 had them at the bootblock
 * call (docs/runtime.md, "Kickstart handoff"); everything else the HLE
 * owns lives above 0xC02000, clear of them. */
#define HLE_EXEC_BASE 0xC00276u
#define HLE_EXEC_NEGSIZE 0x276u
#define HLE_EXEC_POSSIZE 0x24Cu
#define HLE_SLOW_MEMHEADER 0xC004C2u
#define HLE_BOOT_USP 0xC014B2u
#define HLE_BOOT_IOREQ 0xC014E2u
#define HLE_THIS_TASK 0xC01570u
#define HLE_SSP 0xC80000u
#define HLE_CHIP_MEMHEADER 0x000400u

#define HLE_GFX_TABLE 0xC02000u
#define HLE_GFX_NEGSIZE 0x28Eu
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

/* Internal entries in the HLE region, library 15. */
#define HLE_EXCEPTION(n) HLE_ADDR(HLE_LIB_INTERNAL, (n) * 4)
#define HLE_BOOT_RETURN HLE_ADDR(HLE_LIB_INTERNAL, 0x400)
#define HLE_SERVERS HLE_ADDR(HLE_LIB_INTERNAL, 0x404)
#define HLE_WAIT_TOF HLE_ADDR(HLE_LIB_INTERNAL, 0x408)
#define HLE_CONTINUE HLE_ADDR(HLE_LIB_INTERNAL, 0x40C)
#define HLE_GFX_VBLANK HLE_ADDR(HLE_LIB_INTERNAL, 0x410)
#define HLE_CIAA_SERVER HLE_ADDR(HLE_LIB_INTERNAL, 0x414)
#define HLE_CIAB_SERVER HLE_ADDR(HLE_LIB_INTERNAL, 0x418)

/* Interrupt nodes the HLE owns: cia.resource's PORTS and EXTER servers. */
#define HLE_CIAA_NODE 0xC02D40u
#define HLE_CIAB_NODE 0xC02D60u

/* Kickstart's copper start-up list and null sprite, gb_copinit. */
#define HLE_COPINIT 0x000420u
#define HLE_NULL_SPRITE 0x000478u

/* NDK 3.2 graphics/gfxbase.i, view.i, gfx.i, copper.i. */
#define GB_ACTIVIEW 0x22
#define GB_COPINIT 0x26
#define GB_LOFLIST 0x32
#define GB_SHFLIST 0x36
#define GB_VBSRV 0x4A
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
#define CM_COUNT 0x02
#define CM_COLORTABLE 0x04
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
#define LIB_OPENCNT 0x20
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
#define MEMF_PUBLIC 0x1u
#define MEMF_CHIP 0x2u
#define MEMF_FAST 0x4u
#define MEMF_CLEAR 0x10000u

enum HleFrameKind
{
	HleFrameHandler,
	HleFrameServer,
	HleFrameSoft,
	HleFrameInput
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

/* hle_exec.c */
int hle_exec_call(struct Amiga *m, uint32_t lvo);
void hle_mem_init(struct Amiga *m);
uint32_t hle_alloc(struct Amiga *m, uint32_t size, uint32_t req, uint32_t at);
void hle_free(struct Amiga *m, uint32_t block, uint32_t size);
void hle_mem_set(struct Amiga *m, uint32_t mh, const uint32_t *chunks, int count);

/* hle_gfx.c */
int hle_gfx_call(struct Amiga *m, uint32_t lvo);
void hle_gfx_vblank(struct Amiga *m);

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

#endif
