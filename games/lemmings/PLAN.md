# Lemmings — plan

What happens next for `lemmings`. The root `PLAN.md` owns the legend and the rules for this
file. Paths are from the `amigarecomp/` root, and the command lines are in `README.md`.

## Commands

| Step | Command | Status |
| --- | --- | --- |
| WinUAE config | `tools/emulator.py` | `[x]` |
| References | `tools/capture.py` | `[x]` `scripts/boot.dbg` |
| Build, Linux | `cmake … build/cmake-linux` | `[ ]` |
| Build, Windows | `cmake … build/cmake` | `[ ]` |
| Run headless with a script | `lemmings --headless --script …` | `[ ]` |
| Compare with WinUAE | `tools/pixdiff.py` | `[ ]` |

## Milestones

### M1 — The oracle

- `[x]`
    - `oracle.uae.in`, `scripts/boot.dbg`, and the choice of the oracle's pair in
      `docs/dumps.md`.
    - *Done when:* `emulator.py` writes the config, WinUAE loads it with no `unknown` or
      `obsolete` line, and two `capture.py` runs write first-screen shots into `reference/`
      that agree on every pixel.
    - *Observed:* 2026-10-06, Windows. `emulator.py` wrote both configs, and WinUAE 6.0.3
      loaded each one with no `unknown` or `obsolete` line. `capture.py` with `boot.dbg`
      reached the Lemmings title menu twice; the Psygnosis and DMA Design shots agree
      81 920/81 920 across runs. The Windows build was not tried: this machine has no C
      compiler on `PATH`.

### M2 — Build and the Psygnosis owl

- `[ ]`
    - The four-line `CMakeLists.txt` (`docs/game.md`, item 2).
    - A `game.c` with the roles `disk1` and `disk2`, and drives chosen from the oracle's
      evidence in `games/lemmings/docs/dumps.md`.
    - A headless script that crosses the cracktro with the left mouse button (`click`) and
      shoots the Psygnosis owl.
    - *Done when:* that shot diffs against `games/lemmings/reference/psygnosis.png`, and the
      README's build and run sections hold the commands that produced it.
    - *Observed:*
