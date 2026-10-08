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

Linux, to use FS-UAE as the oracle, it needs FS-UAE and `Xvfb`: see step 1 of
`games/lemmings/README.md`.

## 2. Unpack the pair

PowerShell:

    Expand-Archive -Force -LiteralPath "games\deuteros\disks\Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).zip" -DestinationPath games\deuteros\build\adf
    Expand-Archive -Force -LiteralPath "games\deuteros\disks\Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).zip" -DestinationPath games\deuteros\build\adf

Git Bash:

    unzip -o "games/deuteros/disks/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).zip" -d games/deuteros/build/adf
    unzip -o "games/deuteros/disks/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).zip" -d games/deuteros/build/adf

## 3. Generate the oracle config

WinUAE (Windows):

    python tools/emulator.py --game deuteros --emulator winuae --df0 "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" --df1 "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

FS-UAE (Linux):

    python3 tools/emulator.py --game deuteros --emulator fsuae --df0 "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" --df1 "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

These write `games/deuteros/build/winuae/oracle.uae` from `oracle.winuae.in` and
`games/deuteros/build/fsuae/oracle.fs-uae` from `oracle.fsuae.in`. Edit the templates, never
the generated files. Add `--print` to see the result without writing it. `--emulator` defaults
to `winuae` on Windows and `fsuae` elsewhere. FS-UAE needs FS-UAE and `Xvfb`; the install
lines are in `games/lemmings/README.md`, step 1.

## 4. Run the oracle by hand

PowerShell:

    & "C:\Program Files\WinUAE\winuae64.exe" -f "games\deuteros\build\winuae\oracle.uae"

F11 takes a screenshot. The game asks for Disk 2 in DF0; the debugger command
`I floppy0=<path>` swaps it (`docs/emulator.md`).

Linux, WinUAE (the `winuae` package puts `winuae` on `PATH`), after unpacking with the Git Bash
`unzip` lines of step 2 and generating the config with `--emulator winuae`:

    winuae -f games/deuteros/build/winuae/oracle.uae

The generator then prints "WinUAE is not installed in the usual place"; ignore it, the config
is written.

Linux, FS-UAE (for the Flatpak; with `fs-uae` on `PATH` use `fs-uae <config>`):

    flatpak run --nosocket=wayland net.fsuae.FS-UAE games/deuteros/build/fsuae/oracle.fs-uae

F11 takes a screenshot into `games/deuteros/build/fsuae/shots/`, F9 inserts Disk 2 into DF0
(the swap the game asks for), F8 and F6 save and restore a state, Pause opens the debugger in
the terminal, and End quits (from another terminal: `flatpak kill net.fsuae.FS-UAE`). The
title waits for a click, the language menu for `1`, `2` or `3`.

## 5. Take the reference screenshots

    python tools/capture.py --game deuteros --script games/deuteros/scripts/boot.dbg

Under FS-UAE this runs from a cold boot through the language menu and the data-disk prompt to
the Earth base, and keeps `language-menu.png` and `earth-base.png` in
`games/deuteros/reference/`, about 90 s. It has not been run under WinUAE, where the swap is
the `I` command (`docs/emulator.md`). FS-UAE runs on its own `Xvfb` server, so no window
opens.

    python tools/capture.py --game deuteros --until 16 --name NAME

keeps the title, the first screen with 16 colours. `--until 4` alone never reaches the language
menu from a cold boot under FS-UAE, because the title waits for a click. The current references
were copied from `../proof` (`docs/emulator.md`, "References").

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

On Windows the executable is a GUI-subsystem program, so PowerShell does not wait for it: add
`| Out-Host` to a headless command to wait and to get `$LASTEXITCODE`.

The executable takes ADF, ADZ, GZ and ZIP images, in any order, from the command line, from files
dropped on the window, from the menu's "Open Disk…" button, and from the paths it remembers in
`settings.ini` (`docs/game.md`, item 5). With no arguments and no remembered paths it opens the
menu on its About page; the Disks page lists what is missing. Group images by CRC and check them against `dumps.txt`:

    python tools/adf.py games/deuteros/disks

Make a ZIP of the untagged pair (the Linux command; `python` on Windows):

    python3 -m zipfile -c games/deuteros/build/pair.zip "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 1 of 2).adf" "games/deuteros/build/adf/Deuteros - The Next Millennium (1991)(Activision)(M3)(Disk 2 of 2).adf"

Run both scripts from the ZIP instead of the two ADFs:

    games/deuteros/build/cmake-linux/deuteros --headless --shot-dir games/deuteros/build/shots --script games/deuteros/scripts/menu.input games/deuteros/build/pair.zip
    games/deuteros/build/cmake-linux/deuteros --headless --shot-dir games/deuteros/build/shots --script games/deuteros/scripts/earth-base.input games/deuteros/build/pair.zip

