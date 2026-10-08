# Emulators — WinUAE and FS-UAE as the oracle

WinUAE is this project's reference machine, and FS-UAE an equal one (decision 016). Verification #1 in `docs/conventions.md` (a pixel
diff) compares a headless runtime shot against a WinUAE screenshot, and every machine-state
measurement in the docs is a RAM dump taken there. Three tools serve it: `tools/emulator.py`
generates the configuration, `tools/capture.py` drives WinUAE, and `tools/pixdiff.py` compares
the shots (`docs/tools.md`). Measurements cited as "proof" with a section name come from the
predecessor project's notes (`../proof/doc.agent/emulator.md`). This project first ran WinUAE
itself on 2026-10-06, for the second game; see "References".

Either emulator may write a reference (decision 016). Every section before "FS-UAE" is
WinUAE's; "FS-UAE" holds what differs there, and its measurements.

## Setup

- **WinUAE 6.0.3** (01.03.2026), installed at `C:\Program Files\WinUAE\`. It is
  Windows-only. Proof, "What the machine has", observed it on 2026-08-14; every key and
  behaviour named in this document is that version's.
- **Kickstart 1.3 rev 34.5** (A500/A1000/A2000/CDTV, 262 144 bytes, plain, not encrypted),
  MD5 `82a21c1890cae844b3df741f2762d48d`. A real ROM is required: proof, "Kickstart", booted
  the first game on it on 2026-08-14 and found that AROS failed before the first frame (an
  exec sanity-check alert). The ROM is Cloanto's property; `*.rom` is never committed.
- WinUAE does not put itself on `PATH`; tools use absolute install paths.

`tools/emulator.py` finds the ROM by content, never by filename:

- It walks every `*.rom` under each folder of `KICKSTART_DIRS` (one entry, the Amiga Forever
  default `C:\Users\Public\Documents\Amiga Files\WinUAE\Kickstarts`, with environment
  variables expanded), skips files whose size is not 262 144 bytes, and accepts the first
  whose MD5 matches.
- `--kickstart PATH` overrides the search. A file that exists but is not that ROM is still
  used, and the tool prints `[!!]  <path> is not Kickstart 1.3 rev 34.5` (before the write,
  and again after the config under `--print`).
- With no ROM found and `@KICKSTART@` in the template, it exits 2 with the folder list and
  `fix: install a Kickstart 1.3 rev 34.5 ROM or pass --kickstart PATH`.

`WINUAE_EXES` lists the executables it looks for, in order: `winuae64.exe`, then `winuae.exe`,
both under `C:\Program Files\WinUAE\`. The first that exists is printed as a PowerShell launch
line, `& "<exe>" -f "<config>"`; if none exists the config is still written and the tool says
WinUAE is not in the usual place. `capture.py` uses the same list.

The generated config is `games/<id>/build/winuae/oracle.uae` (gitignored, like all of
`build/`); `--print` writes nothing and prints the rendered config on stdout instead, headed by
that path.

    python tools/emulator.py --game <id> --df0 <disk1.adf> --df1 <disk2.adf>
    python tools/emulator.py --game <id> --df0 <disk1.adf> --df1 <disk2.adf> --print

`tools/emulator.py` was first run on Windows on 2026-10-06, for `--game lemmings`. WinUAE
loaded the result and its stdout held no `unknown` or `obsolete` line, only the two lines to
ignore listed below.

## Configuration

The settings are version-controlled in the game's template, `games/<id>/oracle.winuae.in`; the
paths are machine-specific and are filled in at generation time. **Edit the template, never the
generated copy:** the next run of `tools/emulator.py` overwrites `build/`.

Placeholders, each needing its option:

| Placeholder | Option |
| --- | --- |
| `@KICKSTART@` | `--kickstart` (default: searched by content hash) |
| `@DF0@` … `@DF3@` | `--df0` … `--df3` |

`@NAME@` that is not in the table is refused, and so is a placeholder whose option was not
given (exit 2, `<template> uses @DF1@ but --df1 was not given`), so a half-applied config is
never written. Disk paths are made absolute.

What the template has to fix, because each can move a pixel (proof, "Configuration to use";
the first game's template is `games/deuteros/oracle.winuae.in`):

- machine: A500, OCS, 68000, cycle-exact CPU, blitter and chipset, PAL at 50 Hz;
- memory: `chipmem_size=1` and `bogomem_size=2`. The units are 512 KB and 256 KB, not
  bytes, so this is 512 KB chip plus 512 KB slow;
- video: lores, `gfx_linemode=none`, `gfx_filter=null`, `gfx_filter_autoscale=none`, no
  centring, windowed 320 × 256, no fullscreen, no LEDs;
- floppy speed 100 (accurate, not turbo), so loading time is real;
- `use_gui=no`, `win32.start_not_captured=true`, `win32.inactive_pause=false`.

Keys WinUAE 6.0.3 rejects. It logs the complaint to stdout and carries on, so the config is
silently half-applied (proof, "Configuration to use", observed on 2026-08-14):

| Key | WinUAE says | Instead |
| --- | --- | --- |
| `gfx_vresolution=nodouble` | `unknown config entry` | nothing: `gfx_linemode=none` covers it |
| `gfx_correct_aspect=false` | `obsolete config entry` | omit; null filter, no autoscale: 1:1 |
| `win32.screenshot_mult_width=1` | `Unknown value ('1')` | omit; output is 1:1 without it |

Keys that are accepted but do **not** do what their name suggests:

- `win32.screenshot_width`/`_height`/`_x`/`_y` are accepted, but they do not pin the PNG size.
  See "Capture window".
- `use_debugger=true`, `-debug` and `-console` do not open the debugger (proof, "The debugger
  and RAM dumps", tried 2026-08-15, all silently inert); see "Debugger and RAM dumps".
- WinUAE logs unknown command-line parameters without complaint, so "no error" is no evidence
  that a switch did anything. After any config edit, redirect WinUAE's stdout and search it for
  `unknown` and `obsolete`.
- Two log lines to ignore: `VM: Reserve … bytes failed!` (JIT address space, irrelevant to a
  68000 config) and a failed load of `default.uae`.

Input bindings in the template: injected keys arrive on the generic keyboard device
`NULLKEYBOARD`, and the template binds them to WinUAE host events: F11 screenshot, F8 save
state, F6 restore state, F5 warp. Three syntax facts (proof, "Screenshots for pixel diffs",
verified 2026-08-15): the button token is the decimal scancode followed by the key name
(`87.F11`, 87 being `0x57`); `input.config=1` is required because config 0 is read-only; and
the device must be `NULLKEYBOARD`, since a real keyboard's HID path only works on the machine
that saved it. No binding is needed for the Amiga to receive an injected key, see "Input
boundary".

## Capture window

A WinUAE screenshot is a measurement instrument, not a picture. It is useful for a diff only if
it is **lossless (PNG), unscaled, unfiltered, without aspect correction, and exactly 320 × 256**
(proof, "Screenshots for pixel diffs"). Under those conditions each pixel is a native lores
pixel and every colour is a 12-bit OCS colour expanded by nibble replication
(`#RRGGBB` with each channel `n × 17`); `tools/pixdiff.py --colours` flags any colour that is
not. A scaled or filtered capture diffs against everything and tells nothing.

