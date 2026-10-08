# Decisions

Why the project is shaped the way it is. Newest last. A decision here is not permanent, but
reversing one means writing the reversal down, with what changed.

Each entry: the decision, what else was considered, and why. The *why* is the part that is
worth anything in a year; a decision with no rationale gets re-litigated every few months.
"Author's call." marks a decision the author made, not one derived from evidence.

**What does *not* belong here:** a finding, a measurement, or a settled technical question.
Those go to the doc that owns them, with a line in `PROGRESS.md`. An entry earns its place
only if it would otherwise be re-argued, which is why the entries about paths *not* taken are
the ones kept longest.

---

## 001 — Stack: C99 + SDL3 + OpenGL 3.3 core + RmlUi, CMake, Python 3.11 tooling

**2026-10-06.**

*Why:* the proof shipped exactly this through R1–R5 on Windows (WinLibs GCC) and Linux, with
frames bit-identical across both. `core/` stays SDL-free, so its frames are deterministic and
pixel-diffable.

## 002 — The player's own disk images are read at runtime, never embedded or extracted

**2026-10-06.** Author's call.

*Considered:* the proof's build-time embedding (its decision 012). That needs a compiler on
every player's machine and leaves a binary that contains game bytes.

*Why:* everyone can download the same executable, and the only game bytes on a machine are the
ones the player brought. Images are held whole in memory and never written. Writable disks,
such as save disks, are our own files in the pref path.

## 003 — Musashi interpreter only; no static recompilation

**2026-10-06.** Author's call.

*Considered:* the proof's translator (`recomp.py`, fuzz cosim, `--cosim`), and shipping
recompiled blocks as an optional library.

*Why:* the proof measured `--interp` at **8.96–9.98× real time** at -O3 on an i7-7700K. Its
worst frame took 6.2–6.8 ms against a 20.03 ms PAL frame, and 100 % recompiled code saved only
0.5–0.6 ms a frame. A translator needs either a toolchain on the player's side or shipping
game-derived code.

*What would reverse this:* a supported game's worst frame misses 20 ms at -O3 on the target
hardware.

## 004 — Kickstart is emulated (HLE); no ROM

**2026-10-06.** Author's call.

*Considered:* a Kickstart 1.3 ROM supplied by the player, and AROS. The proof found that AROS
fails on Deuteros with "Sanity check on memory list failed".

*Why:* the player supplies one kind of file fewer. Games call a small set of OS functions and
then take over the hardware. Every unimplemented call stops loudly, naming the library, the
LVO and the caller, so the set grows by evidence. `dos.library`, with an OFS/FFS reader,
arrives when the first game needs it.

## 005 — One executable per supported game, with the game definition compiled in

**2026-10-06.** Author's call.

*Considered:* one executable that loads game packs or scripts at runtime.

*Why:* a hack is C with full access to the runtime, so no scripting VM is needed. The
executable is what a player downloads. Third-party libraries are linked statically.

## 006 — Hacks are our C code, guarded by CRC-32 anchors; value 0 is the original game

**2026-10-06.**

*Considered:* the proof's guards, which held 4–10 raw game bytes in `run.c`; and byte-pattern
search, which needs game bytes in the repo.

*Why:* an anchor `(address, length, CRC-32)` holds no game bytes and still detects code that
differs in another dump.

*What would reverse this:* a dump with relocated code that matters. In that case, add a second
hook, with its own anchor, at the relocated address.

## 007 — Unknown dumps run, with a warning

**2026-10-06.** Author's call. This reverses the proof's "loader family A only" (its decision
010).

*Why:* the proof refused those dumps because its build read offsets per loader family. Here
only the game's own loader code reads the disk, so an unknown dump of the same game can boot.
A hack whose anchor does not match stays inactive and says so.

## 008 — Accepted containers: ADF, ZIP (any number of ADFs inside), ADZ/GZ. No DMS

**2026-10-06.** Author's call.

