# Tools

Every tool is one Python 3.11+ file in `tools/`, standard library only, with `--help`. Run each
from the `amigarecomp/` root. `CLAUDE.md` owns the roster; `docs/conventions.md` owns the
rules a tool follows.

## `doctor.py`

    python tools/doctor.py

Read-only. It reports:

- **Toolchain:** Python ≥ 3.11, `cmake` ≥ 3.24 (the version `docs/game.md` item 2 requires),
  `ninja`, `git`, a C compiler (`gcc`, `cc` or `clang`) and a C++ compiler (`g++`, `c++` or
  `clang++`).
- **Git hygiene**, when `amigarecomp/` is a git work tree: `git check-ignore` over every
  `games/*/disks/`, `games/*/reference/` and `games/*/build/`, and over the root `build/`. A
  path that git can see is printed as `[!!]  <path> is visible to git`. Outside a work tree it
  prints `[!!]  not a git repository`, which does not change the exit status.

Marks: `[ok]` passes, `[--]` is missing, `[!!]` is wrong. The exit status is 1 when any check
is `[--]` or `[!!]` (except "not a git repository"), otherwise 0.

## `pixdiff.py`

    python tools/pixdiff.py --game <id> a.png b.png
    python tools/pixdiff.py --game <id> a.png b.png --search
    python tools/pixdiff.py --game <id> shot.png --colours

Compares two images pixel for pixel: the project's verification #1 (`docs/conventions.md`). It
reads any non-interlaced PNG of 8 bits per channel or less (a 16-bit channel keeps its high
byte; alpha is dropped) and uses the standard library only. The capture it expects is described
in `docs/emulator.md`, "Capture window".

| Option | Meaning |
| --- | --- |
| `--game ID` | required; the folder under `games/` (exit 2 if it is missing) |
| `images` | two PNGs to compare, or one with `--colours` |
| `--colours` | list the distinct colours of each image and stop |
| `--search` | try every alignment within 64 rows and 16 columns, report the best |
| `--dx N`, `--dy N` | compare a(x, y) with b(x + dx, y + dy); default 0 |
| `--diff PATH` | where to write the difference PNG |
| `--no-diff` | do not write one |

Output of a comparison: the two names, a note when the sizes differ (only the overlap is
compared), `<agree> of <total> pixels agree = <share> %` with the offset used when it is not
zero, and then `EXACT` when every pixel agrees. Otherwise it lists the commonest colour
pairs (`#RRGGBB where the other has #RRGGBB`), the worst rows and a bounding box,
`differences inside x a–b, y c–d`. `--search` ranks alignments by pixels where at least one
image is not its own background colour (the commonest), because a mostly-background screen
agrees above 99 % at wrong alignments.

`--colours` prints each image's size and colour count, then every colour with its pixel count
and its 12-bit `$RGB` value, flagging `** NOT a 12-bit colour **` when a channel is not a
multiple of 17, which marks a scaled or filtered capture.

The difference image shows agreeing pixels at a quarter brightness and disagreeing ones in flat
magenta. It defaults to `games/<id>/build/pixdiff/diff.png` (gitignored); a `--diff` path
outside the root is printed as given.

Exit status: 0 only if there are compared pixels and every one agrees; 1 for any difference or
an unreadable PNG (`cannot read -- …`); 2 for an unknown game. `--colours` exits 0. `capture.py`
imports `read_png` from this file.

## `emulator.py`

    python tools/emulator.py --game <id> --df0 <disk1.adf> --df1 <disk2.adf>
    python tools/emulator.py --game <id> --df0 <disk1.adf> --df1 <disk2.adf> --print

Generates the WinUAE configuration for one game from its template `games/<id>/oracle.uae.in`.
`docs/emulator.md` owns the setup, Kickstart identity, placeholder rules and the reasons for
the settings.

| Option | Meaning |
| --- | --- |
| `--game ID` | required; the game folder under `games/` |
| `--df0` … `--df3` | floppy image for `@DF0@` … `@DF3@`, made absolute |
| `--kickstart PATH` | Kickstart 1.3 ROM for `@KICKSTART@`; default: found by content hash |
| `--print` | write nothing and print the rendered config instead |

Output: `games/<id>/build/winuae/oracle.uae`, then `wrote     <path>` and a PowerShell launch
line when WinUAE is installed in the usual place. With `--print` it prints
`# ---- games/<id>/build/winuae/oracle.uae ----` and the text. Exit codes: 0 on success; 2
with `error: …` (and a `fix:` line where there is one) for a missing game folder or template,
a missing `--kickstart` file, no Kickstart found, a placeholder the tool cannot fill, or a
placeholder whose option was not given (`<template> uses @DF1@ but --df1 was not given`).

It runs on any platform that has Python; the Kickstart search and the launch line only find
anything on the Windows paths listed in `docs/emulator.md`, "Setup".

## `capture.py`

    python tools/capture.py --game <id> --script run.dbg
    python tools/capture.py --game <id> --until N --name NAME
    python tools/capture.py --game <id> --until N --cmd e --cmd "S {out}/ram.bin 0 80000"

