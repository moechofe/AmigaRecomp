# CLAUDE.md

AmigaRecomp — Amiga games natively, one executable per game, from the player's own disks.

C99 + SDL3 + OpenGL 3.3 core + RmlUi, built with CMake, tooled in Python 3.11. Each game runs
under the Musashi 68k interpreter on a game-agnostic OCS runtime with Kickstart HLE. The
project never ships game code or data.

This file is your routing table. Don't scan the repo — open the one file that matches your
task.

## Where the project is

M0 is done. `PLAN.md` says what is next. `../proof` (OpenDeuteros) is prior art: read-only,
cited for measurements, ported from only when a milestone says so.

## Shared root, self-contained games

Decisions 013 and 014. Everything shared sits at the root: `core/`, `frontend/` (with the
RmlUi menu), `cmake/`, `tools/`, `tests/` and `docs/`. Each game is `games/<id>/`, holding its
images, references, definition, settings, hacks, RmlUi assets, `CMakeLists.txt` and `build/`.
A game is built from its own folder, with the commands in `docs/game.md` item 1.

## Tools

This table owns the roster; `docs/tools.md` explains how to run them.

| Tool | Milestone | Job |
| --- | --- | --- |
| `tools/doctor.py` | M1 | what is installed; git hygiene |
| `tools/emulator.py` | M2 | the WinUAE config for a game |
| `tools/capture.py` | M2 | drive WinUAE: shots, RAM dumps |
| `tools/pixdiff.py` | M2 | pixel diff |
| `tools/adf.py` | M3 | identify images against `dumps.txt` |
| `tools/anchor.py` | M4 | a CRC-32 anchor from a RAM dump |

## Where to look

| If you're going to… | Read first |
| --- | --- |
| decide what to do next | `PLAN.md` |
| find out what happened | `PROGRESS.md` |
| find out why | `DECISIONS.md` |
| find a folder | `ARCHITECTURE.md` |
| check code style or verification | `docs/conventions.md` |
| add a game, a setting or a hack | `docs/game.md` |
| run a tool | `docs/tools.md` |
| build or run the runtime; HLE; timing | `docs/runtime.md` |
| use the oracle | `docs/emulator.md` |
| look up a generic disk format | `docs/formats/` (M3) |
| run one game's commands: config, references, build, run, compare | `games/<id>/README.md` |
| work on one game's images, build, facts or hacks | `games/<id>/` and its `docs/`, laid out as in `docs/game.md` |

## Hard rules

1. Engine ours, bytes theirs: no game data in git or in an executable.
2. No game code in any form: no disassembly and no byte sequences. Anchors are CRCs and
   addresses.
3. The shared code is game-agnostic (decision 012). Anything specific to one game, including
   its images, references, CMake project and build tree, lives in its own `games/<id>/`
   (decision 013).
4. `core/` has no SDL, no OS headers, no file I/O and no `malloc`.
5. Never invent a constant.
6. A clean compile is not a test result.
7. Hack value 0 is the original game, and headless runs force every hack to 0 (decision 011).
8. `PLAN.md` is the author's file. Tick boxes only on observation, and propose scope changes
   in conversation.
9. No obvious comments, and no decision history in code comments.
10. One owner per fact.

## Keeping these docs true

A change to something a doc describes updates that doc in the same change.
