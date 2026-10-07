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