What was observed (proof, same section; WinUAE 6.0.3, 2026-08-14 and 2026-08-17):

- The PNG is written as 24-bit RGB (not palette indexed), named after the DF0 image plus a
  three-digit counter. The folder is `ScreenshotPath` under
  `HKCU\Software\Arabuusimiehet\WinUAE`.
- WinUAE captures the window's **client area**, so the PNG follows the window. With the
  template's settings the client area is **320 × 279**: 320 × 256 of emulation plus a status
  bar of 23 pixels (status bar text `313p` for PAL). A window resized to 640 × 535 wrote a
  624 × 473 PNG, and 960 × 791 wrote 944 × 729. A resized window therefore silently invalidates
  a diff: leave the window alone during a measured run.
- The registry flag `Screenshot_Original` (a DWORD under the same key) makes the PNG 320 × 256
  at every window size, but it also moves the picture: the first game's first screen
  was displaced 28 pixels right and lost 37 columns on the right instead of 9. It was set,
  measured and removed. **Do not set it.**
- The counter stops at 999: once `_999` exists WinUAE writes nothing and says nothing. See
  "Input boundary" for what `capture.py` does about it.
- The raw PrintScreen key is taken by Windows for the Snipping Tool by default, which steals
  the foreground, so `capture.py` sends F11, bound to the same event by the template.
- Taking a screenshot without a keypress, through the debugger's `I AKS_SCREENSHOT_FILE`, does
  not work in 6.0.3: four spellings were tried, no file was written, and no error was shown.
