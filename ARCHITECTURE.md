# Architecture

> Start at `CLAUDE.md`. What happens next is in each game's `games/<id>/PLAN.md`, how to start
> a game is in `PLAN.md`, what already happened is in `PROGRESS.md`, and the reasons are in
> `DECISIONS.md`.

Folders are plain lowercase words naming what they hold (decision 014). Folders are named here
before they exist, so each name is decided once; a folder appears on disk when a milestone
puts a file in it.

Shared code sits at the root. Each game is the self-contained folder `games/<id>/` (decision
013); `docs/game.md` owns its layout.

## Exists today

- `docs/` — agent deep-dives indexed by `CLAUDE.md`: `conventions.md`, `game.md`, `tools.md`,
  `runtime.md` and `emulator.md`.
- `cmake/` — `amigarecomp.cmake`.
- `core/` — the A500/OCS runtime, complete as of M2 except the hook, hack and settings parts of
  `game.h`, which M4 adds.
- `frontend/` — window, GL quad, audio and input; the F1 menu, disk-image loading and settings
  come later.
- `tools/` — `doctor.py`, `emulator.py`, `capture.py` and `pixdiff.py`.
- `games/deuteros/` — the first game.
- `games/lemmings/` — the second game; only its WinUAE oracle so far (`oracle.uae.in`,
  `scripts/boot.dbg`, `docs/dumps.md`, `README.md`, `PLAN.md`).

## Local only, gitignored

- `games/<id>/disks/` — the developer's own copies of that game's images, kept as they arrived.
- `games/<id>/reference/` — WinUAE save states, RAM dumps and screenshots for that game. All of
  it is game data.
- `games/<id>/build/` — that game's CMake trees (`cmake/` on Windows, `cmake-linux/` on Linux),
  plus its tool output, screenshots and diff images.
- `build/` (root) — `deps/`, the FetchContent sources that every game's build shares, plus
  output from tools that are not tied to one game.

## Created by a milestone

Each entry names its milestone in `games/deuteros/PLAN.md`.

- `cmake/` (M1) — shared CMake modules. It is not a project of its own.
    - `amigarecomp.cmake` covers the dependencies, the core and frontend libraries, the
      `amiga_add_game()` function and the `tests/` targets.
    - `dumps.cmake` (M3) and `embed.cmake` (M4) come later.
    - Every game's `CMakeLists.txt` includes `amigarecomp.cmake`.
- `frontend/` (M1) — window, GL quad, audio and input. Later milestones add the RmlUi F1 menu,
  whose shared documents live in `frontend/menu/` (M4), disk-image loading (M3) and settings
  persistence (M4). Game-agnostic.
- `core/` (M2) — the A500/OCS runtime: bus, scheduler, copper, Denise, blitter, Paula, the
  CIAs, Kickstart HLE, Musashi glue, input scripts, save states, and the game contract
  `game.h` (M4). It is C99 with no SDL, no OS headers, no file I/O and no `malloc`.
  Game-agnostic.
- `games/<id>/` (M1, starting with `games/deuteros`) — one supported game, self-contained
  (decision 013). `docs/game.md` owns its layout.
- `tools/` (M1) — Python tools, one job each, shared by every game. `CLAUDE.md` owns the
  roster.
- `tests/` (M3) — `ctest` unit tests over synthetic images and memory. They never use game
  bytes. Every game's build runs them.
- `docs/formats/` (M3) — generic Amiga formats. `adf.md` covers ADF geometry, the bootblock and
  its checksum, the OFS/FFS root, and the ZIP/ADZ/GZ containers. A game's own formats live in
  that game's `docs/`.
- `docs/tools.md` (M1), `docs/emulator.md` (M2) and `docs/runtime.md` (M2) — created along with
  the first tool, the oracle and the runtime respectively.
