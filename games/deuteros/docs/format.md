# Deuteros — disk and screen facts

Facts about this one game that the runtime, the hacks and the scripts rely on. They are
descriptions, never quotations: the game's code and strings are not reproduced here. The byte
layout of the disk data (graphics, text, compression) is owned by the prior art in
`../../../../proof/doc.format/data.md`; this file summarises only what the runtime needs and
cites the rest.

"Disk 1" and "Disk 2" mean `Deuteros - The Next Millennium (1991)(Activision)(M3)` disks 1 and
2 of 2, the untagged M3 dump pair (short form `Disk 1 of 2 (untagged M3 dump)`). Offsets are
byte offsets into the 901 120-byte ADF; RAM addresses are Amiga addresses.

## Images

Both images are 901 120 bytes (`ls -l`, observed 2026-10-06). Each is byte-identical to the
image of the same name in `../proof/build/adf/`, from which every figure in the proof's format
documents was taken (`cmp` equal, observed 2026-10-06).

## Boot chain

The disk has no filesystem during boot. Three hops, described in full in
`../../../../proof/doc.format/loader.md` (sections "The shape of it", "Stage 1", "Stage 2"):

1. Kickstart reads the bootblock, Disk 1 ADF 0x000000-0x0003FF, and calls it. It carries the DOS
   type signature, a rootblock pointer of 880 and code from +0x0C. A throwaway read on
   2026-10-06 confirmed the signature, the value 880 at +0x08, and that the 256 longwords of
   the block sum (end-around carry) to 0xFFFFFFFF, i.e. a valid bootblock checksum. Block 880
   holds code, not a root block (proof, loader.md).
2. The bootblock reads one track, Disk 1 ADF 0x002C00-0x0041FF (5 632 bytes), to RAM 0x012800.
   This stage 2 is the resident loader.
3. Stage 2 dispatches on a one-word index that the bootblock sets to 0, so a cold boot always
   takes index 0 (proof, loader.md, "The dispatch table"):
    - load index 0: Disk 1 ADF 0x005800-0x0099FF (16 896 bytes) to RAM 0x020000;
    - load index 1: Disk 1 ADF 0x06E000-0x0DA9FF (444 928 bytes) to RAM 0x013000, run after the
      language-menu click rather than at boot.

All five ranges lie inside the image and have the sizes stated (throwaway check, 2026-10-06).
The runtime's load log confirms them: `deuteros --headless --script scripts/earth-base.input`
on the pair, observed 2026-10-06, printed `load 2: DF0=disk1 ADF 0x002C00 +0x01600 -> RAM
0x012800` (stage 2), `load 4: ... ADF 0x005800 +0x04200 -> RAM 0x020000` (index 0) and
`load 41: ... ADF 0x06E000 +0x6CA00 -> RAM 0x013000` (index 1). The number after `load` counts
reads from the start of the run, not the dispatch index. The run made 82 reads and 80 logged
loads.

Between those two, stage 2 first reads a 12-sector block at Disk 1 ADF 0x00B000 to RAM
0x01FE00 (`load 3`), and the title and menu data stream one track at a time through track
buffers at RAM 0x023024 and 0x0256C0 (the log's lines 5-40 and 42-81, same run). What those
loads are is the proof's table "Every read, from the runtime's load log"; it is not restated.

## Drives and disks

- Only trackdisk unit 0 is read during boot. In the 2026-10-06 run every `load` line named
  `DF0`; none named `DF1` or `DF2`, so no read of any other unit occurs on the way to the
  Earth base. (Proof, loader.md: the loader opens unit 0 and never another.)
- After a language is chosen the game prompts for its data disk and wants it in DF0, not DF1.
  The scripts therefore swap `disk2` into DF0 at the prompt. In the 2026-10-06 run the first
  Disk 2 read (`load 47`, ADF 0x000000, the data-disk check) follows Disk 1's own track 0
  read at the same offset.
- DF1 holds the save disk: the save screen reads track 0 of unit 1 for the disk to format
  (observed 2026-10-03, `../../../../proof/doc.agent/recomp.md`: with Disk 2 in DF1 formatting
  ended in "operation failed"). The runtime therefore always mounts the save disk in DF1;
  writes reach only that image.

## Display

The game's screen is 320x200, lores, 4 bitplanes (16 colours), non-interlaced, with a
contiguous plane layout and two buffers, measured 2026-08-15 (proof, `doc.format/screen.md`,
"The screen"). Only three screens have been measured and all three have this shape; that it
holds for every screen is unknown.

The game's mouse pointer is hardware sprite 0; the other seven sprites point at one common
empty sprite (proof, screen.md; the runtime's `game_def.pointer_sprite = 0`, `game.c`). The
menu's yellow marker is a sprite pixel, which is why sprites add colours to a capture that the
bitplanes do not hold.

## Screens

Distinct `$0RGB` values in a frame, as the scripts' `until colours N` step counts them (the
same metric as `tools/capture.py`). These are the values `scripts/menu.input` and
`scripts/earth-base.input` wait for:

| Screen | Colours | Notes |
| --- | --- | --- |
| Title | 16 | `until colours 16` waits for it; the first `until` of both scripts |
| Language menu | 4 | two in the bitplanes plus two sprite colours |
| Language menu, sprites off | 2 | after `sprites off`; the colours of the bitplanes alone |
| Earth base | 18 | `until colours 18`, the first game screen |

The Earth base figure is also the one in the proof's `doc.agent/emulator.md` (first real
screen, 18 colours). The headless run of 2026-10-06 printed `16 colours` for the Earth base shot
because the script turns sprites off before the shot. Reference captures of two earlier frames
exist (`reference/pre-title-06.png`, `reference/pre-title-14.png`), but no colour count for
them is documented: unknown.

## Verification figures

Observed 2026-10-06 with `tools/pixdiff.py` against the WinUAE references in `reference/`,
the 320x256 capture area being 81 920 pixels:

| Shot | Matching pixels | Differences |
| --- | --- | --- |
| Language menu, sprites off | 81 920 of 81 920 (EXACT) | none |
| Earth base, sprites off | 81 862 of 81 920 | inside x 13-23, y 79-90 |

What causes the 58 differing pixels in that rectangle is not established here: unknown.