Drives WinUAE through the Win32 API, so it runs only on Windows: elsewhere `main()` prints a
message and exits 2 (`--help` works everywhere). It needs the generated
`games/<id>/build/winuae/oracle.uae` (run `tools/emulator.py` first) and WinUAE installed at
one of the paths in `docs/emulator.md`, "Setup". What a live run has been observed to do is in
`docs/emulator.md`, "Input boundary". `docs/emulator.md` owns how it halts WinUAE,
what input it can deliver ("Input boundary") and the reasons behind each step.

| Option | Meaning |
| --- | --- |
| `--game ID` | required; the game folder under `games/` |
| `--until N` | poll screenshots until one has N distinct colours (180 s limit per wait) |
| `--name NAME` | file name for that screenshot in `reference/` (default `until-N`) |
| `--statefile PATH` | restore this `.uss` state at launch instead of cold-booting |
| `--dumps N` | run N cycles of shot, halt, dump, resume (default 0) |
| `--interval S` | seconds of running between dumps (default 1.0) |
| `--out PREFIX` | dump prefix, `-NN.bin` appended (default `reference/ramdump/capture`) |
| `--addr A`, `--length N` | dump region, decimal or `0x` hex (defaults 0 and `0x80000`) |
| `--cmd LINE` | a debugger command run after the dumps; repeatable |
| `--script FILE` | a session file, run after `--cmd`, see below |
| `--key NAME` | press a key the game sees, first of all; repeatable; names below |
| `--settle S` | seconds to wait for the log after each command (default 0.4) |
| `--save PATH` | bank a save state here at the end; needs `--statefile` |
| `--keep` | leave WinUAE running at the end |

Key names: `1 2 3 end esc f7 f11 f12 prtsc return shift space`. The default `--out` is
relative to `games/<id>/`.

Order of a run: `--key`s, `--until`, `--dumps`, then the commands and script, then `--save`.

A script holds one item per line; empty lines and lines starting `#` are skipped. A line
starting `!` is a directive, anything else goes to the debugger and the log's answer is
printed. Every directive except `!settle` first resumes the machine.

| Directive | Effect |
| --- | --- |
| `!until N` | wait for a screenshot with N distinct colours |
| `!sleep S` | wait S seconds |
| `!shot [NAME]` | keep a screenshot as `reference/NAME.png` (WinUAE's own name if omitted) |
| `!settle S` | change the seconds waited for the log after each later command |
| `!key NAME [HOLD]` | press a key; the same set as `--key`; HOLD seconds between press and release (default 0) |
| `!click` | click where the pointer is (takes no arguments) |
| `!save PATH` | bank a save state at PATH; needs `--statefile` |
| `!press` | the same click, printed as having no acknowledgement |

Substitutions in script lines (and `--cmd` lines): `{out}` is the dump folder (the parent of
`--out`, by default `games/<id>/reference/ramdump`) and `{df0}` … `{df3}` are the floppy paths
from the `floppyN=` lines of the generated `oracle.uae`. A debugger `S` path must not contain
spaces.

Outputs: dumps under `games/<id>/reference/ramdump/`, kept screenshots in
`games/<id>/reference/`, the debugger log in `games/<id>/build/winuae/condebug.log`; all are
gitignored. Exit 0 on a completed run, 2 for an unknown game or a non-Windows system; other
failures (no WinUAE, no config, a debugger that never opened, no screenshot, a timeout) stop
with a message. Unless `--keep` is given, WinUAE is closed at the end.

## `adf.py`

    python tools/adf.py games/<id>/disks/*
    python tools/adf.py disks/

Read-only. It identifies Amiga disk images and groups copies of one dump together. A directory
argument stands for its files, non-recursively and sorted. The container rules are those of
`docs/game.md` item 5; the format is in `docs/formats/adf.md`.

It reads every `games/*/dumps.txt` (the grammar is in `docs/game.md` item 4). A line that does
not parse stops the run: `error: games/<id>/dumps.txt: cannot parse `<line>``.

Output is one block per candidate, sorted by bootblock bytes, then CRC, then source. A
901 120-byte image prints:

    <crc8>  <boot>  <sum>  <source>
        <id>: <crc> <role> <status> <name>

- `<crc8>` is the lowercase zlib CRC-32 of the image.
- `<boot>` is `DOS<n>` when bytes 0-2 are `DOS` (`n` is byte 3 in decimal), otherwise the first
  four bytes with non-printables as `\xNN`.
- `<sum>` is `sum-ok` when the bootblock checksum validates, otherwise `sum-bad`.
- `<source>` is `path`, or `path:member` for a ZIP member.
- The indented line appears once for each matching line of any game's `dumps.txt`. An image
  that matches none prints `    not in any dumps.txt: <crc> ROLE known <name>`, a line ready to
  paste into a `dumps.txt` once `ROLE` is filled in; `<name>` is the file name without folder
  and extension.

Any other size prints `--------  not an 880 KB double-density ADF (<n> bytes)  <source>`.

An unreadable path, an unsupported extension, or a bad gzip or ZIP prints
`error: <path>: <reason>` to stderr, and the other paths are still processed.

Exit status: 0 when everything was readable and every candidate was 901 120 bytes; 1 when a
path could not be read or a candidate had another size; 2 for a usage error or a `dumps.txt`
that does not parse.