- WinUAE writes a screenshot on the next frame it renders and a halted machine renders none, so
  a shot must be taken while the machine runs. A shot and the dump beside it are therefore one
  halt apart, not one frame (proof, "Driving it without a keyboard", 2026-09-06).

Where the capture sits in the Amiga's raster is a measurement, and it is the source of the two
constants in `core/amiga.h`:

| Constant | Value | Meaning |
| --- | --- | --- |
| `AMIGA_FRAME_W` × `AMIGA_FRAME_H` | 320 × 256 | the capture window |
| `AMIGA_FRAME_LINE0` | 42 | the first row of a screenshot is scanline 42 |
| `AMIGA_FRAME_DIW_X0` | 120 | the first column is display-window x 120, in lores pixels |

Evidence: the first game's 320 × 200 first screen sits at dx 9, dy 14 inside a normal 320 × 256
capture, which puts the capture's first row at scanline 42 (`../proof/doc.format/screen.md`,
"Where it sits in a 320×256 capture"; the game's own values are in
`games/deuteros/docs/format.md`). The proof states the row (42) as a measurement. The column
120 is an **inference**: the same dx 9 subtracted from the game's display-window start, which
the proof does not state as a constant. Because the game's playfield is wider than the capture,
**the capture clips the rightmost 9 pixels** of it. No other game has been measured, and the
constants hold only while the window geometry above does.

A frame row (`m->frame`) runs wider than the capture, `AMIGA_FRAME_STRIDE` = 335 columns to the last one
Denise draws (DIW 454), so a game's `view` may reach past the capture's right edge. Columns 0–319 are the
capture; screenshots, `until colours` and oracle diffs use only those.

## Debugger and RAM dumps

WinUAE's console debugger is how `capture.py` halts a screen, reads registers and saves memory
(proof, "The debugger and RAM dumps"; all observed on WinUAE 6.0.3 in the predecessor's runs of
2026-08-15 and later).

- It opens with **Shift+F12**, which has to reach the emulation window (class `PCsuxRox`) as
  scancode input; the debugger is then a console window (`ConsoleWindowClass`). Emulation
  halts the instant it appears. A dropped press looks like a later `cannot attach to WinUAE's
  console`, so `capture.py` re-sends every 2.5 s until `-conlogfile` grows, which is the
  acknowledgement.
- `capture.py` launches WinUAE with `-conlogfile games/<id>/build/winuae/condebug.log`; every
  reply the debugger prints lands in that file, which the tool reads back. The file exists only
  once the debugger has been opened.
- Commands go to the console's input buffer with `WriteConsoleInputW` after `AttachConsole`; no
  focus and no scancodes. A command sent while the machine runs is queued until the next halt.
- Commands the tools rely on: `S <file> <addr> <n>` (write memory to a file; addresses and
  lengths are hex without prefix; the path is unquoted, so **no spaces**, and it must be
  absolute because WinUAE resolves a relative one against its own working directory), `g`
  and `x` (resume), `e` (dump the custom registers), `r`, `I <custom event>` (for example
  `I floppy0=<path>` inserts a disk into DF0). `h` lists the rest.
- `S` writes raw bytes with no header: offset n is Amiga address base + n. Chip RAM is 512 KB at
  `0x000000`, so `S <file> 0 80000` captures all of it. WinUAE's own receipt is
  `Wrote 00000000 - 0007FFFF (524288 bytes) to '…'`; `capture.py` waits for `Wrote ` and
  checks the file exists. A dump was verified byte-identical to the ADF over a region the
  first game's loader had just read (proof, 2026-08-15).

Options (`docs/tools.md`, "`capture.py`"):

- `--dumps N` runs N cycles. Each cycle: take a screenshot while the machine runs, halt, dump
  with `S`, resume, sleep `--interval` seconds (default 1.0). Colours on screen are printed
  with each dump.
- `--out PREFIX` names the dumps `PREFIX-NN.bin`; default
  `games/<id>/reference/ramdump/capture`, so files land in `games/<id>/reference/ramdump/`
  (gitignored: they are game data).
- `--addr` and `--length` (decimal or `0x` hex; defaults `0` and `0x80000`) are the region.
- `--cmd "…"` (repeatable) and `--script FILE` send debugger commands after the dumps; a
  script's `{out}` is the dump folder. Commands print what the log answers.

