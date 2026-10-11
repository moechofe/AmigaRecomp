# Conventions — style, verification, commits

There is no formatter and no linter. The only CI is `.github/workflows/dist.yml`
(`docs/runtime.md`, "Distributable builds in CI"). These rules are kept by reading them.

## Formatting

`.editorconfig` is authoritative.

| Files | Indent |
| --- | --- |
| everything else (C, C++, …) | tab, width 2 |
| `*.md`, `*.py` | 4 spaces |
| `*.yml`, `*.yaml`, `*.json`, `*.toml` | 2 spaces |
| `*.cmake`, `*.txt` | tab, width 2 |

All files: LF line endings, UTF-8, trailing whitespace trimmed, final newline. Markdown prose
wraps at 96 columns.

C style: Allman braces, and `if(cond)` with no space.

## C

- C99. The one exception is `frontend/menu.cpp`, which is C++17 because RmlUi is, and has a
  C-callable header.
- Module prefixes:
    - in `core/`: `amiga_ mem_ cpu_ sched_ custom_ copper_ denise_ blit_ paula_ cia_ hle_
      boot_ interp_ script_ state_ game_`;
    - in `frontend/`: `sdl_`;
    - in `games/<id>/`: `<id>_`.
- No typedefs for structs or enums. Names are PascalCase, and enum members are prefixed with
  the enum's name.
- `core/` uses fixed-size arrays, no `malloc` and no file I/O, and includes no SDL or OS
  header.
- Every hardware constant cites its source: the HRM, the NDK, or a measurement doc. An
  unmeasured value is not written. Anything unimplemented calls `amiga_fatal()`.

## Game folders

- A game folder is self-contained (decision 013), and nothing outside it names a game.
- Game code touches the machine only through `game.h` and the runtime's public helpers.
- A hack never holds game bytes; it uses anchors (decision 006).

## Python in `tools/`

- Python 3.11+ and the standard library only.
- Tools read archives in place and open `games/<id>/disks/` read-only.
- A tool that works on one game takes `--game <id>` and writes only under `games/<id>/build/`.
  Any other tool writes under the root `build/`.
- One file, one job, with `--help`.
- Hex offsets are printed at the same width the docs use.

## Documentation

- Every claim cites evidence: an ADF name plus an offset, a dated emulator observation, or a
  named source.
- Unknowns are written down as unknown, and inferences are marked as inferences.
- Code comments add no obvious narration and no decision history.
- One owner per fact (decision 009): link to the owner instead of restating.

## Verification — read before claiming anything works

Four things count, and a clean compile is none of them:

1. a pixel diff against WinUAE;
2. a headless script reaching a named screen with no fatal;
3. `ctest` unit tests over synthetic data;
4. a manual, honest look at the result.

## Commits

Do not commit.

## Progress

- Tick a box in a game's `PLAN.md` only once its *Done when* has been observed, and append one
  or two lines to `PROGRESS.md`.
- Changes to scope or order are proposed in conversation.
