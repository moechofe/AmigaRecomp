![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![gamedata](https://img.shields.io/badge/game%20data-not%20included-FF453A)
[![License: MIT](https://img.shields.io/github/license/moechofe/AmigaRecomp)](LICENSE)
[![dist](https://github.com/moechofe/AmigaRecomp/actions/workflows/dist.yml/badge.svg)](https://github.com/moechofe/AmigaRecomp/actions/workflows/dist.yml)

## AmigaRecomp

A collection of native Windows, Linux and macOS executables that run Amiga games from your own disk images, with some quality-of-life improvements.

AmigaRecomp runs Amiga games natively, one program per game. The game's code runs on the Musashi 68000 interpreter, alongside a recreated Amiga chipset (copper, bitplanes, sprites, blitter, CIAs and Paula audio) written with Claude. Kickstart 1.3 is replaced by C code, so no ROM is needed.

The project contains _no game code and no game data_. Some optional hacks slightly improve the experience.

Players need their own copies of the games as `ADF` or `ADZ` files, either loose or inside a `GZ` or `ZIP` archive.

## Legal

AmigaRecomp is an independent project. It is not affiliated with, endorsed by or sponsored by Commodore, Amiga Inc., Cloanto, or the publishers or developers of any game it can run. Amiga, Kickstart, Workbench and all game titles are trademarks or copyrights of their respective owners, and are used here only to identify compatibility.

This repository and its releases contain no game code, no game data and no Kickstart ROM. The Kickstart replacement and the chipset are original code written from public hardware documentation. You must own the games you run and supply your own `ADF` or `ADZ` images.

The project's source code is released under the [MIT License](LICENSE), which does not cover any game or third-party trademark. Third-party components and their licences are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

This is not legal advice. Rights holders can reach me through the issue tracker.