A `g` clears the session's idea that the machine is halted, and a `Memwatch` or `Breakpoint`
line in the log sets it again, so a watchpoint that fires on its own does not desynchronise the
tool. Silent logs mean the breakpoint has not fired yet, not that the command failed.

Not available: opening the debugger without a keypress (`-debug`, `-console` and
`use_debugger=true` do nothing; no remote option was found in WinUAE's `Readme.txt`, searched
2026-08-15). The predecessor saw `fa` (find effective address) fail on 6.0.3 and a `fl`
listing omit watchpoints; both are in the proof's command tables.

## Input boundary

What can be delivered to the machine, as `capture.py` implements it. Everything here is
Windows-only: input goes in with Win32 `SendInput` and real scancodes, because WinUAE reads
RawInput, so a zero-scancode `keybd_event` does nothing.

Delivered:

- **Host events** (screenshot F11, state save F8, state restore F6, warp F5, debugger
  Shift+F12): proven in the predecessor's runs (proof, "The input boundary", 2026-08-16 and
  re-measured 2026-09-24).
- **Keys the game sees**: `--key NAME` and `!key NAME [HOLD]` send one chord, press then
  release, from this set: `1 2 3 space return esc shift f7 f11 f12 end prtsc`; HOLD is the
  seconds between them, default 0. The machine is resumed first, as a key into a halted
  emulation goes nowhere. An injected `1` crossed the first game's language menu (proof,
  2026-09-24).
