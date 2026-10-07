# Lemmings dumps

Evidence for each dump seen. CRC-32 is zlib's over the 901 120 decompressed bytes. The images
sit in `games/lemmings/disks/` as they arrived. `dumps.txt` lists the oracle's pair as `known`.

## The oracle's pair

| Name as distributed | CRC-32 | Role |
| --- | --- | --- |
| `Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].adf` | `074bc441` | disk 1, boots in DF0 |
| `Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].adf` | `6a322665` | disk 2, in DF1 |

- Unzipped into `games/lemmings/build/adf/` and given to `tools/emulator.py` as `--df0` and
  `--df1`.
- 2026-10-06: `tools/capture.py --game lemmings --script games/lemmings/scripts/boot.dbg` ran
  from cold boot to the title menu twice, and took the references listed in
  `docs/emulator.md`, "References".
- Disk 1 boots a Skid Row cracktro. It waits for the left mouse button, then the game runs:
  "Expansion RAM detected & utilised", the Psygnosis owl, DMA Design, the intro, then the
  title menu, with no disk prompt.
- Disk 2's bootblock checksum is not valid, so it is not bootable. It is a data disk.

## Others on hand

Every Disk 1 below was booted for 26 s in the oracle on 2026-10-06, with screenshots at 8, 16
and 26 s. Each one shows a cracker's intro or a trainer menu before the game. None of the images
on hand boots straight into the game.

| Name as distributed | CRC-32 | Seen before the game |
| --- | --- | --- |
| `(Disk 1 of 2)[cr SR][f AGA Ministry]` | `1a8d5a98` | Skid Row cracktro |
| `(Disk 1 of 2)[cr SR][f AGA Ministry][t +10 PNS - BTL]` | `c38c41e1` | Skid Row cracktro |
| `(Disk 1 of 2)[cr SR][f mem configs SR]` | `944dfccf` | Skid Row cracktro |
| `(Disk 1 of 2)[cr SR][f mem configs SR][t +10 PNS - BTL]` | `73256a54` | Skid Row cracktro |
| `(Disk 1 of 2)[cr SR][f mem configs SR][t +28 Defjam]` | `b188739a` | Skid Row cracktro |
| `(Disk 1 of 2)[cr SR][t +10 PNS - BTL]` | `e02352da` | Skid Row cracktro |
| `(Disk 1 of 2)[cr SR][t +10 PNS - BTL][b dump]` | `8b28e9b7` | not booted: a bad dump by its name |
| `(Disk 1 of 2)[cr SR][t +20 Slipstream]` | `0f34b0b1` | Slipstream trainer menu |
| `(Disk 1 of 2)[cr SR][t +28 Defjam]` | `228e4b14` | Skid Row cracktro |
| `(Disk 1 of 2)[cr][h Semtex][t +10 Goonies]` | `52ca2320` | Semtex text scroller, still on screen after 90 s |
| `(Disk 1 of 2)[cr][t +10 Goonies]` | `85ac8c46` | Goonies trainer menu |
| `(NTSC)(Disk 1 of 2)[cr Melnok]` | `0199897b` | Melnok menu, "Play the game" |
| `(Disk 2 of 2)[cr SR][b errdms]` | `206cadfc` | differs from `6a322665` in 11 231 bytes, all in tracks 156–157 |
| `(Disk 2 of 2)[cr SR][v Lamer Exterminator 2]` | `db1d8ad5` | not booted: carries a virus by its name |
| `(NTSC)(Disk 2 of 2)[cr Melnok]` | `e6a68967` | the Melnok disk 1's partner |

The names are shortened from `Lemmings (1991)(Psygnosis)…zip`. All the Disk 1 images start
`DOS\0`, have a valid bootblock checksum and hold no OFS root block at block 880, so the game
loads by its own trackloader.
