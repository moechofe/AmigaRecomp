# Lemmings — plan

What happens next for `lemmings`. The root `PLAN.md` owns the legend and the rules for this
file. Paths are from the `amigarecomp/` root, and the command lines are in `README.md`.

## Commands

| Step | Command | Status |
| --- | --- | --- |
| Oracle config | `tools/emulator.py` | `[x]` WinUAE and FS-UAE |
| References | `tools/capture.py` | `[x]` `scripts/boot.dbg` |
| Build, Linux | `cmake … build/cmake-linux` | `[x]` 2026-10-07, `deuteros` M3 *Observed:* |
| Build, Windows | `cmake … build/cmake` | `[x]` |
| Run headless with a script | `lemmings --headless --script …` | `[x]` `scripts/psygnosis.input` |
| Compare with the oracle | `tools/pixdiff.py` | `[x]` owl 81 920/81 920 |

## Milestones

### M1 — The oracle

- `[x]`
    - `oracle.winuae.in`, `oracle.fsuae.in`, `scripts/boot.dbg`, and the choice of the oracle's
      pair in `docs/dumps.md`.
    - *Done when:* `emulator.py` writes the config, WinUAE loads it with no `unknown` or
      `obsolete` line, and two `capture.py` runs write first-screen shots into `reference/`
      that agree on every pixel.
    - *Observed:* 2026-10-06, Windows. `emulator.py` wrote both configs, and WinUAE 6.0.3
      loaded each one with no `unknown` or `obsolete` line. `capture.py` with `boot.dbg`
      reached the Lemmings title menu twice; the Psygnosis and DMA Design shots agree
      81 920/81 920 across runs. The Windows build was not tried: this machine has no C
      compiler on `PATH`.
    - *Observed:* 2026-10-08, Linux, FS-UAE 3.1.66 (Flatpak). `boot.dbg` reproduced the WinUAE
      references: `psygnosis`, `dma-design` and `title-menu` 81 920/81 920 each (decision 016;
      `docs/emulator.md`, "FS-UAE").

### M2 — Build and the Psygnosis owl

- `[x]`
    - The four-line `CMakeLists.txt` (`docs/game.md`, item 2).
    - A `game.c` with the roles `disk1` and `disk2`, and drives chosen from the oracle's
      evidence in `games/lemmings/docs/dumps.md`.
    - Hardware disk DMA in the shared code (core/disk.c): MFM-encoded ADF tracks, CIA-B
      drive control, the CIA-A drive status bits, and the index pulse. Moved here from
      deuteros M5, because the Disk 1 loader writes DSKLEN.
    - `intuition.library` in the shared HLE, for the cracktro's exit: `OpenLibrary` opens it and
      LVO -390 `RethinkDisplay` is a no-op (`docs/runtime.md`, "Library calls"). Author's call,
      2026-10-08.
    - Run `scripts/psygnosis.input` headless, repeatedly, and implement each fatal or stall in
      the shared code (`docs/game.md`, item 6, step 5).
    - A headless script that crosses the cracktro with the left mouse button (`click`) and
      shoots the Psygnosis owl.
    - *Done when:* that shot diffs 81 920 of 81 920 against
      `games/lemmings/reference/psygnosis.png`, and the README's build and run sections hold the
      commands that produced it.
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
    - *Observed:* 2026-10-10, Windows (WinLibs GCC 16.2.0), tree rebuilt after `f14e445`, which
      added `intuition.library`. `psygnosis.input` passes the cracktro's exit
      (`RethinkDisplay: no screens; display left alone`, `intuition -390: 1 calls`) and ends with
      exit 3, `script timeout: line 6: 8 colours never reached in 3000 frames (1 on screen)`,
      with no shot; `frames 3604, instructions 35214662, cycles 511993516, reads 3, loads 3`
      (`load 2: DF0=disk1 ADF 0x0CC000 +0x03A00 -> RAM 0x070000`, `load 3: … 0x0CA000
      +0x01200 -> RAM 0x070000`; also `copper stopped on a MOVE to 0x000` and `read of
      write-only custom register 0x088`). Not observed: Linux; whether the Disk 1 loader
      writes DSKLEN before the stall.
    - *Observed:* 2026-10-10, Windows (WinLibs GCC 16.2.0). The black screen after the cracktro
      was CIA-B DDRB 0x00: Kickstart's CIA state was set only on the bootblock's return, which
      the Lemmings bootblock never makes, so the loader's drive select stayed an input and it
      polled `/RDY` forever. WinUAE 6.0.3 at the bootblock call (`scripts/handoff.dbg`) gave the
      stage-2 CIA values but for the times of day; `amiga_boot` now sets them, and a read of
      `COPJMP1`/`COPJMP2` restarts the copper (`docs/runtime.md`, "Kickstart handoff" and
      "Timing"). `psygnosis.input` then loads through disk DMA (28 track reads, DF0 cylinders
      0–20) and exits 0: `shot games/lemmings/build/shots/psygnosis.png (frame 864, 32
      colours)`, `frames 864, instructions 10296061, cycles 122634040, reads 3, loads 3`.
      `pixdiff` against `reference/psygnosis.png`: 81 920 of 81 920, EXACT; a second headless
      run and a windowed run gave the same frame, figures and diff. Deuteros is unchanged:
      `menu.input` 81 920/81 920 EXACT, `earth-base.input` 81 862/81 920 inside x 13–23,
      y 79–90; `ctest` 3 of 3. Not observed: Linux.

