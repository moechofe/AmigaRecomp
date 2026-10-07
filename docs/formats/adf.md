# ADF disk images

An ADF is a sector-for-sector dump of an Amiga floppy. `tools/adf.py` identifies images and
`docs/game.md` item 5 owns the rules by which the executable accepts them. The field offsets
below come from the ADF FAQ; a statement marked "derived" is not written in the FAQ.

## Geometry

A double-density disk has 80 cylinders, 2 heads, 11 sectors per track and 512 bytes per sector:
80 x 2 x 11 x 512 = 901 120 bytes. Blocks run in increasing sector, then side, then cylinder:

    block = (cylinder x 2 + head) x 11 + sector

There are 1760 blocks. The root block is block 880. All multi-byte integers are big-endian.

## Bootblock

Blocks 0 and 1 (1024 bytes) form the bootblock.

| Offset | Size | Field |
| --- | --- | --- |
| +0 | 4 | `D`, `O`, `S`, then the flags byte |
| +4 | 4 | checksum |
| +8 | 4 | root block number, 880 |
| +12 | 1012 | boot code, optional |

The three low bits of the flags byte are: bit 0 set for FFS, clear for OFS; bit 1 set for INTL;
bit 2 set for DIRCACHE. The FAQ adds that DIRCACHE implies the international mode while bit 1
stays clear, so the valid values are 0 to 5.

**Checksum.** With the checksum field zeroed, add the 256 big-endian longwords of the 1024
bytes. A carry out of bit 32 is added back into the sum (end-around carry). The stored checksum
is the complement of that sum. Derived: with the stored checksum in place, a valid bootblock
therefore sums to 0xFFFFFFFF, which is the test `tools/adf.py` reports as `sum-ok`.

## OFS and FFS root block

| Offset | Size | Field |
| --- | --- | --- |
| +0 | 4 | primary type, 2 |
| +20 | 4 | block checksum |
| +432 | 1 | volume name length |
| +433 | 30 | volume name characters |
| +508 | 4 | secondary type, 1 |

The block checksum is the negated sum of the 128 longwords of the block, computed with the
checksum field zeroed; it is not the end-around-carry rule of the bootblock. The FAQ gives the
name as a length byte followed by the characters, and does not say how the name is encoded.

## Containers

- `.adf` is the raw image.
- `.adz` and `.gz` are a gzip stream (RFC 1952) holding an ADF.
- `.zip` follows the PKWARE APPNOTE; its members are ADFs or gzipped ADFs.

Which extensions the executable accepts, how deep it looks into a ZIP and what size it requires
are in `docs/game.md` item 5.

## Sources

- Laurent Clevy, "The .ADF (Amiga Disk File) format FAQ", v1.14:
  <http://lclevy.free.fr/adflib/adf_info.html>. Sections 3 (geometry), 4.1 (bootblock),
  4.2 (root block) and 4.2.3 (block checksum).
- RFC 1952, "GZIP file format specification version 4.3".
- PKWARE, ".ZIP File Format Specification" (APPNOTE.TXT).
