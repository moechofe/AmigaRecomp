# Deuteros dumps

Evidence for each dump seen. CRC-32 is zlib's over the 901 120 bytes of the image, as it
arrived. The images sit in `games/deuteros/disks/`, 28 ZIPs that hold 19 distinct CRCs. `dumps.txt`
owns each status; this file owns the evidence. Dates are 2026-10-08, Linux, Release build
`games/deuteros/build/cmake-linux`, every pair run from its ZIPs with
`--pref games/deuteros/build/m5-pref` (`README.md`, section 9).

The stage 2 CRC is `amiga_crc32` over ADF 0x2C00-0x41FF, the anchor `deuteros_identify_disk1`
checks (`docs/format.md`, "Identification"). A dump with a stage 2 CRC that is listed there
runs, as an unknown dump, even when `dumps.txt` does not list it. The Disk 2 bootblock CRC is
`e58d146a` for all five Disk 2 images.

The scripts are in `games/deuteros/scripts/`. `menu.input` and `earth-base.input` are the M2
scripts; the `-intro` variants first wait 200 frames and click, to cross a cracker's intro or
trainer screen that waits for the left button; `earth-base-black-monks.input` is the Black
Monks sequence. Figures are `tools/pixdiff.py` against `reference/menu-nosprite.png` and
`reference/earth-base-nosprite.png`. A run is a pass when it exits 0 and prints no `fatal:`.

## Disk 1

| Name as distributed | CRC-32 | Stage 2 CRC | Status | Evidence |
| --- | --- | --- | --- | --- |
| `(M3)(Disk 1 of 2)`, `(M3)(Disk 1 of 2)[cr SR][f AGA Access]` | `e55363d8` | `0fa5e766` | verified | `menu`: 81 920/81 920 EXACT. `earth-base`: 81 862/81 920, differences inside x 13-23, y 79-90 (the M2 figures). Opens on the title: 9 colours at frame 200. |
| `(M3)(Disk 1 of 2)[cr SKR]`, `v1 (M3)(Disk 1 of 2)[cr SR]` | `2aac7cc2` | `0fa5e766` | verified | Skid Row cracktro, 229 colours at frame 200. `menu-intro`: 81 920/81 920 EXACT. `earth-base-intro`: 81 405/81 920, differences inside x 9-231, y 19-90: the version-string line (y 19-25) and the icon slot. |
| `v1 (M3)(Disk 1 of 2)[cr SR][f crack IMG]` | `e70eaab9` | `0fa5e766` | verified | Skid Row cracktro, 229 colours at frame 200. `menu-intro` EXACT; `earth-base-intro` 81 405/81 920, same rectangle as `2aac7cc2`. |
| `v1 (M3)(Disk 1 of 2)[cr SR][f crack L.A.]` | `909a6899` | `0fa5e766` | verified | Skid Row cracktro, 229 colours at frame 200. `menu-intro` EXACT; `earth-base-intro` 81 405/81 920, same rectangle. Pair: `ab32e237`. |
| `(M3)[a](Disk 1 of 2)`, `v1 (M3)(Disk 1 of 2)[cr SR][f IMG][a cracktro disabled]` | `8cf442c6` | `0fa5e766` | verified | Opens on the title, 9 colours at frame 200. `menu` EXACT; `earth-base` 81 405/81 920, differences inside x 9-231, y 19-90. Pair: `c137ff44`. |
| `(M3)[a2](Disk 1 of 2)`, `v1 (1991-09)(M3)(Disk 1 of 2)[cr SR]` | `d5e00a83` | `a2fb4c87` | verified | Opens on the title, 8 colours at frame 200. `menu` EXACT; `earth-base` 81 429/81 920, differences inside x 9-231, y 19-90. Pair: `03c1f83a`. |
| `v1 (M3)(Disk 1 of 2)[cr SR][f crack SKN][t +5 SKN]` | `fa95b328` | `36063ce7` | verified | SKN trainer screen, 23 colours at frame 200. `menu-intro` EXACT; `earth-base-intro` 81 405/81 920, differences inside x 9-231, y 19-90. |
| `v1 (M3)(Disk 1 of 2)[cr SR][f crack SKN][t +6 SKN][tr es SKN]` | `f1c4eed4` | `36063ce7` | known | SKN trainer screen, 23 colours at frame 200, Spanish text. `menu-intro`: 81 425/81 920, every difference inside x 121-207, y 95-116, where the credit line "Castellano Madrid of Skillion" sits. `earth-base-intro`: 81 270/81 920, differences inside x 9-311, y 19-90. Pair: `d34d9626`. |
| `v1 (M3)(Disk 1 of 2)[cr SR][f crack SKN][t +6 SKN][tr es SKN][b doscopy]` | `d6fd242b` | `36063ce7` | known | The same figures as `f1c4eed4`, both scripts. |
| `(Activision)[cr Black Monks]`, `(M3)(Disk 1 of 2)[cr Black Monks]` | `7dbf36e9` | `db749c89` | known | See "Black Monks". `earth-base-black-monks`: 81 450/81 920, differences inside x 9-215, y 19-89. |
| `(Activision)[cr Black Monks][b doscopy]`, `(M3)(Disk 1 of 2)[cr Black Monks][a]` | `4d1b0183` | `db749c89` | known | The same figures as `7dbf36e9`. |
| `(Activision)[cr Black Monks][m baddump][b doscopy]` | `3f8f1030` | `db749c89` | known | The same figures as `7dbf36e9`, although its name says `[m baddump]`: it reached the Earth base. |

