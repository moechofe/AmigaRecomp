#!/usr/bin/env python3
"""Identify Amiga disk images: CRC-32, bootblock and the dumps.txt lines that match."""

import argparse
import gzip
import re
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADF_SIZE = 901120
CONTAINER_SUFFIXES = (".adf", ".adz", ".gz")
LINE = re.compile(r"^([0-9A-Fa-f]{8})[ \t]+([a-z0-9_]+)[ \t]+(verified|known|refused)[ \t]+(.*[^ \t])$")


class DumpsError(Exception):
    pass


def parse_dumps(root):
    """Return {crc: [(game id, crc, role, status, name)]} over games/*/dumps.txt."""
    table = {}
    for path in sorted((root / "games").glob("*/dumps.txt")):
        game = path.parent.name
        for raw in path.read_text(encoding="utf-8").splitlines():
            text = raw.split("#", 1)[0].strip()
            if not text:
                continue
            m = LINE.match(text)
            if not m:
                raise DumpsError(f"games/{game}/dumps.txt: cannot parse `{text}`")
            crc = m.group(1).lower()
            table.setdefault(crc, []).append((game, crc, m.group(2), m.group(3), m.group(4)))
    return table


def candidates(path):
    """Yield (source, base name, decompressed bytes) for one path argument."""
    ext = path.suffix.lower()
    if ext == ".adf":
        yield str(path), path.stem, path.read_bytes()
    elif ext in (".adz", ".gz"):
        yield str(path), path.stem, gzip.decompress(path.read_bytes())
    elif ext == ".zip":
        with zipfile.ZipFile(path) as zf:
            for info in zf.infolist():
                if info.is_dir() or not info.filename.lower().endswith(CONTAINER_SUFFIXES):
                    continue
                name = info.filename.rsplit("/", 1)[-1]
                stem = name.rsplit(".", 1)[0]
                data = zf.read(info)
                if name.lower().endswith((".adz", ".gz")):
                    data = gzip.decompress(data)
                yield f"{path}:{info.filename}", stem, data
    else:
        raise ValueError(f"unsupported extension `{path.suffix}`")


def boot_name(data):
    if data[:3] == b"DOS":
        return f"DOS{data[3]}"
    return "".join(chr(b) if 32 <= b < 127 else f"\\x{b:02x}" for b in data[:4])


def boot_sum_ok(data):
    total = 0
    for i in range(0, 1024, 4):
        total += int.from_bytes(data[i:i + 4], "big")
        if total > 0xFFFFFFFF:
            total = (total & 0xFFFFFFFF) + 1
    return total == 0xFFFFFFFF


def describe(source, stem, data, table):
    if len(data) != ADF_SIZE:
        return ("", b"", source, f"--------  not an 880 KB double-density ADF ({len(data)} bytes)  {source}")
    crc = f"{zlib.crc32(data):08x}"
    boot = boot_name(data)
    state = "sum-ok" if boot_sum_ok(data) else "sum-bad"
    lines = [f"{crc}  {boot}  {state}  {source}"]
    matches = table.get(crc)
    if matches:
        for game, c, role, status, name in matches:
            lines.append(f"    {game}: {c} {role} {status} {name}")
    else:
        lines.append(f"    not in any dumps.txt: {crc} ROLE known {stem}")
    return (crc, data[:4], source, "\n".join(lines))


def expand_args(args):
    paths = []
    for arg in args:
        p = Path(arg)
        if p.is_dir():
            paths.extend(sorted(c for c in p.iterdir() if c.is_file()))
        else:
            paths.append(p)
    return paths


def run(args, root):
    try:
        table = parse_dumps(root)
    except (DumpsError, OSError, UnicodeDecodeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    ok = True
    results = []
    for path in expand_args(args):
        try:
            for source, stem, data in candidates(path):
                results.append(describe(source, stem, data, table))
        except (OSError, ValueError, EOFError, zlib.error, zipfile.BadZipFile, NotImplementedError) as e:
            print(f"error: {path}: {e}", file=sys.stderr)
            ok = False
    results.sort(key=lambda r: (r[1], r[0], r[2]))
    for r in results:
        print(r[3])
        if r[0] == "":
            ok = False
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(
        description="Group Amiga disk images by CRC-32 and show their bootblock and the "
        "dumps.txt lines that match. Read-only. Accepts .adf, .adz, .gz and .zip.")
    ap.add_argument("paths", nargs="+", metavar="path",
                    help="image, container or directory (its files, non-recursively)")
    ns = ap.parse_args()
    sys.exit(run(ns.paths, ROOT))


if __name__ == "__main__":
    main()
