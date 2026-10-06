#!/usr/bin/env python3
"""Compare two 320x256 images pixel for pixel, and read or write a PNG.

This is the project's verification #1 (docs/conventions.md): a `<game> --headless` shot
against a WinUAE screenshot of the same state.

    python tools/pixdiff.py --game <id> a.png b.png           how many pixels agree, and where
    python tools/pixdiff.py --game <id> a.png b.png --search  also try every alignment
    python tools/pixdiff.py --game <id> shot.png --colours    the distinct colours in one image

`read_png` is also a library: capture.py reads WinUAE's screenshots with it.

A screenshot is a measurement instrument, not a picture: exactly 320x256, unscaled,
unfiltered, lossless (docs/emulator.md, "Capture window"). An image that is not those things
will diff against everything, so the size is reported and never assumed.

Read-only apart from the diff image, which goes under games/<id>/build/pixdiff/.
"""

from __future__ import annotations

import argparse
import collections
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FILTERS = 5


class Image:
    """Width, height and one RGB triple per pixel. The one form everything is compared in."""

    def __init__(self, width: int, height: int, rgb: bytes, source: str = ""):
        self.width, self.height, self.rgb, self.source = width, height, rgb, source

    def pixel(self, x: int, y: int) -> bytes:
        off = (y * self.width + x) * 3
        return self.rgb[off:off + 3]

    def colours(self) -> collections.Counter:
        return collections.Counter(self.rgb[i:i + 3] for i in range(0, len(self.rgb), 3))


def unfilter(raw: bytes, width: int, height: int, bpp: int, stride: int) -> bytearray:
    """Undo the five PNG row filters. Straight out of the spec; there is no clever version."""
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _y in range(height):
        kind = raw[pos]
        pos += 1
        if kind >= FILTERS:
            raise ValueError(f"unknown PNG row filter {kind}")
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                near = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + near) & 0xFF
        out += line
        prev = line
    return out


