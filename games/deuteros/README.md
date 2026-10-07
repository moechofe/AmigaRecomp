# Deuteros

The command lines for this game, in the order they are used. Run every one from the
`amigarecomp/` root. `docs/tools.md` explains each tool's options, `docs/emulator.md` explains
the oracle, and `docs/runtime.md` explains the executable's options. Python commands work the
same in PowerShell and Git Bash. The other commands are given for both.

The images live in `games/deuteros/disks/`, as they arrived. The oracle and the runtime use the
untagged `Disk 1 of 2` and `Disk 2 of 2` pair.

## 1. Check the machine

Windows needs Python ≥ 3.11, Git and WinLibs GCC, which also brings CMake and Ninja. Install
WinLibs once, from PowerShell, then open a new terminal so that its `PATH` entry is seen:

    winget install BrechtSanders.WinLibs.POSIX.UCRT

`docs/runtime.md`, "Windows toolchain", has the versions that built this game. Then, on any
system:

    python tools/doctor.py

Every Toolchain line must be `[ok]`. On Windows, `[--]  C compiler` right after the install
means the terminal was opened before it.

## 2. Unpack the pair

PowerShell:

    Expand-Archive -Force -LiteralPath "games\deuteros\disks\Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).zip" -DestinationPath games\deuteros\build\adf
    Expand-Archive -Force -LiteralPath "games\deuteros\disks\Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).zip" -DestinationPath games\deuteros\build\adf

Git Bash:

    unzip -o "games/deuteros/disks/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).zip" -d games/deuteros/build/adf
    unzip -o "games/deuteros/disks/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).zip" -d games/deuteros/build/adf

## 3. Generate the WinUAE config

    python tools/emulator.py --game deuteros --df0 "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" --df1 "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

This writes `games/deuteros/build/winuae/oracle.uae` from `oracle.uae.in`. Edit the template,
never the generated file. Add `--print` to see the result without writing it.

## 4. Run WinUAE by hand

PowerShell:

    & "C:\Program Files\WinUAE\winuae64.exe" -f "games\deuteros\build\winuae\oracle.uae"

F11 takes a screenshot. The game asks for Disk 2 in DF0; the debugger command
`I floppy0=<path>` swaps it (`docs/emulator.md`).

## 5. Take a reference screenshot

    python tools/capture.py --game deuteros --until 4 --name NAME

This polls screenshots until one has 4 colours (the language menu) and keeps it as
`games/deuteros/reference/NAME.png`. The current references were copied from `../proof`
(`docs/emulator.md`, "References").

## 6. Build

Windows (`deuteros.exe`):

    cmake -S games/deuteros -B games/deuteros/build/cmake -G Ninja
    cmake --build games/deuteros/build/cmake

Linux (`deuteros`):

    cmake -S games/deuteros -B games/deuteros/build/cmake-linux -G Ninja
    cmake --build games/deuteros/build/cmake-linux

The first configure clones the dependencies into the root `build/deps/`, which takes minutes;
later build trees reuse them.

Check that the Windows executable needs only system DLLs (`objdump` comes with WinLibs):

PowerShell:

    objdump -p games\deuteros\build\cmake\deuteros.exe | Select-String "DLL Name"

Git Bash:

    objdump -p games/deuteros/build/cmake/deuteros.exe | grep "DLL Name"

The expected list is in `docs/runtime.md`, "Windows toolchain".

## 7. Run

In a window, with the two images in role order (Disk 1, then Disk 2):

    games/deuteros/build/cmake/deuteros.exe "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

Headless, with a script, writing the script's shot into `games/deuteros/build/shots/`:

    games/deuteros/build/cmake/deuteros.exe --headless --shot-dir games/deuteros/build/shots --script games/deuteros/scripts/menu.input "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"
    games/deuteros/build/cmake/deuteros.exe --headless --shot-dir games/deuteros/build/shots --script games/deuteros/scripts/earth-base.input "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

On Linux, use `games/deuteros/build/cmake-linux/deuteros` instead. `menu.input` writes
`lang-menu-nosprite.png` and `earth-base.input` writes `earth-base-nosprite.png`.

The executable takes ADF, ADZ, GZ and ZIP images, in any order, from the command line, from files
dropped on the window, from the Disks page's "Open…" button, and from the paths it remembers in
`settings.ini` (`docs/game.md`, item 5). With no arguments and no remembered paths it shows the
Disks page. Group images by CRC and check them against `dumps.txt`:

    python tools/adf.py games/deuteros/disks

Make a ZIP of the untagged pair (the Linux command; `python` on Windows):

    python3 -m zipfile -c games/deuteros/build/pair.zip "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

Run both scripts from the ZIP instead of the two ADFs:

    games/deuteros/build/cmake-linux/deuteros --headless --shot-dir games/deuteros/build/shots --script games/deuteros/scripts/menu.input games/deuteros/build/pair.zip
    games/deuteros/build/cmake-linux/deuteros --headless --shot-dir games/deuteros/build/shots --script games/deuteros/scripts/earth-base.input games/deuteros/build/pair.zip

Run the unit tests of the image loader:

    ctest --test-dir games/deuteros/build/cmake-linux --output-on-failure

## 8. Compare with WinUAE

    python tools/pixdiff.py --game deuteros games/deuteros/build/shots/lang-menu-nosprite.png games/deuteros/reference/menu-nosprite.png
    python tools/pixdiff.py --game deuteros games/deuteros/build/shots/earth-base-nosprite.png games/deuteros/reference/earth-base-nosprite.png

The expected figures are in this folder's `PLAN.md`, M2: 81 920 of 81 920 for the menu, and for
the Earth base every difference inside x 13–23, y 79–90. The runs from `pair.zip` give the same
figures as the runs from the two ADFs.
