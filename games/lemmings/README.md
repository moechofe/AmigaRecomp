# Lemmings

The command lines for this game, in the order they are used. Run every one from the
`amigarecomp/` root. `docs/tools.md` explains each tool's options, and `docs/emulator.md`
explains the oracle. Python commands work the same in PowerShell and Git Bash. The other
commands are given for both.

The images live in `games/lemmings/disks/`, as they arrived. Which pair the oracle boots, and
why, is in `docs/dumps.md`.

## 1. Check the machine

    python tools/doctor.py

Linux, to use FS-UAE as the oracle: it needs FS-UAE (the `fs-uae` binary on `PATH`, or the
Flatpak) and `Xvfb`, and `doctor.py` does not check them:

    flatpak install flathub net.fsuae.FS-UAE
    sudo apt install xvfb

A Kickstart 1.3 rev 34.5 ROM is found by its content hash in `~/Documents/WinUAE/Kickstarts`
or `~/Documents/FS-UAE/Kickstarts`.

## 2. Unpack the oracle's pair

PowerShell:

    Expand-Archive -Force -LiteralPath "games\lemmings\disks\Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].zip" -DestinationPath games\lemmings\build\adf
    Expand-Archive -Force -LiteralPath "games\lemmings\disks\Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].zip" -DestinationPath games\lemmings\build\adf

Git Bash:

    unzip -o "games/lemmings/disks/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].zip" -d games/lemmings/build/adf
    unzip -o "games/lemmings/disks/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].zip" -d games/lemmings/build/adf

## 3. Generate the oracle config

WinUAE (Windows):

    python tools/emulator.py --game lemmings --emulator winuae --df0 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].adf" --df1 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].adf"

FS-UAE (Linux):

    python3 tools/emulator.py --game lemmings --emulator fsuae --df0 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 1 of 2)[cr SR].adf" --df1 "games/lemmings/build/adf/Lemmings (1991)(Psygnosis)(Disk 2 of 2)[cr SR].adf"

These write `games/lemmings/build/winuae/oracle.uae` from `oracle.winuae.in` and
`games/lemmings/build/fsuae/oracle.fs-uae` from `oracle.fsuae.in`. Edit the templates, never
the generated files. Add `--print` to see the result without writing it. `--emulator` defaults
to `winuae` on Windows and `fsuae` elsewhere.

## 4. Run the oracle by hand

PowerShell:

    & "C:\Program Files\WinUAE\winuae64.exe" -f "games\lemmings\build\winuae\oracle.uae"

Then press F7, the Amiga's left mouse button, to leave the Skid Row cracktro. F11 takes a
screenshot.

Linux, FS-UAE (the command is the "launch:" line `emulator.py` printed; for the Flatpak):

    flatpak run --nosocket=wayland net.fsuae.FS-UAE games/lemmings/build/fsuae/oracle.fs-uae

or `fs-uae games/lemmings/build/fsuae/oracle.fs-uae` when it is on `PATH`. F7 is the left mouse
button, F11 takes a screenshot into `games/lemmings/build/fsuae/shots/`, Pause opens the
debugger in the terminal, and End quits (from another terminal:
`flatpak kill net.fsuae.FS-UAE`). Left Alt releases the input grab.

## 5. Take the reference screenshots

    python tools/capture.py --game lemmings --script games/lemmings/scripts/boot.dbg

The script runs from a cold boot to the title menu in about 2 minutes. It writes
`psygnosis.png`, `dma-design.png` and `title-menu.png` into `games/lemmings/reference/`.
`--emulator` defaults to the platform's emulator. WinUAE needs the interactive desktop: do not
touch the keyboard or the mouse while it runs. FS-UAE runs on its own `Xvfb` server, so no
window opens and the desktop stays free.
`expansion-ram.png` is not in the script, because that screen lasts about 1.5 s
(`docs/emulator.md`, "References").

Other runs, with the same options on either emulator (`python3` on Linux):

    python tools/capture.py --game lemmings --until 32 --name NAME
    python tools/capture.py --game lemmings --script games/lemmings/build/my.dbg
    python tools/capture.py --game lemmings --statefile games/lemmings/build/my.uss --until 32 --name NAME

`my.dbg` is a session file, for example:

    !sleep 12
    !key f7 0.2
    !until 32
    S {out}/owl.bin 0 80000
    !save games/lemmings/build/owl.uss

It crosses the cracktro, dumps the 512 KB of chip RAM into `games/lemmings/reference/ramdump/`
and banks a save state that `--statefile` restores later. FS-UAE only: `!move DX DY` moves the
pointer. `docs/tools.md` lists every directive.

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
