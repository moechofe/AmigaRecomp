# Runtime — build, run, timing, Kickstart HLE

How the game-agnostic runtime in `core/` and `frontend/` is built, driven and timed, and what
its emulated Kickstart 1.3 provides. Everything here describes the code as it is. Measurements
come from the prior project (`../proof`, read-only), cited by file and section; the facts that
belong to the first game are in
[`games/deuteros/docs/format.md`](../games/deuteros/docs/format.md) and are not restated.

The runtime is the Musashi 68000 interpreter plus an OCS chipset (copper, bitplanes, sprites,
blitter, CIAs, Paula audio) and a Kickstart 1.3 replaced by C. No ROM is read.

## Build and run

Build a game with the commands of `docs/game.md`, item 1; they are not repeated here. The
executable is named `<id>` (`<id>.exe`) and is written to the game's build directory.

    <id> [--headless] [--frames N] [--script FILE] [--shot-dir DIR] [--state PATH]
         [--pref DIR] [--dump FILE] [--no-sprites] <image.adf>...

| Option | Effect |
| --- | --- |
| `--headless` | no window, no audio, no pacing: frames run as fast as the host allows |
| `--frames N` | headless only: stop with exit 0 once the frame counter reaches N (0 or absent: no limit) |
| `--script FILE` | an input script (*Input scripts*); read whole, at most 65 535 bytes |
| `--shot-dir DIR` | where `shot` and F12 write PNGs; default `<pref>shots` |
| `--state PATH` | the save-state file; default `<pref>state.sav` |
| `--pref DIR` | the pref directory; default `SDL_GetPrefPath("AmigaRecomp", "<id>")`; a missing trailing `/` is added and the directory is created |
| `--dump FILE` | write the 512 KB of chip RAM to FILE at exit (failure is reported; the exit code is unchanged) |
| `--no-sprites` | start with sprite rendering off (a script's `sprites on` turns it back on) |

Positional arguments are ADF paths. They fill the game's **read-only** roles in the order of
`game_def.disks` (`core/game.h`); the count must equal the number of read-only roles and each
file must be exactly 901 120 bytes (80 cylinders x 2 heads x 11 sectors x 512). A **writable**
role takes no argument: its image is `<pref><role id>.adf` if that file exists, else 901 120
zero bytes. After every frame, and once more at exit, each writable image the game changed is
written back to that file (a write failure is reported and the run continues). At boot every
drive DF0-DF3 gets the role `game_def.drives[n]` names (-1: empty).

Window mode (no `--headless`) opens a 960 x 768 window, three times the 320 x 256 frame,
with an OpenGL 3.3 core context, and paces itself to one frame of 142 102 CPU cycles at
7 093 790 Hz (about 20.03 ms; *Timing*). Audio is 48 kHz 16-bit stereo from Paula. Keys the
frontend takes:

| Key | Action |
| --- | --- |
| F1 | swallowed, never reaches the game; the settings menu is a later milestone |
| F5 / F9 | save / load the state file (*Save states*); failures print and the run goes on |
| F11 | put the next role of `game_def.df0_cycle` in DF0 and print its label |
| F12 | write `frame-NNNNNN.png` (the frame counter, six digits) into the shot directory |
| other keys | letters, digits, F2-F10, Space, Backspace, Tab, Return, Esc, Delete, arrows, Shift, Ctrl, Alt as Amiga raw keycodes (HRM appendix H) |

The mouse is not captured: the OS cursor is hidden and, every frame, the frontend gives the
game's pointer the whole remaining error as counts, clamped to +/-127 a frame (unit gain,
closed loop). The error is between the OS cursor, scaled to frame pixels, and where
`denise_pointer()` says the game's pointer sprite is. This has not been tried interactively;
only the code exists. With `--script` in window mode the script alone moves the pointer.

Exit codes, from `frontend/main.c`:

| Code | When |
| --- | --- |
| 0 | normal end: window closed, `--frames` reached, the script ended or said `quit` |
| 1 | a fatal runtime error (before or during the run); SDL or GL set-up failure; a `shot` whose PNG cannot be written |
| 2 | usage error; no pref directory; unreadable script; wrong image count or an unreadable or wrong-sized image; a script error (`script error: ...`); a failed `state save` / `state load` in a script |
| 3 | a script timeout (`until colours` never reached) |