Run the hack scripts (`docs/hacks.md` gives the figures each one proves). Each writes the chip
RAM with `--dump`:

    games/deuteros/build/cmake-linux/deuteros --headless --script games/deuteros/scripts/keep-pointer.input --dump games/deuteros/build/kp-on.ram games/deuteros/build/pair.zip
    games/deuteros/build/cmake-linux/deuteros --headless --script games/deuteros/scripts/space-advance-start.input --dump games/deuteros/build/sa-start.ram games/deuteros/build/pair.zip
    games/deuteros/build/cmake-linux/deuteros --headless --script games/deuteros/scripts/space-advance.input --dump games/deuteros/build/sa.ram games/deuteros/build/pair.zip

Compute an anchor for a hack from the dump that `earth-base.input` writes:

    games/deuteros/build/cmake-linux/deuteros --headless --script games/deuteros/scripts/earth-base.input --dump games/deuteros/build/earth-base.ram games/deuteros/build/pair.zip
    python tools/anchor.py games/deuteros/build/earth-base.ram --addr 0x023FC6 --len 8

Run the unit tests of the image loader and of the hack runtime:

    ctest --test-dir games/deuteros/build/cmake-linux --output-on-failure

## 8. Compare with WinUAE

    python tools/pixdiff.py --game deuteros games/deuteros/build/shots/lang-menu-nosprite.png games/deuteros/reference/menu-nosprite.png
    python tools/pixdiff.py --game deuteros games/deuteros/build/shots/earth-base-nosprite.png games/deuteros/reference/earth-base-nosprite.png

The expected figures are in this folder's `PLAN.md`, M2: 81 920 of 81 920 for the menu, and for
the Earth base every difference inside x 13–23, y 79–90. The runs from `pair.zip` give the same
figures as the runs from the two ADFs.

## 9. Every dump on hand

Run one pair of ZIPs from `games/deuteros/disks/` into its own shot folder, with a separate
preferences folder so that the real one is untouched (the placeholders are the Disk 1 CRC, a
script name and the two ZIP names):

    games/deuteros/build/cmake-linux/deuteros --headless --pref games/deuteros/build/m5-pref --shot-dir games/deuteros/build/shots/m5/<disk1crc>-<script>/ --script games/deuteros/scripts/<script>.input "games/deuteros/disks/<Disk 1>.zip" "games/deuteros/disks/<Disk 2>.zip"

Use `menu.input` and `earth-base.input` for a dump that opens on the title, `menu-intro.input`
and `earth-base-intro.input` for one that opens on a cracker's intro or trainer screen that waits
for the left button, and `earth-base-black-monks.input` for the Black Monks dumps, which have no
language menu. Compare the shots as in section 8. `docs/dumps.md` lists the pairs, the script
for each, and the figures.

## 10. Distributable build

The executable is built from a fresh clone, which holds no disks and fetches its own
`build/deps`, and the result is copied to `games/deuteros/build/dist/<os>/`.

Windows (PowerShell, with WinLibs on `PATH` as in section 1):

    git clone --no-hardlinks . $env:TEMP\ar-m6
    cmake -S $env:TEMP\ar-m6\games\deuteros -B $env:TEMP\ar-m6\games\deuteros\build\cmake -G Ninja
    cmake --build $env:TEMP\ar-m6\games\deuteros\build\cmake
    ctest --test-dir $env:TEMP\ar-m6\games\deuteros\build\cmake --output-on-failure
    objdump -p $env:TEMP\ar-m6\games\deuteros\build\cmake\deuteros.exe | Select-String "DLL Name"
    New-Item -ItemType Directory -Force games\deuteros\build\dist\windows
    Copy-Item $env:TEMP\ar-m6\games\deuteros\build\cmake\deuteros.exe games\deuteros\build\dist\windows\deuteros.exe

The expected DLL list is in `docs/runtime.md`, "Windows toolchain". `ctest` reports 3 of 3.

Run it in isolation: a folder that holds only the executable, the two ZIPs and the two scripts,
and a `PATH` that holds only Windows. In a new PowerShell process:

    New-Item -ItemType Directory $env:TEMP\ar-m6-run, $env:TEMP\ar-m6-run\shots
    Copy-Item games\deuteros\build\dist\windows\deuteros.exe, games\deuteros\scripts\menu.input, games\deuteros\scripts\earth-base.input, "games\deuteros\disks\<Disk 1>.zip", "games\deuteros\disks\<Disk 2>.zip" $env:TEMP\ar-m6-run
    Set-Location $env:TEMP\ar-m6-run
    $env:PATH = "C:\Windows\System32;C:\Windows"
    .\deuteros.exe --headless --pref .\pref --shot-dir .\shots --script menu.input "<Disk 1>.zip" "<Disk 2>.zip" | Out-Host
    .\deuteros.exe --headless --pref .\pref --shot-dir .\shots --script earth-base.input "<Disk 1>.zip" "<Disk 2>.zip" | Out-Host

The Windows executable is a GUI-subsystem program, so `| Out-Host` makes PowerShell wait for it;
see `docs/runtime.md`, "Windows toolchain".

Compare `$env:TEMP\ar-m6-run\shots` with the references as in section 8; the figures to expect
are there.
