# Plan

AmigaRecomp turns Amiga games into one natively built, statically linked executable per game.
Each executable runs the game under the Musashi interpreter on a C99 Amiga runtime with
Kickstart HLE, loads the player's own disk images at runtime, and ships no game code or bytes.

This file says how to start work on a game and how to write that game's plan. It has no boxes
to tick. What happens next for a game is in `games/<id>/PLAN.md`, and `ARCHITECTURE.md` lists
the games.

Milestone numbers cited without a game (M0–M6) in the shared docs and in `PROGRESS.md` are
Deuteros's, in `games/deuteros/PLAN.md`. That plan also builds the shared root, and it still
owns the shared work it has not finished: the disk-image loading (M3) and the settings menu
and hacks (M4).

## Method

- Build the oracle early: WinUAE is the reference every diff is taken against (decision 010).
- Every runtime constant cites its source (`docs/conventions.md`).
- The shared code stays game-agnostic (decision 012).

## How a player runs it

The target:

- Download `<id>` (or `<id>.exe`) and run it.
- Give it the images by command line, drag-and-drop, or F1 → Disks → Open…. Accepted formats
  are ADF, ZIP, ADZ and GZ.
- Keys: F1 opens the menu, F5 saves state, F9 restores it, F11 cycles DF0, and F12 takes a
  screenshot.
- Settings, writable disks and states live in `SDL_GetPrefPath("AmigaRecomp","<id>")`.

## Starting a game

1. Pick the `<id>` (`docs/game.md`, item 1) and create `games/<id>/`.
2. Write `games/<id>/PLAN.md` from the template below, before any other file in the folder.
3. Work through `docs/game.md`, item 6, one milestone at a time. Each command, the first time
   it gives a result, goes into `games/<id>/README.md` (`docs/game.md`, item 2).
4. Add the game to `ARCHITECTURE.md`, "Exists today".

## Writing a game's plan

- Legend: `[ ]` not started, `[~]` in progress, `[x]` done. `[x]` is used only once the result
  has been observed. The game's next milestone is the first one that is not `[x]`.
- A box belongs to one game's plan only; ticking it says nothing about another game.
- Milestones are numbered from M1 within the plan. Outside it, cite them as `<id> M<n>`.
- Each milestone holds a box, its tasks, a *Done when:* and an *Observed:*.
    - *Done when:* names an observable result, using one of the four verification methods in
      `docs/conventions.md`, with its figures.
    - *Observed:* gives the date, the platform, the command and the figures, and says what was
      not observed.
- Work in the shared code is written as a task of the game that first needs it, and it stays
  game-agnostic (decision 012). If another game's plan already holds that work in an open
  milestone, cite it (for example `deuteros M3`) instead of copying it, and wait for it.
- Command lines live in the game's `README.md`. The plan names the README section instead of
  repeating the command.
- Ticking a box fills in *Observed:* and appends to `PROGRESS.md` (`docs/conventions.md`,
  "Progress").
- The plan is the author's file: scope and order changes are proposed in conversation.

### Template

Copy this into `games/<id>/PLAN.md`, substitute `<id>` and `<Title>`, and keep only the
milestones the author wants. M1 and M2 come from `docs/game.md`, item 6, steps 1–5; M3 and M4
from steps 6–7; M5 from item 7.

```markdown
# <Title> — plan

What happens next for `<id>`. The root `PLAN.md` owns the legend and the rules for this file.
Paths are from the `amigarecomp/` root, and the command lines are in `README.md`.

## Commands

| Step | Command | Status |
| --- | --- | --- |
| WinUAE config | `tools/emulator.py` | `[ ]` |
| References | `tools/capture.py` | `[ ]` |
| Build, Linux | `cmake … build/cmake-linux` | `[ ]` |
| Build, Windows | `cmake … build/cmake` | `[ ]` |
| Run headless with a script | `<id> --headless --script …` | `[ ]` |
| Compare with WinUAE | `tools/pixdiff.py` | `[ ]` |

## Milestones

### M1 — The oracle

- `[ ]`
    - Put the images in `games/<id>/disks/` and choose the pair WinUAE boots; the evidence goes
      in `docs/dumps.md`.
    - Write `oracle.uae.in` and a `scripts/<name>.dbg` that reaches the first screen.
    - *Done when:* `emulator.py` writes the config, WinUAE loads it with no `unknown` or
      `obsolete` line, and two `capture.py` runs write first-screen shots into `reference/`
      that agree on every pixel.
    - *Observed:*

### M2 — Build and the first screen

- `[ ]`
    - Write `CMakeLists.txt` (`docs/game.md`, item 2), a `game.c` with the roles and the
      drives, and `dumps.txt` with the reference dump as `known`.
    - Write a headless script that reaches the first screen. Implement each fatal in the
      shared code (`docs/game.md`, item 6, step 5).
    - *Done when:* the script's shot pixel-diffs against `reference/<shot>.png` on every
      informative pixel, and the README's build and run sections hold the commands that
      produced it.
    - *Observed:*

### M3 — The reference dump is verified

- `[ ]`
    - Write a script that reaches a named in-game screen.
    - *Done when:* the verification gate of `docs/game.md`, item 6, step 6, passes, and
      `dumps.txt` and `docs/dumps.md` record it.
    - *Observed:*

### M4 — Every dump on hand

- `[ ]`
    - Run the scripts on every readable image in `games/<id>/disks/`.
    - *Done when:* every readable image has the status `verified`, `known` or `refused` (with
      its reason), and none ends in an unexplained fatal.
    - *Observed:*

### M5 — Hacks

- `[ ]`
    - One task per hack the author asks for, each following `docs/game.md`, item 7.
    - *Done when:* each hack's script shows its effect, and with every hack off the M3 figures
      are unchanged.
    - *Observed:*

### M6 — Distributable executables

- `[ ]`
    - Build the Windows and Linux `<id>` from a checkout that holds no disks.
    - *Done when:* each build runs on a machine with no compiler, no Python and no SDL `-dev`
      packages, given only a player's images, and reaches the M3 screen. The author observes
      this.
    - *Observed:*

## Risks

For the author: one row per risk, with its answer, in a `| Risk | Answer |` table.
```
