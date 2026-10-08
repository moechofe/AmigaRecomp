# Lemmings — plan

What happens next for `lemmings`. The root `PLAN.md` owns the legend and the rules for this
file. Paths are from the `amigarecomp/` root, and the command lines are in `README.md`.

## Commands

| Step | Command | Status |
| --- | --- | --- |
| WinUAE config | `tools/emulator.py` | `[x]` |
| References | `tools/capture.py` | `[x]` `scripts/boot.dbg` |
| Build, Linux | `cmake … build/cmake-linux` | `[ ]` |
| Build, Windows | `cmake … build/cmake` | `[x]` |
| Run headless with a script | `lemmings --headless --script …` | `[~]` stops at `OpenLibrary("intuition.library")` |
| Compare with WinUAE | `tools/pixdiff.py` | `[ ]` |

## Milestones

### M1 — The oracle

- `[x]`
    - `oracle.winuae.in`, `scripts/boot.dbg`, and the choice of the oracle's pair in
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

- `[~]`
    - The four-line `CMakeLists.txt` (`docs/game.md`, item 2).
    - A `game.c` with the roles `disk1` and `disk2`, and drives chosen from the oracle's
      evidence in `games/lemmings/docs/dumps.md`.
    - Hardware disk DMA in the shared code (core/disk.c): MFM-encoded ADF tracks, CIA-B
      drive control, the CIA-A drive status bits, and the index pulse. Moved here from
      deuteros M5, because the Disk 1 loader writes DSKLEN.
    - A headless script that crosses the cracktro with the left mouse button (`click`) and
      shoots the Psygnosis owl.
    - *Done when:* that shot diffs against `games/lemmings/reference/psygnosis.png`, and the
      README's build and run sections hold the commands that produced it.
    - *Observed:* 2026-10-06, Windows (WinLibs gcc 16.2). `lemmings.exe` builds and links.
      `scripts/psygnosis.input` does not reach the owl: after the click at frame 600 the
      Skid Row cracktro runs its exit (`0x030360`: restore vectors, DMA, INTENA and the copper
      pointer, `OpenLibrary("intuition.library")`, `RethinkDisplay` LVO -390, power LED on, `rts`)
      and the run ends with `fatal: OpenLibrary("intuition.library") is not implemented`, PC
      `03039C`, frame 600. No `DSKLEN` write is reached yet, so `core/disk.c` is exercised
      only by a throwaway harness: Disk 1 tracks 0, 40 and 41, after 20 steps, all 11 sectors
      decode with exact checksums and data. Deuteros is unchanged: `menu.input` 81 920/81 920
      EXACT, `earth-base.input` 81 862/81 920 inside x 13–23, y 79–90. Blocked on the plan's
      stop rule: intuition.library is a new library, the author's call (the cracktro's own exit
      needs only `OpenLibrary` and `RethinkDisplay`).
