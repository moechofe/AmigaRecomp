# Deuteros — plan

What happens next for `deuteros`, and for the shared code that its milestones build: `core/`,
`frontend/`, `cmake/`, `tools/` and `tests/`. The root `PLAN.md` owns the legend and the rules
for this file. Paths are from the `amigarecomp/` root, and the command lines are in
`README.md`.

## Commands

| Step | Command | Status |
| --- | --- | --- |
| WinUAE config | `tools/emulator.py` | `[x]` |
| References | `tools/capture.py` | `[~]` copied from `../proof` |
| Build, Linux | `cmake … build/cmake-linux` | `[x]` M1 |
| Build, Windows | `cmake … build/cmake` | `[x]` M1 |
| Run headless with a script | `deuteros --headless --script …` | `[x]` M2, Linux and Windows |
| Compare with WinUAE | `tools/pixdiff.py` | `[x]` M2, Linux and Windows |

## Milestones

### M0 — Foundation

- `[x]` This set of steps.
    - *Done when:* every file that `ARCHITECTURE.md` lists under "Exists today" exists, and
      every path in `CLAUDE.md`'s routing table either exists or names its milestone.
    - The implementer ticks this box, and fills in *Observed*, only after the Verification
      section of this plan passes.
    - *Observed:* 2026-10-06. `find amigarecomp -type f` lists the foundation files; the four
      files that share a path with `../proof` differ from it under `cmp`; the old-naming grep
      matches only `../proof` paths and decision 014's *Considered:*; a fresh-reader subagent
      confined to this folder answered the hack, ZIP and layout questions from the docs.

### M1 — Toolchain, static link, one executable per game

