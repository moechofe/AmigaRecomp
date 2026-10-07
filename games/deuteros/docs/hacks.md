# Deuteros — hacks

One section per hack, in `game_def.hacks` order. A hack is opt-in: value 0 is the original game,
and headless runs force every hack to 0 (decision 011). Both hacks are derived from the prior art
in `../../../../proof/doc.agent/recomp.md`, sections "Mouse" and "Keyboard", and re-measured
here. Every address is a RAM address in the index-1 overlay (ADF = RAM + 0x5B000 in untagged
`Disk 1 of 2`), loaded before the Earth base is reached. No game code appears here, only addresses
and what the code does; anchors are CRC-32s (zlib) of the original bytes.

All anchors were computed with `python tools/anchor.py games/deuteros/build/earth-base.ram
--addr 0x… --len N` on the RAM dump that `deuteros --headless --script
games/deuteros/scripts/earth-base.input --dump games/deuteros/build/earth-base.ram
games/deuteros/build/pair.zip` writes (observed 2026-10-07). Each CRC equals the CRC-32 of the
proof's guard array for the same range (throwaway comparison, same date), so the dump holds the
code the proof guarded.

## `keep_pointer`

Setting: bool, default 0, `hack.keep_pointer` in `settings.ini`. Source: proof "Mouse".

**Original.** The Advance Time loop runs at 0x023F60–0x024022. It has two exits. The one taken
by a click forces the game's pointer x word at 0x01FFC8 to 74 at 0x023FC6, then continues into
the pointer update at 0x020118. That update adds the last motion event's deltas, the words
0x01EE10 (dx) and 0x01EE12 (dy), to the pointer once more. The other exit, at 0x023FE0, leaves x
alone. The click that ends Advance Time therefore throws the pointer to the right.

**Hack.** At 0x023FC6 the hook skips the 8-byte instruction, charges its cycles, resumes at
0x023FC6 + 8 and zeroes the two delta words. The pointer stays where the player left it.

**Anchors.**

| Addr | Len | CRC-32 | What |
| --- | --- | --- | --- |
| 0x023FC6 | 8 | 0xD8A44675 | the instruction that forces the pointer x |

**Proof.** `scripts/keep-pointer.input`: the Earth-base prefix of `earth-base.input`, then
`hack keep_pointer on`, `click 28 10` (park the pointer top-left, then 28 and 10 counts), `wait
60`, `click`, `wait 10`. Measured 2026-10-07, headless, `--dump`:

| Run | Pointer x (word 0x01FFC8) | Pointer y (word 0x01FFCE) | Last deltas (0x01EE10 / 0x01EE12) |
| --- | --- | --- | --- |
| hack on | 28 | 19 | 0 / 0 |
| same script without the `hack` line | 102 | 29 | 28 / 10 |

The first click starts Advance Time: a variant that ends with `wait 30` after `click 28 10` holds
byte 0x01FFD9 = 1. The y word is at 0x01FFCE; the word at 0x01FFCA reads 0 in both runs. A search
of 0x01FF00–0x020000 over both dumps for the word that is 19 with the hack and 29 without it found
one address, 0x01FFCE.

## `space_advance`

Setting: bool, default 0, `hack.space_advance`. Source: proof "Keyboard".

**Original.** The input handler turns a new raw key code below 0x50 into a character and puts it
in a 20-byte queue at 0x01EEC0, with the count in the word 0x01EED6; Space gives 0x20. The routine
at 0x01F238 pops the head into d0 (d0 is unchanged on an empty queue). The main loop at
0x040574–0x040670 runs the panel hover and click routine, pops one key and, at 0x04063E, reacts
only to one letter; its last instruction at 0x040670 loops back. Space is popped and ignored, so
at the Earth base the queue count returns to 0 and nothing starts.

The panel's slot table at 0x0214D8 holds 8 slots, a pointer to the pointer to the slot-mask word
(0x01301A, which leads to 0x013008), then id and handler pairs. Slot 0 is icon 0x1A with handler
1, which the table at 0x0212B0 maps to Advance Time. Hovering slot 0 writes 4 to the word 0x03F77C
(the click-sound context), 0 to 0x0222A0, 1 to 0x02229E and 0x1A to the longword 0x022292. A click
with x below 0x61 takes the panel click path at 0x0222E8, which runs the handler indexed by the
word 0x02229E. The Advance Time wait loop begins at 0x023FA0 and ends with a branch back at
0x023FB8; a click falls through to the exit at 0x023FBA. The loop never pops keys: a Space pressed
during Advance Time stays queued and the main loop pops it afterwards.

While Advance Time runs, byte 0x01FFD9 is 1 and the game hides the pointer; idle it is 0.

**Hack.** Two hooks.

- *Start*, at 0x04063E, only when d0.b is 0x20, the slot table anchor matches and bit 0 of the
  slot-mask word is set: write what hovering slot 0 writes, charge the cost of a call, push
  0x040670 and continue at 0x0222E8, so the game's own click path runs handler 1 wherever the
  pointer is and returns to the main loop's next pass. Any other state runs the original
  instruction.
- *Stop*, at 0x023FA0, only when the queue count is non-zero and the queue head is 0x20: charge
  the cost of a call, push 0x023FBA and continue at 0x01F238, so the game pops the Space itself and
  takes the click exit. A non-Space head is left alone.

**Anchors.**

| Addr | Len | CRC-32 | What |
| --- | --- | --- | --- |
| 0x04063E | 4 | 0xFE929D58 | the main loop's key test (start hook guard) |
| 0x023FA0 | 6 | 0x313D2691 | the head of the wait loop (stop hook guard) |
| 0x0214D8 | 10 | 0xB6B95F64 | the panel's slot-0 header; checked inside the start hook |

**Proof.** Measured 2026-10-07, headless, `--dump`, final words of the dump:

| Script | Byte 0x01FFD9 | Word 0x01EED6 |
| --- | --- | --- |
| `space-advance-start.input` (Space, `wait 30`) | 1 | 0 |
| same, without the `hack` line | 0 | 0 |
| `space-advance.input` (Space, `wait 60`, Space, `wait 100`) | 0 | 0 |