Messages that explain exit 2 for images: `cannot read <path>`, `<path>: not an 880 KB
double-density ADF`, and, for a wrong count, `<title> needs N images, in this order:` followed
by the read-only role ids. A fatal runtime error prints `fatal: <message>`, then PC, SR and
frame, then D0-D7 and A0-A7, to stderr.

Every run ends by printing, to stdout: `frames F, instructions I, cycles C, reads R, loads L`
(frame count, interpreter instructions, CPU cycles, trackdisk reads served, distinct entries in
the load log), then one line `  <library> -<lvo>: N calls` for each library vector that was
called, in the order exec, graphics, trackdisk, input. `shot` prints `shot <path> (frame F, N
colours)`, and a runtime log line `load N: DFn=<role> ADF 0x<offset> +0x<length> -> RAM
0x<address>` is printed for every trackdisk read.

### Windows toolchain

Observed 2026-10-06 on Windows 11 (build 26200), building `games/deuteros`: WinLibs GCC 16.2.0
(MinGW-w64, x86_64, UCRT runtime, POSIX threads; winget id `BrechtSanders.WinLibs.POSIX.UCRT`),
its bundled CMake 4.4.2 and Ninja 1.13.2, Python 3.14 and Git. Only GCC has built the project:
`cmake/amigarecomp.cmake` passes GCC options (`-Wextra`, `-w`, `-static`).

- winget adds WinLibs' `mingw64\bin` to the user `PATH`. A terminal opened before the install
  does not see it, and `tools/doctor.py` then reports `[--]  C compiler`.
- The first configure cloned the five dependencies into the root `build/deps/` (182 MB) in
  170 s; the first build ran 555 steps in about 140 s; the build tree is 234 MB.
- The executable is linked `-static` and is a console-subsystem program, so its stdout and
  stderr reach the terminal. `objdump -p <id>.exe` lists only these DLLs: ADVAPI32, GDI32,
  IMM32, KERNEL32, ole32, OLEAUT32, SETUPAPI, SHELL32, USER32, VERSION, WINMM, and the
  `api-ms-win-crt-*` forwarders of the Universal CRT, which is part of Windows 10 and later.
- `frontend/main.c` is compiled into each executable instead of `amiga_frontend`. Kept in the
  archive, it is never pulled in on MinGW and the link fails with `undefined reference to
  'WinMain'`; the comment in `cmake/amigarecomp.cmake` says why.

## Input scripts

A script is text, one directive per line. `#` starts a comment that runs to the end of the line
(a `#` inside an argument is therefore not possible); blank lines are ignored; lines are cut at
127 characters. Directives that take frames are listed below; one that takes none lets the next
start at once, within the same frame boundary. The grammar, from `core/script.c`:

| Directive | Effect |
| --- | --- |
| `wait N` | run N frames |
| `until colours N [T]` | run frames until the 320 x 256 frame holds exactly N distinct 12-bit colours; checked before each frame. T is the timeout in frames, default 3 000; if it runs out the script ends with exit 3 and `line L: N colours never reached in T frames (M on screen)` |
| `key NAME` | key down for one frame; the key-up is sent at the start of the next frame, which the directive does not itself take |
| `click [X Y]` | with coordinates: 8 frames of motion of -100,-100 per frame (the pointer is parked in the top-left corner), then motion of at most 100 counts a frame until X, Y counts have been given, then the left button held for 3 frames, then one frame released. Bare `click`: only the hold and release, where the pointer is |
| `mouse DX DY` | one frame of motion DX, DY counts (buttons unchanged) |
| `disk dfN ROLE` | put the role ROLE (an id from `game_def.disks`) in drive DFN (N = 0-3); instant, takes no frame |
| `sprites on\|off` | sprite rendering; any argument but `on` means off |
| `state save` / `state load` | write or restore the state file, as F5 / F9 |
| `shot NAME` | write `<shot-dir>/NAME.png` (the 320 x 256 frame), instant |
| `quit` | end the script with exit 0 |

