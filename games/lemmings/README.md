# Lemmings

The command lines for this game, in the order they are used. Run every one from the
`amigarecomp/` root. `docs/tools.md` explains each tool's options, and `docs/emulator.md`
explains the oracle. Python commands work the same in PowerShell and Git Bash. The other
commands are given for both.

The images live in `games/lemmings/disks/`, as they arrived. Which pair the oracle boots, and
why, is in `docs/dumps.md`.

## 1. Check the machine

    python tools/doctor.py

## 2. Unpack the oracle's pair

PowerShell:

    Expand-Archive -Force -LiteralPath "games\lemmings\disks\Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].zip" -DestinationPath games\lemmings\build\adf
    Expand-Archive -Force -LiteralPath "games\lemmings\disks\Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].zip" -DestinationPath games\lemmings\build\adf

Git Bash:

    unzip -o "games/lemmings/disks/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].zip" -d games/lemmings/build/adf
    unzip -o "games/lemmings/disks/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].zip" -d games/lemmings/build/adf

## 3. Generate the WinUAE config

    python tools/emulator.py --game lemmings --df0 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].adf" --df1 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].adf"

This writes `games/lemmings/build/winuae/oracle.uae` from `oracle.uae.in`. Edit the template,
never the generated file. Add `--print` to see the result without writing it.

## 4. Run WinUAE by hand

PowerShell:

    & "C:\Program Files\WinUAE\winuae64.exe" -f "games\lemmings\build\winuae\oracle.uae"

Then press F7, the Amiga's left mouse button, to leave the Skid Row cracktro. F11 takes a
screenshot.

Linux (the `winuae` package puts `winuae` on `PATH`). `tools/emulator.py` only searches for the
Kickstart under `C:\Users\Public\...`, so pass it with `--kickstart` in step 3, after unpacking
with the Git Bash `unzip` lines of step 2:

    python3 tools/emulator.py --game lemmings --kickstart "$HOME/Documents/WinUAE/Kickstarts/Kickstart v1.3 rev 34.5 (1987)(Commodore)(A500-A1000-A2000-CDTV)[!].rom" --df0 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].adf" --df1 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].adf"
    winuae -f games/lemmings/build/winuae/oracle.uae

The tool then prints "WinUAE is not installed in the usual place"; ignore it, the config is
written.

## 5. Take the reference screenshots

    python tools/capture.py --game lemmings --script games/lemmings/scripts/boot.dbg

The script runs from a cold boot to the title menu in about 2 minutes. It writes
`psygnosis.png`, `dma-design.png` and `title-menu.png` into `games/lemmings/reference/`. It
needs the interactive desktop: do not touch the keyboard or the mouse while it runs.
`expansion-ram.png` is not in the script, because that screen lasts about 1.5 s
(`docs/emulator.md`, "References").

## 6. Check and compare screenshots

    python tools/pixdiff.py --game lemmings games/lemmings/reference/psygnosis.png --colours
    python tools/pixdiff.py --game lemmings <shot.png> games/lemmings/reference/psygnosis.png

`--colours` flags any colour that is not a 12-bit OCS colour, which marks a scaled capture.
The comparison writes its difference image to `games/lemmings/build/pixdiff/diff.png`. The
comparison of the recompiled run is:

    python tools/pixdiff.py --game lemmings games/lemmings/build/shots/psygnosis.png games/lemmings/reference/psygnosis.png

## 7. Build and run the recompiled version

Windows (PowerShell and Git Bash are the same; the compiler is WinLibs, `docs/runtime.md`,
"Windows toolchain"):

    cmake -S games/lemmings -B games/lemmings/build/cmake -G Ninja
    cmake --build games/lemmings/build/cmake

Linux:

    cmake -S games/lemmings -B games/lemmings/build/cmake-linux -G Ninja
    cmake --build games/lemmings/build/cmake-linux

Headless, with `scripts/psygnosis.input` (it writes `games/lemmings/build/shots/psygnosis.png`
once the run reaches the owl):

    games/lemmings/build/cmake/lemmings.exe --headless --shot-dir games/lemmings/build/shots --script games/lemmings/scripts/psygnosis.input "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].adf" "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].adf"

On Linux, use `games/lemmings/build/cmake-linux/lemmings`. Today the run stops at the
cracktro's exit: `fatal: OpenLibrary("intuition.library") is not implemented`. This folder's
`PLAN.md`, M2, has the state.
