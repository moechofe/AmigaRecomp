#!/usr/bin/env python3
"""Report what this checkout has, what it is missing, and what git can see.

Read-only: it opens nothing for writing and never touches the disk images.
Run it from the amigarecomp/ root:  python tools/doctor.py
Exit status 1 when any check is [--] or [!!], else 0.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Windows picks cp1252 for a redirected stdout.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parent.parent

# docs/game.md item 2 requires 3.24 in every game's CMakeLists.txt.
CMAKE_MIN = (3, 24)

OK, MISSING, WARN = "[ok]  ", "[--]  ", "[!!]  "

failures = 0


def line(mark, text, detail=""):
    global failures
    if mark != OK:
        failures += 1
    print(f"{mark}{text}" + (f"  {detail}" if detail else ""))


def which(*names):
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def section(title):
    print(f"\n{title}\n" + "-" * len(title))


def cmake_version(exe):
    out = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout
    m = re.search(r"version (\d+)\.(\d+)", out)
    return (int(m.group(1)), int(m.group(2))) if m else None


def check_toolchain():
    section("Toolchain")
    v = sys.version_info
    line(OK if (v.major, v.minor) >= (3, 11) else WARN, f"python {v.major}.{v.minor}.{v.micro}", sys.executable)

    cmake = which("cmake")
    if not cmake:
        line(MISSING, "cmake", "not on PATH")
    else:
        version = cmake_version(cmake)
        need = ".".join(map(str, CMAKE_MIN))
        if version and version >= CMAKE_MIN:
            line(OK, f"cmake {version[0]}.{version[1]}", cmake)
        else:
            line(WARN, "cmake", f"{cmake} is older than {need}")

    for label, names in [
        ("ninja", ("ninja",)),
        ("git", ("git",)),
        ("C compiler", ("gcc", "cc", "clang")),
        ("C++ compiler", ("g++", "c++", "clang++")),
    ]:
        found = which(*names)
        line(OK if found else MISSING, label, found or "not on PATH")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)


def check_git():
    section("Git hygiene  (hard rule 1: no game data in git)")
    if not shutil.which("git") or git("rev-parse", "--is-inside-work-tree").returncode != 0:
        print(WARN + "not a git repository")
        return
    paths = []
    for game in sorted((ROOT / "games").glob("*/")):
        if game.is_dir():
            paths += [game.relative_to(ROOT) / name for name in ("disks", "reference", "build")]
    paths.append(Path("build"))
    for path in paths:
        # The trailing slash is required: without it an absent directory is reported as not
        # ignored.
        ignored = git("check-ignore", "-q", f"{path.as_posix()}/").returncode == 0
        if ignored:
            line(OK, f"{path.as_posix()}/ is ignored")
        else:
            line(WARN, f"{path.as_posix()}/ is visible to git")


def main():
    argparse.ArgumentParser(description=__doc__.splitlines()[0]).parse_args()
    check_toolchain()
    check_git()
    print()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