### M3 — The reference dump is verified

- `[ ]`
    - Find, by hand in the oracle (README, section 4), the input that takes the title menu to
      the preview screen of the first Fun level: a left click on "1 Player" (F7 is the left
      button; under FS-UAE `!move DX DY` aims the pointer) or a key. Add it to
      `scripts/boot.dbg` after the `title-menu` shot, followed by `!until N` on the preview's
      colour count and `!shot fun-1`. A key that `tools/capture.py` cannot send yet is added to
      its key set and to `docs/tools.md`.
    - Two `capture.py` runs write `reference/fun-1.png`; its colours and reproducibility go
      into `docs/emulator.md`, "References".
    - `scripts/fun-1.input`: the cracktro click of `psygnosis.input`, `until colours 3` then
      `until colours 30` for the title menu (as `boot.dbg` does), the same input (`click X Y`
      or `key NAME`, `docs/runtime.md`, "Input scripts"), `until colours N`, then
      `shot fun-1`. Implement each fatal in the shared code (`docs/game.md`, item 6, step 5).
    - Set both `[cr SR]` images to `verified` in `dumps.txt`, and record the commands, the date
      and the figures in `docs/dumps.md`.
    - *Done when:* the verification gate of `docs/game.md`, item 6, step 6, passes: the M2 owl
      shot at 81 920 of 81 920, and `fun-1.input` reaching the preview with no fatal, its shot
      agreeing with `reference/fun-1.png` on every informative pixel. The README holds the
      commands, and *Observed:* names the emulator that took `fun-1.png` (decision 016).
    - *Observed:*

## Risks

For the author.

| Risk | Answer |
| --- | --- |
| The Disk 1 loader is the first to use hardware disk DMA, and `core/disk.c` has carried it only to the title menu (Disk 1 to cylinder 63, Disk 2 cylinders 0 and 12–20) | Its fatals name the missing behaviour; implement it from the HRM, as `docs/runtime.md`, "Floppy hardware", does |
| The game opens a library beyond graphics and intuition | `OpenLibrary` gives it a stub whose first call is fatal and names it; adding a library is the author's call, as `intuition.library` was |
| The Fun 1 preview animates | The diff counts informative pixels only, and *Observed:* names the region that differs, as `title-menu` does in `docs/emulator.md`, "References" |
