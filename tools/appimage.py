#!/usr/bin/env python3
"""Pack a game's Linux executable into an AppImage with appimagetool."""

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ARCHES = ("x86_64", "aarch64")
ICONS = ("png", "svg")


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(2)


def find_icon(packaging, game):
    for ext in ICONS:
        icon = packaging / f"{game}.{ext}"
        if icon.is_file():
            return icon
    fail(f"no {packaging.relative_to(ROOT)}/{game}.png or {game}.svg")


def assemble(appdir, game, exe, desktop, icon):
    """Write the AppDir: the executable, the desktop entry, the icon and AppRun."""
    if appdir.exists():
        shutil.rmtree(appdir)
    bindir = appdir / "usr" / "bin"
    bindir.mkdir(parents=True)
    shutil.copy2(exe, bindir / game)
    (bindir / game).chmod(0o755)
    appsdir = appdir / "usr" / "share" / "applications"
    appsdir.mkdir(parents=True)
    shutil.copy2(desktop, appsdir / f"{game}.desktop")
    shutil.copy2(desktop, appdir / f"{game}.desktop")
    if icon.suffix == ".svg":
        iconsdir = appdir / "usr" / "share" / "icons" / "hicolor" / "scalable" / "apps"
        iconsdir.mkdir(parents=True)
        shutil.copy2(icon, iconsdir / icon.name)
    shutil.copy2(icon, appdir / icon.name)
    shutil.copy2(icon, appdir / ".DirIcon")
    apprun = appdir / "AppRun"
    apprun.write_text(f'#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/usr/bin/{game}" "$@"\n')
    apprun.chmod(0o755)


def main():
    ap = argparse.ArgumentParser(
        description="Assemble games/<id>/build/appimage/<id>.AppDir from the Linux executable and "
        "games/<id>/packaging/<id>.desktop and <id>.png (or .svg), then run appimagetool to write "
        "games/<id>/build/dist/linux/<id>-linux-<arch>.AppImage.")
    ap.add_argument("--game", required=True, metavar="ID", help="the folder under games/")
    ap.add_argument("--exe", type=Path,
                    help="the Linux executable (default games/<id>/build/cmake-linux/<id>)")
    ap.add_argument("--arch", choices=ARCHES, default=platform.machine(),
                    help="the executable's architecture (default: this machine's)")
    ap.add_argument("--tool", default="appimagetool", metavar="PATH",
                    help="appimagetool, an AppImage or a binary (default: from PATH)")
    ns = ap.parse_args()
    if ns.arch not in ARCHES:
        fail(f"this machine is {ns.arch}; pass --arch")

    gamedir = ROOT / "games" / ns.game
    if not gamedir.is_dir():
        fail(f"no games/{ns.game}")
    packaging = gamedir / "packaging"
    desktop = packaging / f"{ns.game}.desktop"
    if not desktop.is_file():
        fail(f"no {desktop.relative_to(ROOT)}")
    icon = find_icon(packaging, ns.game)
    exe = ns.exe or gamedir / "build" / "cmake-linux" / ns.game
    if not exe.is_file():
        fail(f"no executable at {exe}")
    tool = shutil.which(ns.tool)
    if tool is None:
        fail(f"{ns.tool} is not on PATH; pass --tool")

    appdir = gamedir / "build" / "appimage" / f"{ns.game}.AppDir"
    out = gamedir / "build" / "dist" / "linux" / f"{ns.game}-linux-{ns.arch}.AppImage"
    assemble(appdir, ns.game, exe, desktop, icon)
    out.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ARCH=ns.arch, APPIMAGE_EXTRACT_AND_RUN="1")
    done = subprocess.run([tool, "--no-appstream", str(appdir), str(out)], env=env)
    if done.returncode != 0:
        fail(f"appimagetool exited {done.returncode}")
    print(f"wrote {out.relative_to(ROOT)} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