Names are shortened from `Deuteros - The Next Millennium …zip`. Every Disk 1 starts `DOS\0` and
holds no OFS root block: the game loads by its own trackloader.

## Disk 2

| Name as distributed | CRC-32 | Status | Evidence |
| --- | --- | --- | --- |
| `(M3)(Disk 2 of 2)`, `v1 (M3)(Disk 2 of 2)(Data Disk)` | `56f55f5b` | verified | The pair of `e55363d8`, `2aac7cc2`, `e70eaab9`, `fa95b328` and, for Black Monks, all three. |
| `(M3)[a2](Disk 2 of 2)`, `v1 (1991-09)(M3)(Disk 2 of 2)(Data Disk)` | `03c1f83a` | verified | The pair of `d5e00a83`. |
| `(M3)[a](Disk 2 of 2)`, `v1 (M3)(Disk 2 of 2)(Data Disk)[b dump]` | `c137ff44` | verified | The pair of `8cf442c6`. |
| `v1 (M3)(Disk 2 of 2)(Data Disk)[f crack L.A.]` | `ab32e237` | verified | The pair of `909a6899`. |
| `v1 (M3)(Disk 2 of 2)(Data Disk)[t +6 SKN][tr es SKN]` | `d34d9626` | known | The pair of `f1c4eed4` and `d6fd242b`: it carries the Spanish text. |

A Disk 2 is `verified` when a pair that includes it passes the gate: both runs exit 0 without
`fatal:`, the menu shot is 81 920/81 920 and the Earth-base shot is written. The Spanish pair is
`known`: the menu differs by its own translated credit line, and there is no Spanish reference.

## Black Monks

Loader family B: stage 2 CRC `db749c89`. Disk 1 shows the title, and after a click a black
screen, 5 colours, that reads "Cracked by Black Monks!" with a text cursor; there is no language
menu and no data-disk prompt. Typed keys are echoed. Return, with or without typed characters
before it, starts the game on the Earth base: observed on the oracle (FS-UAE 3.1.66, Kickstart
1.3, driven by `tools/capture.py`) and on the runtime, where the same Return first ended in a
jump into garbage. Two gaps in `core/` caused that:

- `input.device` `CMD_RESET` (1) was fatal (`docs/runtime.md`, "input.device").
- `InitRastPort` cleared all 0x64 bytes of the RastPort. The loader places a ColorMap at
  RastPort + 0x56 and builds it before the call, so its ColorTable pointer became 0 and the next
  `LoadRGB4` wrote the title palette to addresses 0-0x27, including `ExecBase` at 4. The first
  `AddIntServer` that followed jumped through it, to 0x078504BB. The oracle's library routine
  clears 0x4E bytes (`docs/runtime.md`, "Observed graphics.library 34 / Kickstart 1.3
  behaviour"), and the HLE does the same now.

The Black Monks dumps have no language menu, so there is no `menu` shot to compare. They are
`known`, not `verified`: the gate asks for the menu at 81 920/81 920. The hacks were not run on
them.

## The save-disk images

`(M3)[save disk]` (`7c78a8c4`) and `(M3)[a][save disk]` (`48fd1979`) are 901 120 bytes, the size
of an ADF, and start `MSCF`: a Microsoft Cabinet file (`Connect.cnt`), not an Amiga disk. They
are `refused` in `dumps.txt`, with the reason as the message. `deuteros` given either one,
headless, prints that message and exits 2. The proof found the same
(`../proof/doc.format/data.md`, "What the build checks").
