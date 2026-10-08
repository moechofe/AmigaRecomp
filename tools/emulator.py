#!/usr/bin/env python3
"""Generate an oracle emulator configuration for one game from its template.

The template games/<id>/oracle.<emulator>.in holds every setting that can move a pixel and
is version-controlled. The paths it needs (Kickstart ROM, floppy images) are machine
specific, so the runnable config is generated into games/<id>/build/<emulator>/:
oracle.uae for WinUAE, oracle.fs-uae for FS-UAE. Edit the template; never edit the generated
file.

Placeholders: @KICKSTART@ and @DF0@ .. @DF3@, each needing its option (--kickstart,
--df0 .. --df3), and @BUILD@, the absolute build/<emulator>/ folder, which is always known.
Without --kickstart the Kickstart 1.3 ROM is searched for by content hash.

    python tools/emulator.py --game <id> --df0 DISK1.adf --df1 DISK2.adf
    python tools/emulator.py --game <id> --emulator fsuae --df0 DISK1.adf --df1 DISK2.adf --print
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent

# Kickstart 1.3 rev 34.5 (A500/A1000/A2000/CDTV). Identity by content hash.
KS13_MD5 = "82a21c1890cae844b3df741f2762d48d"
KS13_BYTES = 262144
KICKSTART_DIRS = (r"C:\Users\Public\Documents\Amiga Files\WinUAE\Kickstarts",
                  "~/Documents/WinUAE/Kickstarts", "~/Documents/FS-UAE/Kickstarts")

# WinUAE does not put itself on PATH; printed launch commands use absolute paths.
WINUAE_EXES = (
    r"C:\Program Files\WinUAE\winuae64.exe",
    r"C:\Program Files\WinUAE\winuae.exe",
)

PLACEHOLDER = re.compile(r"@([A-Z0-9_]+)@")
OPTIONS = {"KICKSTART": "--kickstart", **{f"DF{n}": f"--df{n}" for n in range(4)}}
AUTOMATIC = {"BUILD"}

FSUAE_FLATPAK = "net.fsuae.FS-UAE"


class Emulator(NamedTuple):
    template: str            # file under games/<id>/
    folder: str              # folder under games/<id>/build/
    config: str              # generated file name inside that folder
    dirs: tuple[str, ...]    # folders under build/<folder>/ the config names; created with it


EMULATORS = {
    "winuae": Emulator("oracle.winuae.in", "winuae", "oracle.uae", ()),
    "fsuae": Emulator("oracle.fsuae.in", "fsuae", "oracle.fs-uae", ("base", "logs", "states", "shots")),
}
DEFAULT_EMULATOR = "winuae" if sys.platform == "win32" else "fsuae"


def config_path(game, name):
    """games/<id>/build/<folder>/<config>, relative to ROOT."""
    e = EMULATORS[name]
    return Path("games") / game / "build" / e.folder / e.config


def fsuae_command():
    """argv that starts FS-UAE, without the config: the binary on PATH, else the Flatpak.
    None when neither is installed."""
    exe = shutil.which("fs-uae")
    if exe:
        return [exe]
    flatpak = shutil.which("flatpak")
    if flatpak and subprocess.run([flatpak, "info", FSUAE_FLATPAK],
                                  capture_output=True).returncode == 0:
        return [flatpak, "run", "--nosocket=wayland", "--env=SDL_VIDEODRIVER=x11", FSUAE_FLATPAK]
    return None


def fail(problem, fix=""):
    print(f"error: {problem}", file=sys.stderr)
    if fix:
        print(f"  fix: {fix}", file=sys.stderr)
    raise SystemExit(2)


def md5_of(path):
    return hashlib.md5(path.read_bytes()).hexdigest()


def find_kickstart():
    """Locate Kickstart 1.3 by content hash. Returns a Path, or None."""
    for raw in KICKSTART_DIRS:
        folder = Path(os.path.expanduser(os.path.expandvars(raw)))
        if not folder.is_dir():
            continue
        for rom in folder.rglob("*.rom"):
            try:
                if rom.stat().st_size != KS13_BYTES:
                    continue
                digest = md5_of(rom)
            except OSError:
                continue
            if digest == KS13_MD5:
                return rom
    return None


def render(template, values):
    """Substitute @NAME@ placeholders, refusing to emit a half-applied config."""
    text = template.read_text(encoding="utf-8")
    names = {m.group(1) for m in PLACEHOLDER.finditer(text)}
    unknown = sorted(names - set(OPTIONS) - AUTOMATIC)
    if unknown:
        fail(f"{template.name} uses placeholders this tool cannot fill: "
             + ", ".join(f"@{u}@" for u in unknown))
    for name in sorted(names - AUTOMATIC):
        if name not in values:
            fail(f"{template.name} uses @{name}@ but {OPTIONS[name]} was not given")
    for name in names:
        text = text.replace(f"@{name}@", str(values[name]))
    return text


def launch_command(config, name):
    if name == "fsuae":
        argv = fsuae_command()
        return " ".join(f'"{a}"' for a in argv) + f' "{config}"' if argv else None
    for raw in WINUAE_EXES:
        path = Path(os.path.expandvars(raw))
        if path.is_file():
            return f'& "{path}" -f "{config}"'
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", required=True, metavar="ID", help="game folder under games/")
    for n in range(4):
        parser.add_argument(f"--df{n}", metavar="PATH", help=f"floppy image for DF{n}")
    parser.add_argument("--kickstart", metavar="PATH",
                        help="Kickstart 1.3 ROM (default: search by content hash)")
    parser.add_argument("--print", action="store_true", dest="to_stdout",
                        help="write nothing; print the rendered config instead")
    parser.add_argument("--emulator", choices=sorted(EMULATORS), default=DEFAULT_EMULATOR,
                        help="oracle emulator (default: winuae on Windows, fsuae elsewhere)")
    args = parser.parse_args()

    game = ROOT / "games" / args.game
    if not game.is_dir():
        fail(f"no games/{args.game}/")
    template = game / EMULATORS[args.emulator].template
    if not template.is_file():
        fail(f"missing template {template.relative_to(ROOT).as_posix()}")

    out_rel = config_path(args.game, args.emulator)
    values = {"BUILD": ROOT / out_rel.parent}
    for n in range(4):
        disk = getattr(args, f"df{n}")
        if disk:
            values[f"DF{n}"] = Path(disk).resolve()

    warning = None
    if args.kickstart:
        rom = Path(args.kickstart).resolve()
        if not rom.is_file():
            fail(f"{args.kickstart} is not a file")
        if md5_of(rom) != KS13_MD5:
            warning = f"[!!]  {rom} is not Kickstart 1.3 rev 34.5"
    else:
        rom = find_kickstart()
    if rom is not None:
        values["KICKSTART"] = rom
    elif "@KICKSTART@" in template.read_text(encoding="utf-8"):
        fail(
            f"no Kickstart 1.3 (md5 {KS13_MD5[:12]}...) under " + ", ".join(KICKSTART_DIRS),
            "install a Kickstart 1.3 rev 34.5 ROM or pass --kickstart PATH",
        )

    text = render(template, values)
    if warning:
        print(warning)
    if args.to_stdout:
        print(f"# ---- {out_rel.as_posix()} ----")
        print(text)
        if warning:
            print(warning)
        return

    out = ROOT / out_rel
    out.parent.mkdir(parents=True, exist_ok=True)
    for folder in EMULATORS[args.emulator].dirs:
        (out.parent / folder).mkdir(exist_ok=True)
    out.write_text(text, encoding="utf-8")
    print(f"wrote     {out_rel.as_posix()}")
    command = launch_command(out, args.emulator)
    print(f"  launch: {command}" if command else
          "  " + {"winuae": "WinUAE is not installed in the usual place",
                  "fsuae": "fs-uae is neither on PATH nor installed as a Flatpak"}[args.emulator]
          + " -- config written anyway")


if __name__ == "__main__":
    main()