*Why:* inflate and the ZIP reader come from miniz, one library under the MIT licence. DMS
needs a decoder derived from xDMS. That means more code and a licence review, for a format
rarer than ZIP.

## 009 — One owner per fact; other files link to it

**2026-10-06.**

*Why:* the proof learned that duplicated prose drifted within a day, and that a stale copy
gets trusted without being checked.

## 010 — WinUAE is the only oracle emulator

**2026-10-06.**

*Why:* WinUAE running a real Kickstart 1.3 is the reference every diff is taken against. A
second emulator adds nothing. AROS cannot boot Deuteros (the proof's finding), and FS-UAE runs
an older UAE core that cannot be cited.

*Reversed by 016 (2026-10-08).*

## 011 — Fidelity first; hacks are opt-in layers

**2026-10-06.**

*Why:* headless and scripted runs ignore `settings.ini` and set every hack to 0. A script has
to turn a hack on explicitly. A hack at value 0 changes nothing, so the pixel diffs keep
meaning something.

## 012 — The shared code is game-agnostic

**2026-10-06.**

No game name, address or game-observed constant appears in `core/`, `frontend/`, `cmake/` or
`tools/`. Everything game-specific lives in `games/<id>/`.

*Why:* the proof had Deuteros addresses in `run.c`, the stage-2 strap registers in `boot.c`,
and "DF1 is the save disk" in `hle_disk.c`. A second game would have forked the core.

## 013 — Each game folder is self-contained

**2026-10-06.** Author's call.

`games/<id>/` holds the game's images (`disks/`), its oracle references (`reference/`), its
definition, settings and hacks, its RmlUi assets (`menu/`), its own CMake project
(`CMakeLists.txt`) and its own build tree (`build/`).

*Considered:* one root CMake project that globs every game, and root-level `disks/<id>/`,
`reference/<id>/` and `build/<id>/`.

*Why:* a game can be configured, built, worked on and archived on its own, and the root holds
nothing that belongs to one game.

*Cost:* each game configures its own build. The fetched dependency sources are shared through
`FETCHCONTENT_BASE_DIR` at the root's `build/deps/`, so they download only once.

## 014 — Plain folder names

**2026-10-06.** Author's call.

Folders are plain lowercase words naming what they hold, plural when they hold many things of
a kind: `core`, `frontend`, `cmake`, `tools`, `tests`, `docs`, `games`, and, inside a game,
`disks`, `scripts`, `menu`, `reference`, `build`, `docs`.

*Considered:* the proof's `role.qualifier` names, such as `backend.core` and `doc.agent`.

*Why:* they added a naming rule to learn and bought nothing that a short, flat layout does not
already give.

## 015 — The player chooses each drive's disk

**2026-10-07.** Author's call.

The Disks page assigns disks to DF0–DF(n-1) after boot. A disk taken from another drive swaps
with the chosen drive's disk, and Empty ejects. F11 and `df0_cycle` are removed.

*Why:* a fixed layout cannot follow a game that wants its data disk in DF0, and a single-drive
game needs a way to change disks.

## 016 — WinUAE and FS-UAE are both oracle emulators

**2026-10-08.** Author's call.

Either emulator may write `games/<id>/reference/<name>.png`. The platform that took a shot is
recorded in the game's PLAN *Observed:*.

*What changed:* the Linux machine has FS-UAE 3.1.66 (a Flatpak) and no WinUAE. FS-UAE runs
WinUAE's 3.3.0 core, and its shots matched WinUAE 6.0.3's Lemmings references on every pixel:
`psygnosis`, `dma-design` and `title-menu` all 81 920 / 81 920 (2026-10-08, Linux, FS-UAE
3.1.66). `docs/emulator.md`, "FS-UAE", has the measurements.

*Considered:* WinUAE only (010); FS-UAE shots in `reference/fsuae/`; FS-UAE for exploration
only.

*Why:* one `reference/` keeps every diff one-to-one, and the match is the evidence that either
emulator's shot is the same measurement.

*What would reverse this:* an FS-UAE shot that differs from WinUAE's of the same screen.
