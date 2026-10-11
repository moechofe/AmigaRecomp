# Games — the per-game method

How a game is added, laid out, identified, verified and hacked. Decisions 005, 006, 007, 011,
013. ☐ marks a part that a later milestone of `games/deuteros/PLAN.md` implements, with that
milestone's number; ☑ marks a part that exists and has been observed.

## 1. What a supported game is

A supported game is the self-contained folder `games/<id>/` (decision 013).

- `<id>` matches `[a-z0-9_]+`. It becomes:
    - the CMake project and target;
    - the executable name (`<id>`, `<id>.exe`);
    - the pref-path app name, `SDL_GetPrefPath("AmigaRecomp", "<id>")`;
    - the C prefix for the game's functions.
- Build it (☑) from the `amigarecomp/` root:

        cmake -S games/<id> -B games/<id>/build/cmake -G Ninja
        cmake --build games/<id>/build/cmake

  On Linux, use `games/<id>/build/cmake-linux` in both commands. The executable is written to
  that build directory.
- Add `<id>` to the `game` matrix of `.github/workflows/dist.yml` once it builds;
  `docs/runtime.md`, "Distributable builds in CI", says what that runs.

## 2. Folder layout

These files are committed:

- `CMakeLists.txt` — exactly these four lines, with `<id>` substituted:

        cmake_minimum_required(VERSION 3.24)
        project(<id> C CXX)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/amigarecomp.cmake)
        amiga_add_game(<id>)

  `amiga_add_game(<id>)` compiles every `*.c` in the game folder together with the shared core
  and frontend. It also generates `game_dumps.c` from `dumps.txt` (☑) and embeds the menu
  assets (☑). Anything else the game's build needs goes after that call, in this file only.
- `game.c` — defines the only exported symbol, `const struct GameDef game_def`.
- `hack_<hackid>.c` — one file per hack. Every function in it is `static`; the file exports
  only `const struct GameHack <id>_hack_<hackid>`, which `game.c` uses.
- `dumps.txt` — the known-dump table (item 4).
- `menu/` (☑) — this game's RmlUi assets, all embedded into the executable.
    - The menu's file interface looks a file name up in the game's `menu/` first, then in the
      shared `frontend/menu/`. A game file therefore overrides the shared file with the same
      name.
    - The shared `menu.rml` links `menu.rcss`, then `game.rcss`. The shared `game.rcss` holds
      only a comment.
    - A game restyles the menu by providing `menu/game.rcss`, and replaces the whole document
      by providing `menu/menu.rml`.
    - A game gives its About page text, as an RML fragment, in `menu/about.rml`; the shared
      `frontend/menu/about.rml` is the fallback.
    - Every file in it is ours: no game graphics, ever.
- `scripts/<name>.input` — headless input scripts.
- `scripts/<name>.dbg` — `tools/capture.py` session files that take the oracle's references.
- `README.md` — every command line for this game, in the order they are used: unpack the
  images, generate the oracle config, take the references, build, run, and compare. The README
  owns the command lines, and `docs/tools.md` owns the tools' options. A command whose result
  changes updates the README in the same change.
- `PLAN.md` — what happens next for this game, written from the template in the root
  `PLAN.md`.
- `oracle.winuae.in` — the WinUAE config template for this game, filled in by
  `tools/emulator.py`.
- `oracle.fsuae.in` — the FS-UAE config template, the same machine; optional, filled in the
  same way.
- `packaging/<id>.desktop` and `packaging/<id>.png` (or `.svg`) — the desktop entry and the icon
  that `tools/appimage.py` packs. The icon is the author's own artwork, never a game graphic;
  `.gitignore` lets `packaging/*.png` through for it.
- `docs/format.md` — the game's boot chain and disk facts, each citing an ADF name and a byte
  offset.
- `docs/dumps.md` — every dump seen, with:
    - its name as distributed;
    - its CRC-32;
    - its role;
    - its status;
    - the evidence for that status (command, date, result).
- `docs/hacks.md` — for each hack:
    - what the original code does, in addresses and prose, never bytes;
    - what the hack changes;
    - its anchors;
    - the script that proves it.

These are local only and never committed; `.gitignore` covers them:

- `disks/` — the developer's copies of the game's ADF/ZIP/ADZ/GZ images, kept as they arrived.
- `reference/` — oracle save states, RAM dumps and screenshots.
- `build/` — the CMake trees (`cmake/`, `cmake-linux/`) and every tool's output for this game.

## 3. The contract `core/game.h` (☑)

