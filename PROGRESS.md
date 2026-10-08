# Progress log

What has already happened, newest last, one or two lines each. Each game's
`games/<id>/PLAN.md` owns what happens next; this file owns what is behind us. Detail belongs
in the doc the work produced; an entry here is a date, the headline claim, and a pointer.

A number quoted here is a verification that was observed, not a target.

- **2026-10-06** — **M0 done.** Foundation docs, decisions 001–014, and the per-game method in
  docs/game.md; nothing copied from ../proof.
- **2026-10-06** — **M1 in progress.** Static Linux `deuteros`: black 960×768 window, `ldd` lists
  only glibc, a second configure downloads nothing; Windows `objdump` pending (the author).
  docs/tools.md.
- **2026-10-06** — **M2 done.** Menu shot 81 920/81 920 and Earth base 81 862/81 920 (x 13–23,
  y 79–90) against the proof's WinUAE references, ~10× real time at -O3; no WinUAE run, and
  `capture.py` is observed only for `--help` and its refusal off Windows. docs/runtime.md,
  docs/emulator.md.
- **2026-10-06** — **Lemmings oracle.** First WinUAE runs by this project: `games/lemmings/
  oracle.uae.in` loads clean, and `scripts/boot.dbg` takes Psygnosis and DMA Design shots that
  agree 81 920/81 920 across runs, plus the title menu. Every dump on hand has an intro; F7 is
  the Amiga's left mouse button for crossing it. docs/emulator.md, games/lemmings/docs/dumps.md.
- **2026-10-06** — **M1 done.** Static Windows `deuteros.exe` with WinLibs GCC 16.2.0: only
  system DLLs, and the M2 diffs reproduce exactly; `frontend/main.c` moved into the executable
  to fix the MinGW `WinMain` link. docs/runtime.md, "Windows toolchain".
- **2026-10-07** — **M3 done.** ADF, ADZ, GZ and ZIP images from the command line, the Open…
  dialog and remembered paths, identified by `dumps.txt` and `identify`; an unknown dump runs
  under a banner; `test_image` passes; the untagged pair from a ZIP reproduces M2's figures
  (81 862/81 920 and 81 920/81 920). Dropped files and Windows not observed. docs/game.md,
  docs/runtime.md, docs/formats/adf.md.
- **2026-10-08** — **M4 done.** keep_pointer holds x 28, y 19 (102, 29 off); Space starts and stops
  Advance Time; hacks off keep 81 920/81 920 and 81 862/81 920; a Hacks toggle survives a
  restart; Deuteros's navy panel; ctest passes. Windows not observed. games/deuteros/docs/hacks.md.
- **2026-10-08** — **FS-UAE oracle.** FS-UAE 3.1.66 (Flatpak) on Linux reproduces the Lemmings WinUAE references: psygnosis, dma-design and title-menu 81 920/81 920 each, a restored state EXACT in 5.8 s; XTEST keys, clicks and pointer moves reach the Amiga, on a private Xvfb because the desktop's Xwayland drops them; templates renamed to oracle.winuae.in / oracle.fsuae.in. Decision 016, docs/emulator.md "FS-UAE".
- **2026-10-08** — **Deuteros under FS-UAE.** oracle.fsuae.in and scripts/boot.dbg take Deuteros from a cold boot to the Earth base on Linux; differences from the WinUAE references are the pointer sprite and WinUAE's own animation box. WinUAE's `I floppy0=` swap does nothing in FS-UAE; F9 inserts Disk 2. docs/emulator.md "FS-UAE".
- **2026-10-08** — **Deuteros M5 done.** All 28 images on hand are `verified`, `known` or `refused`: 7+4 verified (menu 81 920/81 920, Earth base reached), the Spanish SKN pair and the three Black Monks dumps `known` (6), the two save disks `refused` (`MSCF` cabinets). `CMD_RESET` accepted; `InitRastPort` clears 0x4E bytes, as the FS-UAE oracle's Kickstart 1.3 does, which unblocked Black Monks. `identify` takes four stage-2 CRCs. games/deuteros/docs/dumps.md.