- `[x]`
    - `cmake/amigarecomp.cmake` is a module that each game's `CMakeLists.txt` includes. There is
      no root `CMakeLists.txt`. The module is guarded by `include_guard(GLOBAL)` and does the
      following:
        - sets C99 and C++17;
        - sets `FETCHCONTENT_BASE_DIR` to `${CMAKE_CURRENT_LIST_DIR}/../build/deps`, unless the
          cache already sets it;
        - fetches these pinned versions:
            - SDL3 `release-3.4.18`, with `SDL_STATIC ON` and `SDL_SHARED OFF`;
            - Musashi commit `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`, with m68kmake codegen
              and `MUSASHI_CNF`;
            - FreeType `VER-2-14-1`, with zlib, bzip2, png, harfbuzz and brotli off;
            - RmlUi `6.3`, with the FreeType font engine;
            - miniz `3.1.2`. Confirm that the tag exists. If it does not, use the newest 3.1.x
              tag and write down which one.
        - defines the static libraries `amiga_core` (from `core/`) and `amiga_frontend` (from
          `frontend/`);
        - defines the function `amiga_add_game(<id>)`, which builds the executable `<id>` from
          `${CMAKE_CURRENT_SOURCE_DIR}/*.c` and links both libraries.
    - Linking:
        - MinGW: `-static`.
        - Linux: every third-party library is static, plus `-static-libgcc -static-libstdc++`.
          glibc and libGL stay dynamic, so that SDL can dlopen X11, Wayland and the audio
          backends.
    - `games/deuteros/CMakeLists.txt` uses the four-line form from `docs/game.md`, and
      `games/deuteros/game.c` holds only `id` and `title`.
    - `tools/doctor.py` checks the toolchain. When a git repo exists, it also checks that git
      sees nothing derived from disks, by running `git check-ignore` over every
      `games/*/disks`, `games/*/reference` and `games/*/build`, and over the root `build`.
    - Create `docs/tools.md`.
    - *Done when:*
        - `games/deuteros/build/cmake-linux/deuteros` opens a black 960×768 window whose title
          comes from `game_def.title`;
        - `ldd` on it lists only glibc libraries: libc, libm, ld-linux, and libdl and
          libpthread on older glibc;
        - `objdump -p games/deuteros/build/cmake/deuteros.exe | grep "DLL Name"` lists only
          Windows system DLLs, with no `libstdc++-6`, `libgcc_s_*`, `libwinpthread-1` or
          `SDL3`;
        - a second configure into a new build directory downloads nothing, because
          `build/deps/` is reused.
    - *Observed:* 2026-10-06, Linux, from `games/deuteros/build/cmake-linux` (Release, GCC 13).
        - `deuteros` opens a window whose client area is 960×768 (`xwininfo -id` on the client
          window; `xwininfo -name` finds mutter's 1082×926 frame first) and every pixel of its
          capture (`import -window`) is black; the title is `game_def.title`.
        - `ldd` lists `linux-vdso`, `libm`, `libc` and `ld-linux-x86-64`, nothing else.
        - A second configure into a new build directory with `GIT_ALLOW_PROTOCOL=file` exits 0,
          clones nothing and creates no `*-subbuild` directory: `build/deps/` holds
          `musashi-src`, `sdl3-src`, `freetype-src`, `rmlui-src`, `miniz-src` and a `.pin` file
          for each. miniz tag `3.1.2` exists, so it is the pin.
        - `python tools/doctor.py` prints only `[ok]` and exits 0.
    - *Observed:* 2026-10-06, Windows 11, from `games/deuteros/build/cmake` (Release, WinLibs
      GCC 16.2.0, UCRT; `docs/runtime.md`, "Windows toolchain").
        - The first link failed with `undefined reference to 'WinMain'`; `frontend/main.c` is
          now compiled into the executable instead of `amiga_frontend`.
        - `objdump -p … deuteros.exe | grep "DLL Name"` lists only Windows system DLLs and the
          `api-ms-win-crt-*` UCRT forwarders: no `libstdc++-6`, `libgcc_s_*`,
          `libwinpthread-1` or `SDL3`.
        - Headless `menu.input`: 81 920 of 81 920, EXACT; `earth-base.input`: 81 862 of
          81 920, every difference inside x 13–23, y 79–90; both exit 0, as on Linux.
        - Window mode with `menu.input` opens the window, runs paced (369 frames in 9.7 s wall)
          and its shot is 81 920 of 81 920. The window was not looked at.

### M2 — Runtime under Musashi, and the oracle

- `[x]`
    - Rebuild `core/` from `../proof/backend.core`, porting it file by file. Drop:
        - `blocks.c`, `blocks.h` and `disc.c`;
        - the journal, the cosim state and the discovery state;
        - `interp_raw` and `fold64k`.
    - Generalise the disks: four drives, any image in any drive, and trackdisk unit *n* reads
      drive *n*. This removes the rule that unit 1 is the save disk.
    - Strip `boot_return` (in the proof's `boot.c`) down to the Kickstart strap's own effects:
        - the free lists come from the HLE allocator;
        - the stage-2 register values measured on Deuteros are not hard-coded;
        - if Deuteros needs a value that the strap does not define, record that as a finding
          instead of baking the value in.
    - Delete the proof's Deuteros hooks from `run.c`. They come back as hacks in M4.
    - `denise_pointer` reads `game_def.pointer_sprite`.
    - The frontend:
        - takes `.adf` paths on the command line;
        - flags: `--headless`, `--frames N`, `--script FILE`, `--shot-dir DIR`, `--state PATH`,
          `--pref DIR`, `--dump FILE` and `--no-sprites`;
        - window keys: F5, F9, F11 and F12.

      M2 accepts only raw `.adf`. The developer unzips their images by hand into
      `games/deuteros/build/adf/`, never into `disks/`.
    - Script directives: the proof's set minus `keep-pointer` and `space-advance`, plus
      `disk df<n> <role>`.
    - Port `tools/emulator.py`, `tools/capture.py` and `tools/pixdiff.py` from the proof, made
      per-game with `--game <id>`:
        - `emulator.py` fills in `games/<id>/oracle.winuae.in` and writes the result under
          `games/<id>/build/winuae/`;
        - `capture.py` writes into `games/<id>/reference/`;
        - `pixdiff.py` writes its diff images under `games/<id>/build/pixdiff/`.
    - `games/deuteros/` gets:
        - the roles `disk1`, `disk2` and `save`;
        - the drives `{disk1, save, -1, -1}`;
        - the DF0 cycle `{disk1, disk2, save}`;
        - `pointer_sprite 0`;
        - `scripts/menu.input` and `scripts/earth-base.input`;
        - `oracle.winuae.in`;
        - `docs/format.md`.
    - Create `docs/runtime.md` and `docs/emulator.md`.
    - *Done when:*
        - Run from the root, this command writes a shot that pixel-diffs **81 920 of 81 920**
          against the WinUAE `menu-nosprite` shot in `games/deuteros/reference/`. That is the
          proof's R2 figure.

              games/deuteros/build/cmake-linux/deuteros --headless --script games/deuteros/scripts/menu.input games/deuteros/build/adf/<disk1>.adf games/deuteros/build/adf/<disk2>.adf

        - `earth-base.input` diffs with every difference inside x 13–23, y 79–90. The proof's
          R5 figure is 81 862 of 81 920.
        - A case-insensitive `grep` for `deuteros` in `core`, `frontend`, `cmake` and `tools`
          finds nothing.
        - The -O3 headless run of `earth-base.input` runs at ≥ 8× real time.
    - *Observed:* 2026-10-06, Linux, Release (-O3), i7-7700K.
        - `menu.input` writes `lang-menu-nosprite.png`; `tools/pixdiff.py` reports 81 920 of
          81 920 pixels agree, EXACT, against `games/deuteros/reference/menu-nosprite.png`.
        - `earth-base.input` reaches its shot with no fatal; `pixdiff` reports 81 862 of 81 920
          agree, every difference inside x 13–23, y 79–90.
        - `grep -rni deuteros core frontend cmake tools` prints nothing.
        - `earth-base.input` runs 930 frames in 1.86–1.88 s wall, 9.9–10.0× real time (three
          runs: 10.02, 9.91, 10.02).
        - The references were copied from `../proof/build/oracle/`; no WinUAE run happened, and
          `capture.py` and `emulator.py` have been run only for `--help`, `--print` and their
          error paths (`capture.py` runs only on Windows).
        - Not observed: window-mode key handling (F5, F9, F11, F12) and pointer steering,
          because nothing here can inject keys or move the pointer. `docs/runtime.md`,
          "Findings", records that the strap's slow free list is not reproduced and needed no
          fix.

### M3 — Player disk images

- `[x]`
    - Implement `docs/game.md` item 5 in `frontend/` with miniz: command line, drop, dialog,
      remembered paths, the banner, and the Disks page.
    - The F1 menu's skeleton, in RmlUi, holding only the Disks page and the banner.
        - The shared documents are `frontend/menu/menu.rml`, `menu.rcss`, and a comment-only
          `game.rcss`. `menu.rml` links `menu.rcss`, then `game.rcss`.
        - `cmake/embed.cmake` embeds two tables. `sdl_menu_shared[]` holds the shared
          `frontend/menu/*` plus the LatoLatin Regular and Bold fonts from RmlUi's
          `Samples/assets/`. `sdl_menu_game[]` holds the game's `menu/*`.
        - The menu's `Rml::FileInterface` resolves a file name in `sdl_menu_game[]` first, then
          in `sdl_menu_shared[]`.
        - The shared `menu.rcss` uses a neutral panel colour, `#202020f0`.
    - `frontend/settings.c` keeps `settings.ini` in the pref path with the keys `disk.<role>`;
      headless mode neither reads nor writes it.
    - `GameDisk` gains `identify` (`docs/game.md` item 3). Deuteros's `dumps.txt` lists the
      untagged pair as `known`, and its two `identify` functions are CRC anchors recorded in
      `docs/format.md`, "Identification".
    - `cmake/dumps.cmake` turns `dumps.txt` into `game_dumps.c` in the game's build tree.
      `amiga_add_game` calls it.
    - `amigarecomp.cmake` adds the `tests/` targets once per build tree, guarded by
      `if(NOT TARGET test_image)`, so that `ctest --test-dir games/<id>/build/cmake-linux` runs
      them.
    - Add the ctest `test_image` to `tests/`. It builds synthetic 901 120-byte images in memory
      and checks:
        - a ZIP holding two ADFs gives two candidates;
        - an ADZ and a GZ give one candidate each;
        - a 1 802 240-byte ADF is rejected;
        - a truncated ZIP is rejected with a message;
        - `dumps.txt` parsing handles comments and all three statuses.
    - `tools/adf.py` identifies images. It expands containers and prints the CRC-32, the
      bootblock type and whether its checksum is valid. It then prints the matching line from
      any `games/*/dumps.txt`, or, for an unknown image, a line ready to paste in.
    - Write `docs/formats/adf.md`.
    - *Done when:*
        - `ctest --test-dir games/deuteros/build/cmake-linux` passes;
        - Deuteros, started with the ZIP in `games/deuteros/disks/` that holds both disks, runs
          `earth-base.input` to the same diff as M2. If there is no such ZIP, use a ZIP of the
          untagged pair made under `games/deuteros/build/`;
        - an image missing from `dumps.txt` boots with the banner;
        - started with no arguments, the executable shows the Disks page. Opening the images
          through the dialog boots the game, and the next launch boots with no dialog.
    - *Observed:* 2026-10-07, Linux (Ubuntu, X11 GNOME, `DISPLAY=:1`), Release (-O3).
        - `ctest --test-dir games/deuteros/build/cmake-linux` passes; `test_image` prints `ok`
          for the ZIP, ADZ, GZ, double-size, truncated-ZIP, dumps-table and identification
          checks. Changing `known` to `kown` in `tests/dumps.txt` fails the build with
          `cannot parse`.
        - `pair.zip`, made under `games/deuteros/build/` from the untagged pair (the commands
          are in `README.md`, sections 7 and 8), printed `pair.zip:…(Disk 1 of 2).adf: Disk 1,
          known dump` and the same for Disk 2. `earth-base.input` gave 81 862 of 81 920 inside
          x 13-23, y 79-90, and `menu.input` gave 81 920 of 81 920 EXACT, the same figures as
          the two ADFs on the command line.
        - Unknown dump: `[a](Disk 1 of 2).zip` (CRC `8cf442c6`) with the untagged Disk 2 ZIP
          printed `Unknown dump of Disk 1: running anyway; some hacks may stay inactive`. The
          headless `menu.input` run ended with exit 1 and `fatal: input.device command 1 is
          not implemented` at frame 364, before the language menu, so no shot and no pixdiff
          figure exist for it. In window mode the banner was visible over the game 4 s after
          launch.
        - Rejections, headless, exit 2 each: `x.txt: not an ADF, ADZ, GZ or ZIP`; `big.adf:
          not an 880 KB double-density ADF`; a Lemmings ZIP, `…: not a disk of Deuteros: The
          Next Millennium`; Disk 2 alone, `Deuteros: The Next Millennium is missing: Disk 1`.
        - Window, no images, `--pref /tmp/m3-dialog`: the Disks page listed Disk 1 "missing,
          needed to start", Disk 2 "missing, needed later" and `kept in /tmp/m3-dialog/save.adf`.
          A throwaway python-xlib XTEST script clicked "Open…", the GTK portal dialog opened
          with the "Amiga disk images" filter, and the script typed the path of `pair.zip`
          and pressed Return: the game booted, the panel closed, the title was drawn, and
          `settings.ini` held `disk.disk1=` and `disk.disk2=` with that path (the dialog returned
          it as `//home/…`, the dialog's own `/` plus the typed one). A relaunch with the same
          `--pref` booted with no panel; F1 showed `known dump` for both roles and Esc closed
          it.
        - `python tools/adf.py games/deuteros/disks games/lemmings/disks` exits 0: the untagged
          pair and `[f AGA Access]` print `deuteros: e55363d8 disk1 known …`, the Lemmings
          oracle pair prints `lemmings:` lines, `[a](Disk 1 of 2)` prints `not in any dumps.txt:
          8cf442c6 ROLE known …`, and Disk 1 prints `DOS0  sum-ok`.
        - Lemmings, rebuilt with the shared change: `ctest` passes, and `psygnosis.input` on the
          two `[cr SR]` ZIPs ends with `fatal: OpenLibrary("intuition.library") is not
          implemented` at frame 600, exit 1.
        - `grep -rni deuteros core frontend cmake tools tests` prints nothing.
        - RmlUi logs `Failed to load style sheet game.rcss.` at every start: it rejects a
          stylesheet with no rule, and the shared `game.rcss` holds only a comment. `menu.rcss`
          still applies.
        - Not observed: Windows; files dropped on the window (`SDL_EVENT_DROP_FILE`), which
          nothing here can inject; a game's own `menu/` folder.

### M4 — Settings menu and hacks

- `[x]`
    - `core/game.h` is exactly as `docs/game.md` item 3 gives it.
    - Hooks and hack status:
        - Hook PCs live in a bitmap over the word addresses of chip and slow RAM. The bitmap is
          checked before each interpreted instruction.
        - Anchors are checked with `game_anchor_ok`.
        - Each hack's status is tracked.
    - The frontend's own settings form a `struct GameSetting` table in `frontend/settings.c`.
      Its only entry for now is `mouse_capture`: bool, default 0.
    - The F1 menu, on M3's skeleton:
        - An RmlUi data model exposes arrays of `{id, label, hint, type, value, choices,
          status}`. `data-for` renders the "Settings" and "Hacks" sections, so no RML is
          generated.
        - `games/deuteros/menu/game.rcss` sets `#panel` to the proof's navy, `#101c3cf0`.
    - Settings storage:
        - `settings.ini` (M3) gains the keys `<setting id>` and `hack.<hackid>`.
        - Bool values are `0` or `1`; a choice value is the choice id.
        - Headless mode ignores the file and sets every hack to 0 (decision 011).
    - Add the script directive `hack <hackid> on|off|<choice id>`.
    - `tools/anchor.py` prints a `struct GameAnchor` initialiser computed from a RAM dump.
    - Deuteros's `keep_pointer` and `space_advance` become `hack_keep_pointer.c` and
      `hack_space_advance.c`. They are re-derived from the "Mouse hack" and "Keyboard hack"
      sections of `../proof/doc.agent/recomp.md` and anchored by CRC. Write `docs/hacks.md` for
      them.
    - Add the test `test_hack` to `tests/`. On synthetic RAM it checks that:
        - a matching guard calls `run`;
        - a changed byte keeps the original instruction and sets `HackMismatch`;
        - value 0 never calls `run`.
    - *Done when:*
        - with `hack keep_pointer on`, the pointer stays at x 28, y 19 after the Advance Time
          click instead of moving to x 102, y 29. These are the proof's figures;
        - a `space_advance` script starts and stops Advance Time with Space;
        - with every hack off, the M2 diffs are unchanged;
        - a setting toggled in F1 survives a restart, and `settings.ini` shows it;
        - the Deuteros menu panel shows navy `#101c3cf0`, which comes from
          `games/deuteros/menu/game.rcss`;
        - `ctest --test-dir games/deuteros/build/cmake-linux` passes.
    - *Observed:* 2026-10-08, Linux (Ubuntu), Release (-O3), tree rebuilt at HEAD (after `6a1c9d7`
      and the `core/state.c` change). The window checks ran on a private `Xvfb :5` (1280x1024,
      no window manager, 960x768 game window) driven by `xdotool`, not on the GNOME session.
        - `ctest --test-dir games/deuteros/build/cmake-linux`: `100% tests passed, 0 tests
          failed out of 3` (`test_image`, `test_hack`, `test_state`).
        - Hacks off (headless, from `pair.zip`, both exit 0, no `fatal:`): `menu.input` gave
          81 920 of 81 920 EXACT, and `earth-base.input` gave 81 862 of 81 920, differences
          inside x 13-23, y 79-90. These are the M2 figures.
        - `keep_pointer`: `keep-pointer.input` dump, words at 0x1FFC8, 0x1FFCE, 0x1EE10,
          0x1EE12 = `28 19 0 0`. The same script without the `hack keep_pointer on` line =
          `102 29 28 10`.
        - `space_advance`: byte 0x1FFD9 and word 0x1EED6 = `1 0` after `space-advance-start.input`
          (started), `0 0` for the same script without the `hack` line (Space ignored), and
          `0 0` after `space-advance.input` (stopped by the second Space).
        - Panel: F1 on a fresh `--pref` showed the navy panel at x 180-780; the pixel at
          (190, 384) was `#0F1A38` (R 15, G 26, B 56, B-R 41). `Failed to load style sheet
          game.rcss` appeared 0 times in the log, in both launches.
        - Setting survives a restart: a fresh `--pref` has no `hack.*` key and both hacks show
          "modern" chosen, so "modern" is the default and clicking it proves nothing. Clicking
          "original" on Keep pointer wrote `hack.keep_pointer=0`; "modern" wrote
          `hack.keep_pointer=1`; "original" again wrote `0`. After closing the window and
          relaunching with the same `--pref` and no image arguments, the game booted with the
          title drawn, and F1 > Hacks showed "original" chosen for Keep pointer; `settings.ini`
          still held `hack.keep_pointer=0` with `disk.disk1=` and `disk.disk2=`.
        - Not observed: Windows; the GNOME session (the old `DISPLAY=:1` is a dead Xvfb that
          hangs every X client, and `xdotool` input to the Xwayland `:0` did not reach the
          compositor); a display scale other than 1.

### M5 — Every Deuteros dump on hand

- `[x]`
    - Run `adf.py` and both scripts on every readable image in `games/deuteros/disks/`.
    - Implement whatever the runtime lacks, by evidence, for example the hooked-bootblock
      path. Hardware disk DMA is `lemmings M2`.
    - Record each dump in `dumps.txt` and `docs/dumps.md`.
    - *Done when:* every readable image has the status `verified`, `known` or `refused` (with
      its reason), and none ends in an unexplained fatal.
    - *Observed:* 2026-10-08, Linux (Ubuntu), GCC 15.2 Release build `games/deuteros/build/cmake-linux`
      at `6a1c9d7` plus the uncommitted tree. 28 ZIPs, 19 CRCs: 7 `verified` Disk 1 and 4 Disk 2,
      6 `known` (the Spanish SKN pair, two Disk 1 and one Disk 2, and the three Black Monks Disk 1), 2 `refused` (the save
      disks, `MSCF` cabinets). Every run exits 0 with no `fatal:`.
        - `ctest` 3/3; `adf.py games/deuteros/disks` exits 0 and names all 28 images, none "not in
          any dumps.txt"; a save-disk ZIP exits 2 with its message; an `[a2]` and an SKN ADF with
          the last byte changed print `unknown dump` and the banner line.
        - Family A, C and D, English: `menu` or `menu-intro` 81 920/81 920 EXACT for all seven.
          Earth base: untagged 81 862/81 920 (x 13-23, y 79-90, as M2); the six others 81 405 or
          81 429/81 920, differences inside x 9-231, y 19-90 (the version-string line and the
          icon slot). The Skid Row dumps cross a 229-colour cracktro, the SKN `+5` one a
          23-colour trainer screen.
        - Spanish SKN (`f1c4eed4`, `d6fd242b`): menu 81 425/81 920, x 121-207, y 95-116 (the
          translated credit line); Earth base 81 270/81 920, x 9-311, y 19-90.
        - Black Monks (`7dbf36e9`, `4d1b0183`, `3f8f1030`): the FS-UAE oracle gets past the
          "Cracked by Black Monks!" prompt with Return and reaches the Earth base. The runtime
          did not, because of two gaps found by tracing a jump into garbage: `input.device`
          `CMD_RESET` (now accepted and ignored) and `InitRastPort` clearing 0x64 bytes where
          Kickstart 1.3 clears 0x4E (now 0x4E, read from the oracle's ROM). With them,
          `earth-base-black-monks.input` reaches the Earth base, 81 450/81 920, x 9-215, y 19-89.
          They have no language menu, so there is no menu shot and they are `known`, not
          `verified`. `menu-black-monks.input` was not written for that reason.
        - `identify` accepts the stage 2 CRCs `0fa5e766`, `a2fb4c87`, `36063ce7` and `db749c89`.
        - Not observed: Windows; the hacks on non-reference dumps; a Spanish reference.

### M6 — Distributable executables

- `[ ]`
    - Build the Windows and Linux `deuteros` from a checkout that holds no disks.
    - *Done when:* each build runs on a machine with no compiler, no Python and no SDL `-dev`
      packages, given only a player's ZIP, and reaches the Earth base. The author observes
      this.
    - *Observed:*

## Verification

- The four methods are in `docs/conventions.md`.
- The standing results to reproduce from the proof: the menu at 81 920/81 920, and the Earth
  base with every difference inside the lower animated icon slot.

## Risks

For the author.

| Risk | Answer |
| --- | --- |
| HLE coverage for each new game | Unimplemented calls stop loudly, and the set grows by evidence |
| A loader that uses hardware disk DMA (lemmings M2) | MFM-encode the ADF's standard sectors. An ADF can only hold standard-format tracks, so that is enough |
| The simplified timing model breaks | The proof measured 454 cycles a line and 313 lines a frame. If a diff shows tearing, give the blitter a duration |
| Anchors fail on cracked dumps | The hack stays inactive and is shown as such (decision 006) |

## Prior art

- `../proof` (OpenDeuteros, R1–R5) is read-only. Port code from it when a milestone says so;
  never copy its docs.
- mstan/segagenesisrecomp and N64Recomp are design references.