`core/game.h` holds the C below, inside its include guard, after `<stdint.h>` and a forward
declaration of `struct Amiga`.

    struct GameAnchor { uint32_t addr; uint32_t len; uint32_t crc; }; /* CRC-32 (zlib) of the original bytes */
    enum GameValueType { GameValueBool, GameValueChoice };
    struct GameSetting
    {
    	const char *id;             /* settings.ini key, [a-z0-9_]+ */
    	const char *label;
    	const char *hint;
    	enum GameValueType type;
    	int def;                    /* default value; 0 is always the original behaviour */
    	const char *const *choices; /* GameValueChoice: NULL-terminated ids, index 0 = original */
    };
    struct GameHook
    {
    	uint32_t pc;                        /* instruction the hook replaces */
    	const struct GameAnchor *guard;     /* must match when pc is reached */
    	int (*run)(struct Amiga *m, int value); /* 1: handled, PC and cycles set; 0: run the original */
    };
    struct GameHack
    {
    	struct GameSetting setting;
    	const struct GameHook *hooks;
    	uint32_t hook_count;
    	void (*frame)(struct Amiga *m, int value); /* optional, after each frame; checks its own anchors */
    };
    struct GameDisk
    {
    	const char *id;    /* role: "disk1", "save", … */
    	const char *label; /* shown in the menu and in messages */
    	int writable;      /* 1: our file <pref>/<id>.adf, zero-filled when absent */
    	int (*identify)(const uint8_t *adf, uint32_t size); /* unknown dumps: 1 = this role; NULL for writable roles */
    };
    struct GameView { int x, y, w, h; }; /* the game's picture: DIW x (lores) of its first column, its first scanline, its size */
    struct GameDef
    {
    	const char *id, *title;
    	const struct GameDisk *disks; uint32_t disk_count;
    	int drives[4];               /* role index in DF0–DF3 at boot, -1 empty */
    	int drive_count;             /* DF0–DF(n-1): the drives the Disks page offers, 1–4 */
    	int pointer_sprite;          /* sprite holding the game's mouse pointer, -1 none */
    	struct GameView view;        /* what the window shows; every game sets it (item 6, step 8); w 0: the capture window */
    	const struct GameHack *const *hacks; uint32_t hack_count;
    };
    extern const struct GameDef game_def;
    int game_anchor_ok(const struct Amiga *m, const struct GameAnchor *a);

Runtime rules:

- A hack's hooks are live only while its value is ≠ 0.
- When the CPU reaches `pc`, the runtime checks `guard`. If it matches, `run` is called.
  Otherwise the original instruction runs and the hack's status records the mismatch.
- Each hack has one status per session:
    - `HackIdle`: not reached yet;
    - `HackActive`: a guard has matched at least once;
    - `HackMismatch`: guards have failed and none has ever matched.
- The menu shows the status. A hack in `HackMismatch` is greyed out with the text "not
  compatible with this dump".
- A `run` that skips game instructions charges their cycles.
- `game_def.hack_count` is at most `AMIGA_HACKS` (16); the boot refuses a larger table. A hack's
  value is set with `game_hack_set(m, index, value)`: a bool takes 0 or 1, a choice takes an
  index into `choices`; anything else is refused (-1) and changes nothing.
- A hook's `pc` is a canonical, even RAM address: chip RAM 0x000000–0x07FFFF or slow RAM
  0xC00000–0xC7FFFF. Code that runs from a chip mirror never meets a hook, and any other `pc`
  is a fatal error when the hack is switched on.
- `guard` is checked by `game_anchor_ok`, which is also public: a hook may check further
  anchors itself. An anchor whose range is not wholly inside chip or slow RAM never matches.
- A `run` that returns 0 leaves the original instruction to run. A hack with only a `frame`
  stays `HackIdle`: the status is set by hooks alone.
- Hack values, statuses and the hook map belong to the session, not to the machine: a state
  load leaves them as they are.
- Hacks may call only the runtime's public memory and CPU helpers, which `docs/runtime.md`
  lists (☑).

## 4. `dumps.txt` format

- `#` starts a comment.
- Each line holds whitespace-separated fields:
  `<crc32 8 hex digits> <role id> <status> <name to end of line>`.
- The status is one of:
    - `verified`: the gate in item 6 passed on this dump;
    - `known`: identified, not yet verified;
    - `refused`: identified, but does not work. The name field is the message shown to the
      player.
- The CRC-32 is zlib's, over the 901 120 decompressed bytes.
- CMake turns the file into `game_dumps.c` (☑), and `tools/adf.py` reads it.
- This file is the single owner of the dump list; `docs/dumps.md` holds only the evidence.

## 5. How the executable accepts images (☑)

1. **Sources.** The executable takes images from four places:
    - paths on the command line;
    - files dropped on the window;
    - the F1 menu's "Open Disk…" button on the About and Disks pages (the SDL file dialog);
    - paths remembered in `settings.ini` as `disk.<role>=<path>`.
2. **Expansion.**
    - `.adf` is used as is.
    - `.adz` and `.gz` are gunzipped.
    - `.zip` contributes every member ending in `.adf`, `.adz` or `.gz`, case-insensitively and
      one level deep.
    - Every candidate must be exactly 901 120 bytes. Anything else is rejected as "not an 880
      KB double-density ADF".