- **The left mouse button, through a key**: `games/lemmings/oracle.winuae.in` binds F7 to
  `JOY1_FIRE_BUTTON` (port 0 fire, which is the mouse's left button), because an injected click
  reaches the Amiga only while WinUAE holds the mouse capture. `!key f7 0.2` crossed Lemmings'
  cracktro in five runs out of five on 2026-10-06; `!key f7` with no hold was missed in one
  run out of two. Do not use F10: Windows takes it into menu-bar mode, the window shrinks and
  WinUAE writes no more screenshots (observed 2026-10-06).
- **A left click where the pointer already is** (`!click`, `!press`): see the next list.

What it cannot do:

- **Aim.** The predecessor tool had closed-loop pointer aiming (`!aim`, `!click x y`). They
  were dropped from this tool because their thresholds and gain were tuned to one game's pointer
  (an injected delta moved it about 0.83 pixels across and 1.9 down; proof, "The input
  boundary", 2026-09-24). A later game that needs pointer aiming brings it back with per-game
  parameters. Until then `!click` and `!press` act on the pointer's current position only, and
  nothing moves the pointer.
- Run from a shell with no interactive desktop: `GetForegroundWindow()` returns 0 and every
  `SendInput` fails with `ERROR_ACCESS_DENIED` (proof, 2026-08-15).
- Use the debugger's `I` to press keys: six spellings produced no Amiga key and wrote no file.

Two host conditions decide whether injected input arrives at all (proof, 2026-09-24):

1. The **input desktop must be `Default`**. A running screensaver switches it, and every
   `SendInput` then delivers 0 events with `ERROR_ACCESS_DENIED`, indistinguishable from an
   emulator that ignores injected input. With a secure resume the desktop becomes `Winlogon`
   and only a person can repair it. `capture.py` therefore holds the screensaver off for the run
   (setting `ScreenSaveActive`, the registry value, the execution state, plus a zero-distance
   mouse move every 30 s) and restores it after; it kills a running `.scr` process if the
   desktop is not `Default`, and exits if it still is not.
2. The **emulation window must hold the foreground**; `capture.py` activates it before every
   injection. Using the machine during a run still sends keys to whatever is focused.

Two more behaviours worth knowing: `capture.py` moves old screenshots into
`before-<timestamp>/` beside them when fewer than 200 counter numbers remain below 999; and it
re-sends F11 every 3 s until a PNG appears, because WinUAE drops an injected key every few
dozen presses.

**Observed here, 2026-10-06** (Windows, interactive desktop, `--game lemmings`): `--script`
runs with `!sleep`, `!key`, `!until` and `!shot`. F11 screenshots arrived every time, and the
PNGs are 320 × 256 with only 12-bit colours. The debugger, dumps, `!click` and `!save` were not
exercised. A `!click` sent while WinUAE did not hold the mouse capture did not reach the Amiga.
On other systems `main()` exits 2 with a message (`--help` works everywhere).

## Save states

A WinUAE `.uss` state embeds the machine's RAM, so it is game data: `*.uss` is gitignored and
belongs under `games/<id>/reference/`. A state reaches a screen far faster than a scripted cold
boot (the predecessor measured 1.6 s against about 40 s on the first game; proof, "Save
states", 2026-08-16).

Mechanics (proof, same section, measured 2026-08-16):

- **Load at launch:** `capture.py --statefile PATH` adds `-statefile=PATH` to the WinUAE
  command line.
- **Save:** F8 is bound to `SPC_STATESAVE`, with no dialog. It writes back to **the file
  `-statefile=` named**, destroying the state the run started from. Hence the protocol: copy
  the state you want to a disposable file, launch with `--statefile` on the copy, save at the
  moment of interest, then copy the file out under its real name.
- `!save PATH` in a script (and `--save PATH` at the end of a run) does the copy-out: it sends
  F8 until the state file's modification time moves, waits 0.5 s, copies it to `PATH` and prints
  the size. It refuses to run without `--statefile`, since there is nothing for F8 to write
  back to, and fails with an error if F8 never writes within 12 s.
- F6 is bound to `SPC_STATERESTORE` (restore in place); `capture.py` has no directive for it.

A state carries no resolution: one banked from a resized window reloads to an ordinary
320 × 256 screen (proof, 2026-08-17).

## References

A reference shot is a WinUAE or FS-UAE screenshot kept for diffing (decision 016); the platform
that took it goes in the game's PLAN *Observed:*. They live in
`games/<id>/reference/` (gitignored: they are game data, and no screenshot is ever committed).

The first game's set (`menu-nosprite.png`, `earth-base-nosprite.png`, `title.png`,
`pre-title-06.png`, `pre-title-14.png`) was **copied on 2026-10-06 from
`../proof/build/oracle/`**. No WinUAE run happened for this project to produce it; its
provenance is the predecessor's run, and its names say what each is (inference: the `-nosprite`
pair was taken with sprites disabled, as the proof's `Ms 0` command does). What each shows is
owned by `games/deuteros/docs/format.md`.

The second game's set was taken by this project, with `scripts/boot.dbg` on the
`[cr SR]` pair named in `games/lemmings/docs/dumps.md`, on 2026-10-06:

| File | Colours | What | Reproducibility |
| --- | --- | --- | --- |
| `expansion-ram.png` | 8 | the game's "Expansion RAM detected & utilised" screen, shown for about 1.5 s | one shot, from a run that took a screenshot every 0.5 s; too short for `!until` |
| `psygnosis.png` | 32 | the Psygnosis owl, still for about 7 s | 81 920 / 81 920 across three runs |
| `dma-design.png` | 14 | the DMA Design logo, still for about 15 s | 81 920 / 81 920 across three runs |
| `title-menu.png` | 30 | the title menu; it animates | 81 905 / 81 920 across two runs, every difference inside x 35–46, y 134–138 |

To take a new one:

    python tools/capture.py --game <id> --until N --name NAME

polls screenshots until one has N distinct colours and keeps it as
`games/<id>/reference/NAME.png`, default name `until-N`. In a script, `!shot NAME` keeps the
next screenshot under that name (without NAME, WinUAE's own file name). The colour count is the
state signal: **poll for the screen, never sleep a fixed number of seconds**, because a cold
boot varies (proof, "Halting and capturing": one early run slept 55 s and measured the same
screen twice). `--until` waits at most 180 s per call and fails with the colour counts it saw.

Animated screens differ between shots, so a golden image must be a screen that holds still
(proof, "How far the boot gets": the first game's title animates).

Compare with `tools/pixdiff.py` (`docs/tools.md`).

## FS-UAE

FS-UAE 3.1.66 runs WinUAE's 3.3.0 core (`debug.uae` records `config_version=3.3.0`). Everything
below was observed on **Linux (GNOME on Wayland), FS-UAE 3.1.66 as the Flatpak
`net.fsuae.FS-UAE`, on 2026-10-08, with `--game lemmings`**, unless marked `[source]`: read from
FS-UAE's v3.1.66 source and not exercised here.

### Setup

- **Binary:** `tools/emulator.py` (`fsuae_command()`) uses `fs-uae` on PATH, else
  `flatpak run --nosocket=wayland --env=SDL_VIDEODRIVER=x11 net.fsuae.FS-UAE`. The Flatpak
  mounts the home folder, so a path under `~` means the same thing inside it; `/tmp` does not.
  The working directory is kept, which `capture.py` relies on.
- **Flatpak sockets:** with the Flatseal overrides `wayland` and `fallback-x11` set, FS-UAE
  stopped at `SDL_GetCurrentDisplayMode failed` on every X server. `--nosocket=wayland` on the
  command line restores it; adding `--nosocket=fallback-x11` breaks it again.
- **Kickstart:** found by content hash in `~/Documents/WinUAE/Kickstarts` and
  `~/Documents/FS-UAE/Kickstarts`. Here the ROM is in the WinUAE folder, and FS-UAE's own
  Kickstarts folder is empty.
- **A private X server.** `capture.py` starts `Xvfb` (`-displayfd`, 1280 × 1024) and runs FS-UAE
  on it, so no window opens and the user's keyboard and mouse stay free. The desktop's own
  server is Xwayland, and XTEST keys never arrived there: neither `tools/x11.py` nor `xdotool`
  delivered one to an `xev` window holding the X focus. On Xvfb they arrive.
- **Window and quit under Flatpak:** `_NET_WM_PID` is the PID inside the sandbox (2), so the
  window is found by its title, `FS-UAE · Amiga 500`. Ending `flatpak run` does not end the
  sandbox: `capture.py` quits FS-UAE with its `End` key (`action_quit`), then `flatpak kill`.
  To quit a hand-started FS-UAE: `End`, or `flatpak kill net.fsuae.FS-UAE`.
- The log says `No HW OpenGL driver: llvmpipe`; the screenshots do not depend on it.

### Configuration

`games/<id>/oracle.fsuae.in` is the template, generated into `build/fsuae/oracle.fs-uae` with
`@BUILD@` (the absolute `build/fsuae/`) filled in besides the usual placeholders.
`emulator.py` also creates `base/`, `logs/`, `states/` and `shots/` there. The template's own
comments must not contain `@NAME@`: `render()` replaces every one it finds, comments included.

- A key `uae_<k> = v` is handed to the core's option parser after FS-UAE's own model setup, so
  the machine block overrides `amiga_model` `[source: src/fs-uae/uae_config.c]`. A key the core
  rejects is logged as `failed to set option "<k>" to "<v>"`.
- The effective config is `logs/debug.uae` and the log is `logs/fs-uae.log.txt`, both under
  `logs_dir`.
- **Observed:** 0 lines `failed to set option`; of 23 machine keys (CPU, chipset, memory,
  floppies, display), compared between `oracle.winuae.in` and `debug.uae`, all 23 are the same.
  No key was rejected, so there is no table of them.
- Isolation: `end_config = 1`, `joystick_port_1 = none`, and `base_dir`, `logs_dir` and
  `state_dir` under `build/fsuae/`, so the user's `~/Documents/FS-UAE` files do not apply.
- `log_input = 1` (appended by hand to the generated file) logs each host key and the input
  event it became.

### Capture window

FS-UAE writes screenshots from its frame buffer, never from the window.

- `theme_zoom = X,Y,640,512` with lores and no line doubling gives a **320 × 256** PNG: the crop
  is the zoom rectangle halved `[source: src/fs-uae/video.c]`. The estimate `56,32` was
  **right the first time**: the psygnosis shot was `EXACT` against WinUAE's with no offset.
- The mask `screenshots_output_mask = 2` writes only the crop, as
  `shots/oracle-crop-<yymmddHHMM>-<NN>.png`. `NN` restarts each minute; no counter limit was
  met. The folder must exist `[source]`, so `emulator.py` makes it, and `capture.py` empties it
  at launch so `newest()` sees only this run.
- The PNG is written from another thread, so a file counts once its size is non-zero and the
  same 0.1 s later.

Measured against the WinUAE 6.0.3 references of 2026-10-06 (`pixdiff`, no offset):

| Shot | Result |
| --- | --- |
| `psygnosis` | 81 920 / 81 920, `EXACT` |
| `dma-design` | 81 920 / 81 920, `EXACT` |
| `title-menu` | 81 920 / 81 920, `EXACT` (WinUAE's own two runs differed inside x 35–46, y 134–138) |

### Debugger and RAM dumps

`console_debugger = 1` lets `Pause` (`action_enter_debugger`) open the UAE debugger on FS-UAE's
own stdin and stdout. `capture.py` gives it a pty with echo off, drains the pty into
`build/fsuae/console.log`, and writes commands to it.

- The prompt is `>`. `halt()` takes a log that ends in `>` as the acknowledgement; the log
  growing is not one, because FS-UAE prints other lines. `x` resumes.
- **A line is 99 characters.** A longer one is cut and the rest runs as a second command:
  `S` with a 100-character line wrote `0x8000` bytes and left a `>>` prompt, and one of 110
  answered `S-command needs more arguments!`. `FsHost.send` refuses a line over 99, and dump
  paths (`{out}`, `Session.dump`) are made **relative to the repository root**, where FS-UAE
  starts.
- A quoted path fails (`Couldn't open file '"…"'`). A relative path resolves against the working
  directory, and `S` does not create folders (`capture.py` creates the dump folder).
- `S games/lemmings/reference/ramdump/<name>.bin 0 80000` answered
  `Wrote 00000000 - 00080000 (524288 bytes) to '<path>'` and the file was 524 288 bytes.
- Resuming worked: after `S` and a save, the run reached the next screen (14 colours).

### Input boundary

Input is X11 XTEST on the private server (`tools/x11.py`), so it needs no focus fight and no
screensaver handling.

- **Host keys:** F11 shot, F8 save, F6 restore, F5 warp, F7 the Amiga's left button, `Pause`
  debugger, `End` quit. F12 alone opens FS-UAE's menu; F11 and F12 act only on release and never
  reach the Amiga `[source: libfsemu/src/emu/input.c]`.
- **Keys the game sees:** `space`, `return` and `1` each logged `key_code … = press … => input
  event`, none `NO INPUT EVENT`. `!key f7 0.2` crossed the cracktro in every run.
- **Clicks:** `!click` crossed the Skid Row cracktro (it waits for the button) in 3 runs of 3.
- **Pointer motion:** `!move DX DY`, in steps of 8 so FS-UAE's per-event clamp never bites.
  On the title menu `!move 40 0` changed 212 pixels, all in rows 2–13, x 9–57 (two 16-pixel
  pointer positions), and `!move -40 0` returned rows 0–127 to identical. The bottom scroller
  (y ≥ 190) keeps changing, so shots seconds apart differ there whatever the pointer did.
- Left Alt releases the input grab `[source]`; not exercised, since nothing holds a grab on the
  private server.

### Save states

F8 and F6 are `action_save_state_1` and `action_restore_state_1`, on the file
`build/fsuae/states/Saved State 1.uss`. FS-UAE answers on stdout
`savestate_quick slot=1 save=1` and `save=0`.

- A state of the Psygnosis screen is about 67 KB. `!save PATH` and `--save` copy it out.
- `--statefile PATH` copies the file into the slot and presses F6. **F6 right after the window
  appears is ignored**, so it is re-sent every 3 s until `save=0` appears. The restored shot
  was `EXACT` against `psygnosis.png` and the whole run took 5.8 s.
- Unlike WinUAE, F8 never overwrites the state the run started from: it writes the slot, and
  `--save` needs no `--statefile`.

### Deuteros

`games/deuteros/oracle.fsuae.in` is Lemmings' template with the floppy block of
`oracle.winuae.in` and no F7 button. Observed 2026-10-08, Linux, FS-UAE 3.1.66, with
`games/deuteros/scripts/boot.dbg`:

- A cold boot shows the title (16 colours), which waits for a click, then the language menu
  (4 colours), which waits for a key. `--until 4` alone stays on the title for 180 s.
- **Disk swap:** WinUAE's debugger command `I floppy0=<path>` does nothing in FS-UAE: it printed
  a disassembly line and no disk changed. The template lists both images
  (`floppy_image_0`, `floppy_image_1`) and binds F9 to `action_drive_0_insert_floppy_1`, which
  inserts Disk 2 into DF0 (`insert floppy (1) into drive (0)` in the log). The key after it
  needs 3 s; with 1 s the game stayed on the prompt.
- The language menu differs from the WinUAE `menu-nosprite.png` (sprites off) in 65 pixels, all
  inside x 164–176, y 116–127: the pointer sprite, which the reference leaves out. The Earth
  base differs from `earth-base-nosprite.png` in 112 pixels inside x 13–176, y 79–127: the
  same pointer, and the x 13–25, y 79–91 animation box WinUAE's own runs show
  (`games/deuteros/docs/format.md`, "Verification figures").
