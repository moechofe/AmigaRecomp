#!/usr/bin/env python3
"""Pack a game's macOS executable into an application bundle and zip it."""

import argparse
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Reverse DNS of the GitHub repository, github.com/moechofe/AmigaRecomp.
BUNDLE_PREFIX = "io.github.moechofe.amigarecomp"


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(2)


def run(*args):
    done = subprocess.run(args, capture_output=True, text=True)
    if done.returncode != 0:
        fail(f"{args[0]} exited {done.returncode}: {done.stderr.strip()}")
    return done.stdout


def desktop_name(desktop):
    """The Name= of the [Desktop Entry] group."""
    group = None
    for line in desktop.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("["):
            group = line
        elif group == "[Desktop Entry]" and line.startswith("Name="):
            return line[len("Name="):]
    fail(f"{desktop.relative_to(ROOT)} has no Name=")


def macos_floor(exe):
    """The newest minos among the executable's slices: the oldest macOS that runs every one."""
    floors = [line.split()[1] for line in run("vtool", "-show-build", str(exe)).splitlines()
              if line.split()[:1] == ["minos"]]
    if not floors:
        fail(f"vtool reports no minos for {exe}")
    return max(floors, key=lambda v: tuple(int(p) for p in v.split(".")))


def assemble(app, game, exe, name, icon, floor):
    """Write <id>.app: the executable, the icon converted to ICNS, Info.plist; then sign it ad hoc."""
    if app.exists():
        shutil.rmtree(app)
    macos = app / "Contents" / "MacOS"
    resources = app / "Contents" / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir()
    shutil.copy2(exe, macos / game)
    (macos / game).chmod(0o755)
    run("sips", "-s", "format", "icns", str(icon), "--out", str(resources / f"{game}.icns"))
    info = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": name,
        "CFBundleExecutable": game,
        "CFBundleIconFile": game,
        "CFBundleIdentifier": f"{BUNDLE_PREFIX}.{game}",
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": name,
        "CFBundlePackageType": "APPL",
        "LSMinimumSystemVersion": floor,
        "NSHighResolutionCapable": True,
    }
    with open(app / "Contents" / "Info.plist", "wb") as f:
        plistlib.dump(info, f)
    run("codesign", "--force", "--sign", "-", str(app))


def main():
    ap = argparse.ArgumentParser(
        description="Assemble games/<id>/build/dist/macos/<id>.app from the macOS executable, "
        "games/<id>/packaging/<id>.desktop (its Name) and <id>.png, sign it ad hoc, and zip it "
        "into games/<id>/build/dist/macos/<id>-macos-<arch>.zip, where <arch> is `universal` "
        "for an executable with several architectures.")
    ap.add_argument("--game", required=True, metavar="ID", help="the folder under games/")
    ap.add_argument("--exe", type=Path,
                    help="the macOS executable (default games/<id>/build/cmake-macos/<id>)")
    ns = ap.parse_args()

    if sys.platform != "darwin":
        fail("needs macOS: sips, vtool, lipo, codesign and ditto")
    gamedir = ROOT / "games" / ns.game
    if not gamedir.is_dir():
        fail(f"no games/{ns.game}")
    packaging = gamedir / "packaging"
    desktop = packaging / f"{ns.game}.desktop"
    if not desktop.is_file():
        fail(f"no {desktop.relative_to(ROOT)}")
    icon = packaging / f"{ns.game}.png"
    if not icon.is_file():
        fail(f"no {icon.relative_to(ROOT)}")
    exe = ns.exe or gamedir / "build" / "cmake-macos" / ns.game
    if not exe.is_file():
        fail(f"no executable at {exe}")

    archs = run("lipo", "-archs", str(exe)).split()
    label = "universal" if len(archs) > 1 else archs[0]
    dist = gamedir / "build" / "dist" / "macos"
    app = dist / f"{ns.game}.app"
    out = dist / f"{ns.game}-macos-{label}.zip"
    dist.mkdir(parents=True, exist_ok=True)
    assemble(app, ns.game, exe, desktop_name(desktop), icon, macos_floor(exe))
    out.unlink(missing_ok=True)
    run("ditto", "-c", "-k", "--keepParent", str(app), str(out))
    print(f"wrote {app.relative_to(ROOT)} and {out.relative_to(ROOT)} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