3. **Identification.**
    - Compute the CRC-32 and look it up in `dumps.txt`. A `refused` line shows its message.
    - If no line matches, try each non-writable role's `identify`, in order. The first one that
      returns 1 takes the image as an unknown dump, and a banner reads "Unknown dump of
      <label>: running anyway; some hacks may stay inactive".
    - If no role matches, the image is rejected as "not a disk of <title>".
4. **Conflicts.**
    - When two images claim the same role, verified wins over known, and known over unknown. On
      a tie, the image given first wins.
    - A missing role opens the menu on its About page; the Disks page lists what is missing.
    - The machine boots once every role in `drives[]` is filled.
    - The Disks page offers DF0–DF(`drive_count`-1); a `drives[n]` beyond `drive_count` refuses
      to start (`game_def.drives[n] is beyond drive_count N`).
5. **Storage.** Images are held whole in memory and never written or copied. Writable roles
   live in `<pref>/<role>.adf`. The Disks page's "Load save disk…" on a writable role copies
   an 880 KB ADF you choose into that file and into the image in memory; the chosen file is
   never written, and a dump of one of the game's own disks is refused.

## 6. Bringing up a new game

An ordered checklist:

1. Put the images in `games/<id>/disks/`.
2. Run `python tools/adf.py games/<id>/disks/*` (☑) to group the images by CRC and
   bootblock. Pick the reference dump that the oracle will boot.
3. Create `games/<id>/CMakeLists.txt` (item 2), a minimal `game.c` (roles and drives, no
   hacks, and `view` once a screen draws: step 8), and `dumps.txt` listing the reference dump
   as `known`. Configure and build.
4. Write `oracle.winuae.in` and/or `oracle.fsuae.in`, then take reference shots and states into `games/<id>/reference/`
   with `tools/capture.py --game <id>` (☐ M2).
5. Run the game headless, repeatedly, until the first fatal error:

        games/<id>/build/cmake-linux/<id> --headless --script games/<id>/scripts/<name>.input <images>

   Each fatal is a missing HLE call or missing hardware. Implement it in `core/` with a cited
   source: the HRM, the NDK, or a measurement written into `docs/runtime.md` or
   `docs/formats/`. Never implement it in a game-specific way.
6. **The verification gate for `verified`:**
    - the first-screen shot matches WinUAE on every informative pixel (`tools/pixdiff.py`);
    - a script reaches a named in-game screen with no fatal;
    - a window run shows the picture whole (item 8).

   Record the command, the date and the figures in `docs/dumps.md`.
7. Run the same scripts on every other dump and set each dump's status.
8. **Set `game_def.view` in `game.c`, as soon as a screen draws; every game needs it.** Without
   it the window shows the 320 × 256 capture window, whose first column is DIW x 120 and first
   row scanline 42. Kickstart's standard display window, `DIWSTRT` 0x81 with `DDFSTRT` 0x38,
   starts a lores picture at DIW x 129, so a game that uses it appears shifted right by 9
   pixels, with black columns at the left and the last 9 columns cut off at the right. That
   is the normal case, not a quirk of one game: Deuteros and Lemmings both had it (Deuteros
   until it set `view`, Lemmings until 2026-10-10). Headless shots and every `pixdiff` look
   right, because they are the capture window too, so only a window run shows it.
    - `x`: the DIW x of the picture's first pixel: `DDFSTRT` × 2 + 17 in lores, × 2 + 9 in
      hires (`docs/runtime.md`, "Timing"). `w`: 320 for a standard screen.
    - `y`, `h`: the first scanline and the height of the picture, from the copper's `BPLCON0`
      `WAIT` or `DIWSTRT`. Use the tallest screen the game shows, so no screen is cut: a
      shorter one sits inside it with black below.
    - The rectangle must lie inside the frame row, DIW x 120–454 (`docs/emulator.md`, "Capture
      window"); the frontend refuses to start otherwise (`game_def.view … lies outside the
      frame buffer`).
    - Check it with `<id> --scale 2` and a script that reaches the screen: the window opens at
      twice the view, and the picture's first column and last column are both visible.
      `games/lemmings/docs/format.md`, "Screens", and `games/deuteros/docs/format.md` hold the
      two games' values and their evidence.

## 7. Writing a hack

An ordered checklist:

1. Find the code with the WinUAE debugger and a runtime `--dump`.
2. Write the `docs/hacks.md` entry first.
3. Compute the anchors with `python tools/anchor.py <ramdump> --addr 0x… --len N`.
4. Implement `hack_<hackid>.c` and list it in `game_def.hacks`.
5. Add a script that turns the hack on with `hack <hackid> on` and shows its effect.
6. Re-run the dump's verification gate with every hack off, and confirm that the figures have
   not changed.