def unpack_bits(line: bytes, width: int, depth: int) -> list[int]:
    """Sub-byte greyscale or palette samples, MSB first, as the spec puts them."""
    per = 8 // depth
    mask = (1 << depth) - 1
    return [(line[i // per] >> (8 - depth * (i % per + 1))) & mask for i in range(width)]


def read_png(path: Path) -> Image:
    """Any non-interlaced 8-bit-or-less PNG, returned as RGB. Interlaced is refused, loudly."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path.name} is not a PNG")
    pos, idat, header, plte, trns = 8, [], None, b"", b""
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            plte = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
        pos += 12 + length
    if header is None:
        raise ValueError(f"{path.name} has no IHDR")
    width, height, depth, colour, compression, filt, interlace = header
    if interlace:
        raise ValueError(f"{path.name} is Adam7 interlaced; this tool reads non-interlaced "
                         f"PNGs only. Re-save it, or teach this function Adam7.")
    if compression or filt:
        raise ValueError(f"{path.name} uses a compression or filter method PNG does not define")
    del trns                                  # alpha is dropped; see the composite below

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    bpp = max(1, channels * depth // 8)
    stride = (width * channels * depth + 7) // 8
    rows = unfilter(zlib.decompress(b"".join(idat)), width, height, bpp, stride)

    rgb = bytearray()
    for y in range(height):
        line = rows[y * stride:(y + 1) * stride]
        if depth < 8:
            samples = unpack_bits(line, width * channels, depth)
        elif depth == 16:
            samples = list(line[0::2])        # high byte; the low one cannot survive an 8-bit diff
        else:
            samples = list(line)
        for x in range(width):
            s = samples[x * channels:(x + 1) * channels]
            if colour == 3:
                index = s[0]
                rgb += plte[index * 3:index * 3 + 3] or b"\x00\x00\x00"
            elif colour in (0, 4):
                value = s[0] * (255 // ((1 << depth) - 1)) if depth < 8 else s[0]
                rgb += bytes((value, value, value))
            else:
                rgb += bytes(s[:3])
    return Image(width, height, bytes(rgb), path.name)


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    """Raw RGB triples, 8 bits a channel. Hand-rolled, stdlib only -- conventions.md, 'Python in tool.dev'."""
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(tag: bytes, body: bytes) -> bytes:
        data = tag + body
        return struct.pack(">I", len(body)) + data + struct.pack(">I", zlib.crc32(data))

    out = [b"\x89PNG\r\n\x1a\n",
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))]
    out.append(chunk(b"IDAT", zlib.compress(raw, 9)))
    out.append(chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(out))


def background(image: Image) -> bytes:
    """The commonest colour. Measured, not assumed to be black -- see `compare`."""
    return image.colours().most_common(1)[0][0]


def compare(a: Image, b: Image, dx: int = 0, dy: int = 0) -> dict:
    """Pixel a(x, y) against pixel b(x + dx, y + dy), over the overlap of the two.

    Two counts come back, and the second is the one that means anything. A game screen is
    mostly background, so `share` sits above 99 % for alignments that are plainly wrong.
    `informative` counts only positions where at least one of the two images is not its own
    background colour, which is where alignment information actually lives.
    """
    a_bg, b_bg = background(a), background(b)
    agree = total = ink_agree = ink_total = 0
    wrong: collections.Counter = collections.Counter()
    rows: collections.Counter = collections.Counter()
    box = None
    for y in range(a.height):
        by = y + dy
        if not 0 <= by < b.height:
            continue
        for x in range(a.width):
            bx = x + dx
            if not 0 <= bx < b.width:
                continue
            total += 1
            want, got = a.pixel(x, y), b.pixel(bx, by)
            informative = want != a_bg or got != b_bg
            ink_total += informative
            if want == got:
                agree += 1
                ink_agree += informative
            else:
                wrong[(want, got)] += 1
                rows[y] += 1
                box = (x, y, x, y) if box is None else (
                    min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    return {"agree": agree, "total": total, "dx": dx, "dy": dy,
            "share": agree / total if total else 0.0,
            "ink_agree": ink_agree, "ink_total": ink_total,
            "ink_share": ink_agree / ink_total if ink_total else 0.0,
            "wrong": wrong, "rows": rows, "box": box}


def best_offset(a: Image, b: Image, reach: int = 64, sideways: int = 16,
                step: int = 3) -> dict:
    """The alignment that agrees most, sampled coarsely then confirmed in full.

    A decoded bitmap may be smaller than a screenshot, so where the picture sits inside the
    captured frame is a measurement, not something to read off DIWSTRT. Horizontal counts too:
    BPLCON1 delays a playfield by up to 15 pixels, so a decode can be right and still be a few
    pixels off the glass.

    Ranked on **informative pixels only** -- positions where at least one image is not its own
    background. The share picks the alignment with the smallest overlap, and the raw agreement
    count picks whatever maximises background-on-background. Sampling is by ink too, or a
    coarse grid steps straight over a sparse screen.
    """
    a_bg, b_bg = background(a), background(b)
    ink = [(x, y) for y in range(0, a.height, step) for x in range(a.width)
           if a.pixel(x, y) != a_bg]
    if not ink:
        ink = [(x, y) for y in range(0, a.height, step) for x in range(0, a.width, step)]
    best = None
    for dy in range(-reach, reach + 1):
        for dx in range(-sideways, sideways + 1):
            hit = 0
            for x, y in ink:
                bx, by = x + dx, y + dy
                if 0 <= bx < b.width and 0 <= by < b.height and a.pixel(x, y) == b.pixel(bx, by):
                    hit += 1
            if best is None or hit > best[0]:
                best = (hit, dx, dy)
    del b_bg
    return compare(a, b, best[1], best[2]) if best else compare(a, b)


def diff_image(a: Image, b: Image, result: dict) -> tuple[int, int, bytes]:
    """Agreeing pixels dimmed to a quarter, disagreeing ones flat magenta."""
    px = bytearray()
    for y in range(a.height):
        by = y + result["dy"]
        for x in range(a.width):
            bx = x + result["dx"]
            if not (0 <= by < b.height and 0 <= bx < b.width):
                px += b"\x00\x00\x00"
                continue
            want = a.pixel(x, y)
            px += bytes(c // 4 for c in want) if want == b.pixel(bx, by) else b"\xff\x00\xff"
    return a.width, a.height, bytes(px)


def show_colours(image: Image) -> None:
    counts = image.colours()
    print(f"  {image.width}x{image.height}, {len(counts)} distinct colours")
    print("  An OCS colour is 12-bit, so every one of these should expand from $0RGB by")
    print("  nibble replication. A colour that does not is a scaled or filtered capture and")
    print("  cannot be diffed -- docs/emulator.md, 'Capture window'.")
    for (r, g, b), n in counts.most_common():
        ocs = (r >> 4, g >> 4, b >> 4)
        exact = (r, g, b) == tuple(c * 17 for c in ocs)
        print(f"    #{r:02X}{g:02X}{b:02X}  {n:6d} px   ${ocs[0]:X}{ocs[1]:X}{ocs[2]:X}"
              f"{'' if exact else '   ** NOT a 12-bit colour **'}")


def report(result: dict, a: Image, b: Image, top: int = 8) -> None:
    print(f"  {a.source} vs {b.source}")
    if (a.width, a.height) != (b.width, b.height):
        print(f"  sizes differ: {a.width}x{a.height} vs {b.width}x{b.height}; comparing the "
              f"overlap")
    offset = "" if not result["dy"] and not result["dx"] else \
        f"  at offset dx={result['dx']}, dy={result['dy']}"
    print(f"  {result['agree']} of {result['total']} pixels agree = "
          f"{result['share'] * 100:.3f} %{offset}")
    if not result["wrong"]:
        print("  EXACT")
        return
    print(f"  {len(result['wrong'])} distinct disagreements; the commonest:")
    for (want, got), n in result["wrong"].most_common(top):
        print(f"    #{want[0]:02X}{want[1]:02X}{want[2]:02X} where the other has "
              f"#{got[0]:02X}{got[1]:02X}{got[2]:02X}   {n} px")
    worst = result["rows"].most_common(top)
    print("  worst rows: " + ", ".join(f"y={y} ({n})" for y, n in sorted(worst)))
    x0, y0, x1, y1 = result["box"]
    print(f"  differences inside x {x0}\u2013{x1}, y {y0}\u2013{y1}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare two images pixel for pixel; the project's verification #1.",
        epilog="docs/conventions.md, 'Verification'; docs/tools.md.")
    parser.add_argument("--game", required=True, help="the game folder under games/")
    parser.add_argument("images", nargs="+", type=Path, help="one PNG with --colours, else two")
    parser.add_argument("--colours", action="store_true",
                        help="list the distinct colours of one image and stop")
    parser.add_argument("--search", action="store_true",
                        help="try every vertical alignment and report the best")
    parser.add_argument("--dx", type=int, default=0, help="shift the second image (default 0)")
    parser.add_argument("--dy", type=int, default=0, help="shift the second image (default 0)")
    parser.add_argument("--diff", type=Path,
                        help="write a difference PNG here (default games/<id>/build/pixdiff/diff.png)")
    parser.add_argument("--no-diff", action="store_true", help="do not write a difference PNG")
    args = parser.parse_args()
    if not (ROOT / "games" / args.game).is_dir():
        print(f"no games/{args.game}/", file=sys.stderr)
        return 2
    out = ROOT / "games" / args.game / "build" / "pixdiff"

    try:
        images = [read_png(p) for p in args.images]
    except (OSError, ValueError, zlib.error) as exc:
        print(f"cannot read -- {exc}")
        return 1

    if args.colours:
        for image in images:
            print(f"\n=== {image.source} ===")
            show_colours(image)
        print()
        return 0

    if len(images) != 2:
        parser.error("give two images to compare, or one with --colours")

    a, b = images
    print("\n=== pixel diff ===")
    result = best_offset(a, b) if args.search else compare(a, b, args.dx, args.dy)
    report(result, a, b)

    if not args.no_diff:
        path = args.diff or (out / "diff.png")
        width, height, px = diff_image(a, b, result)
        path.parent.mkdir(parents=True, exist_ok=True)
        write_png(path, width, height, px)
        print(f"\n  wrote {path.relative_to(ROOT) if ROOT in path.parents else path} -- "
              f"agreeing pixels dimmed, disagreeing ones magenta. Gitignored; look at it.")
    print()
    return 0 if result["total"] and result["agree"] == result["total"] else 1


if __name__ == "__main__":
    sys.exit(main())