`NAME` of `key` is one of `0`-`9`, `a`-`z`, `space`, `return`, `esc`, `f1`-`f10`, `shift`.

What ends a script: `quit`; or the end of the text, which is the same as `quit` (exit 0);
a timeout (exit 3); or an error (exit 2). Errors name the line and the line's text:
`line L: expected \`until colours <n> [<timeout>]\``, `unknown key`, `expected \`disk df<n>
<role>\``, `expected \`state save|load\``, `unknown directive`; the two `disk` errors are the
exceptions, with no line number: `disk: unknown drive <arg>` (the drive is not `df0`-`df3`)
and `disk: unknown role <arg>`. A misspelt or incomplete `wait`, `key`, `mouse`, `sprites` or
`shot` is `unknown directive` (or `unknown key`). In headless mode the script is consulted
before each frame, so a script that ends with a `wait` runs that wait to completion first.

## Save states

One state file (`--state`, F5/F9, `state save|load`). Layout, from `core/amiga.h` and
`core/state.c`:

| Offset | Field |
| --- | --- |
| 0 | `magic[8]` = `ARSTATE` and a NUL |
| 8 | `version` (`STATE_VERSION`, 1) |
| 12 | `size` = `sizeof(struct Amiga)` of the writing build |
| 16 | `role_crc[8]`: CRC-32 (zlib) of each attached **read-only** role's image, 0 for a writable or absent role |
| 48 | the raw `struct Amiga`, as it stands between frames |

The struct is written in host layout, so a state belongs to the build that wrote it.
`state_check` compares the header with the current one and returns `not an AmigaRecomp state`
(magic), `written by another build` (version or size) or `state was saved with different disk
images` (any CRC). A load reads the whole file into a scratch machine first and touches the
live one only when the header matches and the file is exactly header plus struct; the other
failures are `no state at <path>` and `<path>: truncated or oversized`, printed as
`<path>: <reason>`. A save failure prints `cannot write the state <path>`. A script exits 2 on
either failure.

`state_restore` copies the emulated machine: chip and slow RAM, the load log and its counters,
the key queue, the frame buffer, the drive-to-role table, the CPU, the cycle, instruction and
frame counters, copper, Denise, blitter, Paula, both CIAs and the HLE state. It deliberately
keeps what belongs to the host and the process: the disk images (so a writable disk is never
rolled back, and a state saved with other read-only images is refused), the per-process LVO
call counters printed at exit, the `booted` flag, the `log` binding, the sprite switch and
the fatal flag. Musashi's registers are copied in and out of `struct Cpu` around every run of
the interpreter (`interp_sync_in` / `interp_sync_out`), and states are taken between frames,
so none of its state needs saving [INFERENCE from the code; no test isolates it].

A field added to `struct Amiga` must be added to `state_restore` too, or it silently stays at
its live value.

Evidence for the design: `../proof/doc.agent/recomp.md`, "Save states" (the same split, and a
state resumed in a fresh process reproducing a 300-frame run, measured 2026-10-06 in the
proof).

## Timing

PAL, derived from the CPU cycle counter (`core/sched.c`):

- 313 lines per frame, 227 colour clocks (CCK) per line, 454 CPU cycles per line (two per CCK),
  so **142 102 cycles per frame**. The beam position is a function of the cycle count alone.
- Evidence: WinUAE's `fs` command ("wait an exact number of scanlines or colour clocks and
  report them") answers `Cycles: 227 Chip, 454 CPU.` for one line and 71 051 cycles for 313
  lines; `fc 3546895` answered what it was given. Measured 2026-09-05, in
  `../proof/doc.agent/emulator.md`, "The commands, as the 6.0.3 binary accepts them".
- The CPU clock is 7 093 790 Hz, twice the PAL colour clock of 3 546 895 Hz (HRM appendix F);
  one frame is 20.0319 ms.
- **CIA E clock**: one tick per 10 CPU cycles, for both CIAs, timers A and B. CIA-A's
  time-of-day counter advances once a frame, at line 0 (the vertical sync); CIA-B's once a
  line, at the line's end (the horizontal sync). VERTB is raised at line 0.
