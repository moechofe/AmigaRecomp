#!/usr/bin/env python3
"""Print the GameAnchor initialiser of a range of a RAM dump: {addr, len, CRC-32}."""

import argparse
import sys
import zlib


def number(text):
    try:
        return int(text, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a number: {text}")


def anchor(dump, addr, length, base):
    """Return the initialiser line; ValueError when the range is outside the dump."""
    start = addr - base
    if start < 0 or start + length > len(dump):
        raise ValueError
    return "{0x%06Xu, %d, 0x%08Xu}" % (addr, length, zlib.crc32(dump[start:start + length]))


def main():
    ap = argparse.ArgumentParser(
        description="Print `{addr, len, crc}` for a GameAnchor: the CRC-32 (zlib) of len bytes at guest "
        "address addr of a RAM dump, such as the one `--dump` writes.")
    ap.add_argument("dump", metavar="ramdump")
    ap.add_argument("--addr", type=number, required=True, help="guest address, decimal or 0x hex")
    ap.add_argument("--len", type=number, required=True, dest="length", help="byte count, at least 1")
    ap.add_argument("--base", type=number, default=0,
                    help="guest address of the dump's first byte (default 0, which is what --dump writes)")
    ns = ap.parse_args()
    if ns.length < 1:
        ap.error("--len must be at least 1")
    try:
        with open(ns.dump, "rb") as f:
            dump = f.read()
    except OSError as e:
        print(f"error: {ns.dump}: {e.strerror or e}", file=sys.stderr)
        sys.exit(1)
    try:
        print(anchor(dump, ns.addr, ns.length, ns.base))
    except ValueError:
        print(f"error: 0x{ns.addr:X} + {ns.length} is outside {ns.dump} (base 0x{ns.base:X}, {len(dump)} bytes)",
              file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
