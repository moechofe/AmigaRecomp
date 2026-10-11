# Lemmings — boot chain and screens

Facts about the `[cr SR]` pair (`docs/dumps.md`), observed in the runtime on 2026-10-10 with
`scripts/psygnosis.input` unless another source is named. Addresses, never bytes.

## Boot chain

- The bootblock never returns to Kickstart's strap: it reads `Lemmings (1991)(Psygnosis)(Disk 1
  of 2)[cr SR].adf` offset 0x0CC000, 0x3A00 bytes, to RAM 0x070000 through trackdisk (load 2)
  and runs it. That is the Skid Row cracktro, which waits for the left mouse button.
- The cracktro's exit opens `intuition.library`, calls `RethinkDisplay` and reads the same disk
  at offset 0x0CA000, 0x1200 bytes, to RAM 0x070000 (load 3).
- That loader runs with DMA and interrupts off, clears chip RAM 0x000400–0x06FFFF, selects DF0
  through CIA-B PRB and waits for `/RDY` (CIA-A PRA bit 5). It then reads whole tracks by disk
  DMA: on the way to the owl, 28 track reads from cylinders 0–20, to RAM 0x061000 and 0x073D10;
  on the way to the title menu, Disk 1 up to cylinder 63, then Disk 2 from DF1 (cylinders 0 and
  12–20) to RAM 0x058800. It needs Kickstart's CIA-B DDRB 0xFF, which `docs/runtime.md`,
  "Kickstart handoff", sets at the bootblock call.

## Screens

Display registers as the game writes them (copper, line 10–12 of each frame):

| Screen | BPLCON0 | DDFSTRT–DDFSTOP | DIWSTRT / DIWSTOP | Picture |
| --- | --- | --- | --- | --- |
| Psygnosis owl, balloon intro | 0x5200: lores, 5 planes | 0x38–0xD0 | 0x2C81 / 0xF4C1 | DIW x 129–448, scanlines 44–243 |
| DMA Design | 0xC200: hires, 4 planes | 0x3C–0xD4 | 0x2C81 / 0xF4C1 | 640 hires pixels over DIW x 129–448, scanlines 44–243 |
| Title menu | 0xC200: hires, 4 planes | 0x3C–0xD4 | 0x2C81 / 0xFCC1 | DIW x 129–448, scanlines 44–251 |

`game_def.view` (`game.c`) is the title menu's window, 320 × 208 from DIW x 129, scanline 44;
the 200-line screens sit inside it. In the 320 × 256 capture window that is x 9, y 2, with DIW
440–448 clipped.