- **Instruction-boundary interrupt rule.** A 68000 latches the interrupt level before the last
  prefetch of an instruction, so an interrupt that appears during an instruction is taken
  after the *next* one. `Cpu.ipl_prev` holds the level that was pending when the current
  instruction began. `amiga_run_frame` (`core/run.c`) and `interp_run` (`core/interp.c`) take an
  interrupt only if its level is above the mask and `ipl_prev` is above the mask too; a CPU
  parked in the HLE region or stopped is between instructions and takes it at once. Register
  reads are timed at the end of the reading instruction (`amiga_now` adds the instruction's
  cycle cost), so a status bit that rises during an instruction is seen by it, while the
  interrupt is taken at the boundary after. Evidence: a game loop polling INTREQ for VERTB
  while exec serviced the interrupt exits on every frame under WinUAE (cycle-exact; 8 of 8
  frames at a breakpoint, 2026-10-03) and never exits under the naive "next boundary" rule;
  `../proof/doc.agent/recomp.md`, "Timing model and its simplifications". The game itself is in
  [`games/deuteros/docs/format.md`](../games/deuteros/docs/format.md).
- **The blitter and the disk are instant**: a blit finishes when BLTSIZE is written (BBUSY
  reads 0), a trackdisk transfer lands in RAM before the call returns, and so does a hardware
  disk DMA: the second DSKLEN write copies the words and raises DSKBLK at once (the rotation
  position is `amiga_now / 227 % 6250` words, so a WORDSYNC scan starts there). No DMA
  contention: the CPU never waits for bitplane, copper, sprite or blitter DMA.
- The copper runs at colour-clock granularity.
- **Lores only.** Hires, HAM and dual playfield are logged once and drawn as lores.
- **Unmeasured: the first line of sprite DMA.** `DENISE_SPRITE_LINE` (`core/denise.c`) is 25,
  the line after the PAL vertical blank; every sprite's control-word fetch restarts on it. No
  observation of it exists; the oracle shots of the proof were taken with sprites off
  (`../proof/doc.agent/recomp.md`, "Timing model and its simplifications").

## HLE

Kickstart is not read: its libraries are C functions and its data structures are written into
guest RAM, at the addresses the real ROM used where the game could see them (*Kickstart
handoff*) and above 0xC02000 elsewhere. Structure field offsets follow the NDK 3.2 includes.

A library's jump table holds `JMP` slots that point into `0xF00000 + (lib << 12) + (-LVO)`; the
region 0xF00000-0xF0FFFF is "HLE space". When the PC enters it, `hle_dispatch` runs the C
function and pops the return address. Libraries: 0 exec, 1 graphics, 2 trackdisk.device, 3
input.device, 15 internal (exception vectors, bootblock return, interrupt servers, the
continuation guest code returns into). Anything not implemented calls `amiga_fatal`, naming the
library, the vector and the caller (`<library> LVO -N (0xNNN) is not implemented, called from
0xAAAAAA`); every exception vector but the autovectors is fatal. (Evidence for the scheme:
`../proof/doc.agent/recomp.md`, "HLE — the emulated Kickstart".)

Guest-memory layout, from `core/hle.h`:

| Address | What |
| --- | --- |
| 0x000400 | chip RAM's MemHeader |
| 0x000420 / 0x000478 | graphics' copper start-up list (`gb_copinit`, 22 instructions) / null sprite |
| 0xC00000-0xC00275 | exec's jump table (`HLE_EXEC_NEGSIZE` 0x276) |
| 0xC00276 | ExecBase (`HLE_EXEC_BASE`; also the longword at absolute 4); positive size 0x24C |
| 0xC004C2 | slow RAM's MemHeader |
| 0xC014B2 | the boot task's user stack at the bootblock call (`HLE_BOOT_USP`) |
| 0xC014E2 | the ROM's trackdisk IORequest (`HLE_BOOT_IOREQ`) |
| 0xC01570 | ThisTask (`HLE_THIS_TASK`) |
| 0xC80000 | the supervisor stack (`HLE_SSP`) |
| 0xC02000 | graphics.library's jump table (0x28E bytes); the base is 0xC0228E |
| 0xC02A00 | trackdisk.device's jump table (0x24 bytes); the base is 0xC02A24 |
| 0xC02A80, 0xC02AC0 | trackdisk units 0 and 1 (message ports) |
| 0xC02B00-0xC02B4F | the five server lists of 16 bytes: PORTS, COPER, VERTB, EXTER, NMI (interrupt bits 3, 4, 5, 13, 15) |
| 0xC02B80, 0xC02BC0 | trackdisk units 2 and 3 |
| 0xC02C00 | library and device name strings, every 0x20 |
| 0xC02D00 | the boot IORequest's reply port |
| 0xC02D40 / 0xC02D60 | the interrupt nodes of cia.resource's PORTS (CIA-A) and EXTER (CIA-B) servers |
| 0xC02E00 | input.device's jump table (0x24 bytes); the base is 0xC02E24 |
| 0xC02E70 | the input handler list |
| 0xC02F00 | the input event queue: 16 events of 0x16 bytes |

Units 2 and 3 sit in the gap after the server lists (which end at 0xC02B50) and before the
strings; nothing else in `core/` writes 0xC02B50-0xC02BFF (checked by searching `core/` for
`0xC02B`, 2026-10-06). The addresses of ExecBase, its table, both MemHeaders, the boot stack,
the boot IORequest and ThisTask are the measured ones (`../proof/doc.format/loader.md`,
"Machine state at the bootblock call"); the rest is ours.

### Library calls

LVO numbers are the negative offsets, as in the stats lines at exit.

**exec** (`core/hle_exec.c`): -120 Disable, -126 Enable, -132 Forbid, -138 Permit, -150
SuperState (supervisor mode on the caller's stack; D0 = the old SSP), -156 UserState, -162
SetIntVector, -168 AddIntServer, -174 RemIntServer, -180 Cause, -198 AllocMem, -204 AllocAbs,
-210 FreeMem, -216 AvailMem (free bytes, or the largest chunk with MEMF_LARGEST), -294
FindTask, -306 SetSignal, -318 Wait, -324 Signal, -330 AllocSignal, -336 FreeSignal, -354
AddPort, -360 RemPort, -366 PutMsg, -372 GetMsg, -378 ReplyMsg, -384 WaitPort, -408
OldOpenLibrary, -552 OpenLibrary, -414 CloseLibrary, -444 OpenDevice, -450 CloseDevice, -456
DoIO, -462 SendIO, -468 CheckIO, -474 WaitIO, -480 AbortIO. Limits, each a fatal error when
exceeded: `FindTask` accepts only a NULL name; `Wait` is fatal unless a requested signal is
already set (there is no scheduler); `WaitPort` on an empty port is fatal; `Add/RemIntServer`
need an interrupt that has a server list; `Cause` holds at most 16 software interrupts;
`OpenLibrary` knows only `graphics.library`; `OpenDevice` knows `trackdisk.device` units 0-3
and `input.device` unit 0; `FreeMem` outside every MemHeader is fatal. Memory is two
MemHeaders (chip, slow) with real free lists, kept by `hle_alloc` / `hle_free`.

**graphics** (`core/hle_gfx.c`): -30 BltBitMap (computed directly in RAM, no blitter
registers; fatal on overlapping planes of different strides), -192 LoadRGB4, -198
InitRastPort, -204 InitVPort, -210 MrgCop, -216 MakeVPort, -222 LoadView, -228 WaitBlit, -270
WaitTOF (parks until the next vertical blank), -288 SetRGB4, -360 InitView, -384 VBeamPos,
-390 InitBitMap, -408 GetSprite, -414 FreeSprite, -420 ChangeSprite, -426 MoveSprite, -456
OwnBlitter, -462 DisownBlitter. `MakeVPort` and `MrgCop` build the copper list for one
non-interlaced lores ViewPort; other modes or a horizontal RxOffset are fatal.

**trackdisk.device** (`core/hle_disk.c`): the device's own vectors -30 BeginIO and -36 AbortIO,
plus the commands below, reached through `DoIO` / `SendIO`.

**input.device** (`core/hle_input.c`): -30 BeginIO and -36 AbortIO, plus the commands below.

### Floppy hardware

`core/disk.c`, for a loader that drives Paula and the CIAs itself. A disk is its ADF; every
track is encoded to MFM on each transfer (no cache): 11 sectors of 544 words, then a gap, 6250
words a revolution (200 ms at 32 us a word). Layout per the ADF FAQ (Laurent Clevy), "MFM
track format"; registers per the HRM, "Disk controller" chapter and appendix F.

- `DSKPTH/L`, `DSKSYNC` and `DSKLEN` (0x020, 0x022, 0x07E, 0x024). A read starts when DSKLEN
  is written twice with bit 15 set. Fatal: a write transfer (bit 14), DMACON without DMAEN and
  DSKEN, not exactly one drive selected, an empty drive, a stopped motor, WORDSYNC with no
  sync word on the track. FAST clear is logged once and read as MFM. WORDSYNC raises DSKSYN
  and starts the copy after the sync word; DSKBLK is raised when the copy ends.
- CIA-B PRB: /SELn falling latches the motor from /MTR; the rising edge of /STEP moves the
  head one cylinder (DIR high: toward 0; limits 0 and 79); /SIDE selects the head. The
  trackdisk read leaves the head on the cylinder it read.
- CIA-A PRA bits 2-5 (`/CHNG`, `/WPRO`, `/TK0`, `/RDY`) come from the selected drives and
  read high with none selected. `/CHNG` is low for an empty drive and after an insert once
  booted, and a step clears it. A role without a writable image reads write-protected.
- The index pulse is CIA-B's /FLAG once a revolution while a selected drive has a disk and its
  motor on. DSKBYTR still reads 0; the transfer is not timed.
- Evidence: a throwaway harness drove these registers on Disk 1 and decoded RAM as a loader
  does (sync, header and data checksums, data against the ADF) for tracks 0, 40 and 41, after
  20 steps: all sectors exact. No game has yet made a disk DMA; the step edge is not verified
  against the HRM.

### trackdisk

Unit n is drive DFn (the unit pointers are those of the layout table). The unit's role is
`drive_role[n]`; reads and writes are instant copies to and from the role's image.

- An empty drive fails every command with error 29 (`TDERR_DiskChanged`) except `TD_CHANGENUM`
  (the change counter) and `TD_CHANGESTATE` (1, no disk).
- `amiga_insert(drive, role)` puts a role (or -1) in a drive. Once the machine has booted the
  drive's change counter goes up and is written to the unit's `tdu_Counter` (+0x3C); before
  boot it does not.
- An extended command (bit 0x8000) fails with 29 when `iotd_Count` is below the change counter.
- Commands: `CMD_READ` (2), `CMD_WRITE` (3), `CMD_UPDATE` (4), `CMD_CLEAR` (5), `TD_MOTOR` (9),
  `TD_SEEK` (10), `TD_FORMAT` (11, as a write), `TD_CHANGENUM` (13), `TD_CHANGESTATE` (14,
  0 = disk in), `TD_PROTSTATUS` (15), `TD_GETDRIVETYPE` (18, 3.5"), `TD_GETNUMTRACKS` (19, 160),
  `TD_ADDCHANGEINT` and `TD_REMCHANGEINT` (20, 21: logged once, never fired). Anything else
  is fatal.
- A role that is writable accepts writes and marks itself dirty; a read-only role answers
  `TD_PROTSTATUS` with 1 and fails writes with 28 (`TDERR_WriteProt`). A transfer outside the
  901 120 bytes fails with 20.
- Every read is added to the load log (identical drive, offset, length and destination are
  logged once) and printed.

### input.device

`IND_ADDHANDLER` (9) and `IND_REMHANDLER` (10) keep a priority-ordered handler list;
`IND_SETTHRESH`, `SETPERIOD`, `SETMPORT`, `SETMTYPE`, `SETMTRIG` (12-16) are accepted and
ignored (logged once); anything else is fatal. Events go to the handlers at the end of the
interrupt that produced them. A key from CIA-A's serial register becomes a `RAWKEY` event (the
PORTS server plays keyboard.device); the mouse counters and buttons are sampled once a
vertical blank and become `RAWMOUSE` events (graphics' VERTB server plays gameport.device).

### Observed graphics.library 34 / Kickstart 1.3 behaviour

"graphics.library 34 / Kickstart 1.3 behaviour observed under WinUAE", written into the code as
fixed values because the real library produced them and the game's structures show them. Where
each lives:

| Observation | Where in `core/` |
| --- | --- |
| the copper list's vertical display-window bytes: `DIWSTRT` high byte 0x05, `DIWSTOP` high byte 0x40 | `gfx_viewport` in `hle_gfx.c` |
| copper list buffer `MaxCount` = instruction count + 3 | `gfx_mrgcop` in `hle_gfx.c` |
| `InitVPort` leaves `SpritePriorities` 0x24 | LVO -204 in `hle_gfx.c` |
| `InitView` leaves `DyOffset` 0x2C and `DxOffset` 0x81 | LVO -360 in `hle_gfx.c` |
| graphics' vertical-blank server has priority 10; cia.resource's PORTS and EXTER servers have priority 0 | `hle_build` in `hle.c` |
| `VPOSR` reads 0x8000 (long-frame bit set, PAL OCS Agnus ID 0), plus bit 8 of the line | `custom_read` in `custom.c` |

Evidence. The display-window values: the first game leaves the window wide open, `DIWSTRT`
0x0581 and `DIWSTOP` 0x40C1, `../proof/doc.format/screen.md`, "The screen". The copper-list
shape, the two window bytes and the "count + 3" `MaxCount` were matched instruction for
instruction against lists dumped under WinUAE, `../proof/doc.agent/recomp.md`, "HLE -- the
emulated Kickstart". The server priorities are marked "measured" in the same section. The
proof's code comments carry `SpritePriorities`, `DyOffset`/`DxOffset` ("as observed in the
game's ViewPorts / Views") and the `VPOSR` read ("at the bootblock call"); no dated log of
those four readings is in the proof's docs, so their measurement date is unknown. Nothing in
the proof's documents contradicts them.

## Kickstart handoff

`amiga_boot` (`core/boot.c`) builds the machine as Kickstart 1.3 leaves it when it calls the
disk's bootblock, then jumps in. Evidence: `../proof/doc.format/loader.md`, "Machine state at
the bootblock call" (WinUAE 6.0.3 on Kickstart 1.3 rev 34.5, cold boot, measured 2026-10-03,
two runs of two identical).

| What | Value set | Measured |
| --- | --- | --- |
| SR | 0x0000 with Z set (0x0004): user mode, mask 0 | 0x0004 |
| A7 | `HLE_BOOT_USP` 0xC014B2, with `HLE_BOOT_RETURN` pushed there | A7 = 0xC014B2 |
| supervisor stack | `HLE_SSP` 0xC80000 | ISP 0xC80000 |
| A1 | `HLE_BOOT_IOREQ` 0xC014E2, the IORequest | 0xC014E2 |
| A6 | `HLE_EXEC_BASE` 0xC00276 | 0xC00276 |
| INTENA | 0x602C | INTENAR 0x602C |
| DMACON | 0x02D0 (DMAEN, copper, blitter, disk) | DMACONR 0x02D0 |
| INTREQ | 0x1000 (DSKSYNC pending) | INTREQR 0x1000 |
| PC | the boot buffer 0x001558 plus the 12-byte header | 0x001564 |

The bootblock's first 1 024 bytes are copied from DF0's image to 0x001558 (as a trackdisk read,
logged as load 1 and counted as read 1), and the return address a JSR from Kickstart would
have pushed is on the stack, so the bootblock's final return enters HLE space. **No other
register is set**: D0-D7, A0, A2-A5 stay zero, although the measured state had values in
several of them that the bootblock does not read. ExecBase's scalar fields, the MemHeaders'
bounds, the boot task and the copper start-up list are the measured ones (same section of
`loader.md`); the free lists are discussed under *Findings*.

`boot_return` (`core/boot.c`) is the bootblock's return, and replays the strap's effect.
Evidence: `../proof/doc.format/loader.md`, "Machine state at stage 2's entry" (measured
2026-10-03, WinUAE 6.0.3).

- **D0 must be 0**; any other value is fatal (`bootblock returned d0 = 0x...`).
- The boot buffer 0x001558, 0x488 bytes, is freed through the HLE allocator (`hle_free`), as
  the strap did; the measured chip free list shows it as the first chunk.
- DMACON gets BPLEN (0x0100) set: 0x02D0 becomes 0x03D0, as measured.
- The PC becomes A0 (stage 2's entry, which the bootblock leaves there) and A6 becomes
  ExecBase. Nothing else of the strap's register or stack handover is imitated.
- CIA state at stage 2's entry is set to the measured values. CIA-A: interrupt mask 0x0A
  (timer B, serial port), CRB 0x08, timer A latch and counter 0xFFFF, timer B 0x02CB, DDRA
  0x03, PRA 0xC0, SDR 0x3E, time of day 0x7A. CIA-B: mask 0x04 (TOD alarm), ICR 0x10 pending,
  CRB 0x80, timer A and B 0xFFFF, PRA and PRB 0xFF, DDRA 0xC0, DDRB 0xFF, time of day 0x3E.
  The measured CRA of both is 0, which is what the HLE already holds.

## Public helpers for hacks

What `docs/game.md`, item 3, refers to. Game code uses only these; the declarations are in
`core/amiga.h` and `core/cpu_ops.h`.

- `mem_read8` / `mem_read16` / `mem_read32 (m, addr)`: read guest memory through the real bus.
  RAM is read directly; the custom registers and the CIAs catch the chipset up to the current
  instruction first. Addresses are 24 bits; chip RAM mirrors fold onto 512 KB. A word or
  longword access at an odd address is logged once.
- `mem_write8` / `mem_write16` / `mem_write32 (m, addr, v)`: the same for writes.
- `mem_ram (m, addr)`: a host pointer to the RAM byte at `addr`, or NULL if it is not RAM (in
  the custom or CIA area, or unmapped). The memory is big-endian.
- `amiga_now (m)`: the current time in CPU cycles, counting from the start; while an instruction
  runs it includes that instruction's cost (the time its I/O happens).
- `CPU_CYC (op)`: the cycle cost of opcode word `op` from Musashi's 68000 table, as `uint64_t`.
- `cpu_push16` / `cpu_push32 (m, v)`: push a word or longword on A7 (decrement, then write).
- `cpu_pop16` / `cpu_pop32 (m)`: pop and return a word or longword from A7 (read, then
  increment).

## Speed

Observed 2026-10-06 on an Intel i7-7700K, Release (-O3) build, headless run of the first game's
`earth-base.input` (`games/<id>/scripts/`, see
[`games/deuteros/docs/format.md`](../games/deuteros/docs/format.md)): 930 frames in 1.86-1.88 s
wall, 9.9-10.0 times real time. Speed = frames x 0.0200319 s / wall, where 0.0200319 s is one
PAL frame of 142 102 cycles at 7 093 790 Hz. Three runs: 10.02, 9.91, 10.02. For comparison,
the proof's Musashi-only Release build ran the same script at 8.96x
(`../proof/doc.agent/recomp.md`, "Interpreter speed", measured 2026-10-06).

## Findings

1. **The slow-RAM free list at stage 2's entry differs from WinUAE's, and no fix was needed.**
   At stage 2's entry WinUAE's slow-RAM free list is two chunks, 0xC04AE8 + 0x20 and 0xC04B90
   + 0x79C70 (`../proof/doc.format/loader.md`, "Machine state at stage 2's entry"). The HLE
   strap leaves the allocator's list as it was at the bootblock call, 0xC01808 + 0x10 and
   0xC04AD0 + 0x79D30 (same file, "Machine state at the bootblock call"). Why the real strap
   leaves a different list is unknown. With the list left as the HLE has it, the first game's
   menu shot still diffs EXACT (81 920 of 81 920 pixels) and its Earth-base shot diffs 81 862
   of 81 920, every difference inside x 13-23, y 79-90, which is the same as the proof's.
   No value had to be added to match.
2. **A strap-less stage-2 entry was enough.** The jump to A0 with A6 = ExecBase, bitplane DMA
   on, the measured CIA state and the boot buffer freed is all the handoff does; no
   hard-coded stage-2 register, flag or stack value was needed (*Kickstart handoff*).
