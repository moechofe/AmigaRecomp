A collection of native Windows, Linux and macOS executables that run Amiga games from your own disk images, with some quality-of-life improvements.

AmigaRecomp runs Amiga games natively, one program per game. The game's code runs on the Musashi 68000 interpreter, alongside a recreated Amiga chipset (copper, bitplanes, sprites, blitter, CIAs and Paula audio) written with Claude. Kickstart 1.3 is replaced by C code, so no ROM is needed.

The project contains _no game code and no game data_. Some optional hacks slightly improve the experience.

Players need their own copies of the games as `ADF` or `ADZ` files, either loose or inside a `GZ` or `ZIP` archive.

All games has a way to swap disks, fullscreen and stereo output. Some games has quality-of-life improvement.

| Games | Windows | Linux | macOS | Hacks |
|---|---|---|---|---|
| Deuteros: The Next Millennium<br>1991 Activision | [x86_64](https://github.com/$REPO/releases/download/$TAG/deuteros-windows-x86_64.exe) | [x86_64](https://github.com/$REPO/releases/download/$TAG/deuteros-linux-x86_64.AppImage)<br>[aarch64](https://github.com/$REPO/releases/download/$TAG/deuteros-linux-aarch64.AppImage) | [universal](https://github.com/$REPO/releases/download/$TAG/deuteros-macos-universal.zip) | OS cursor<br>Key to advance time |
| Lemmings<br>1991 Psygnosis | [x86_64](https://github.com/$REPO/releases/download/$TAG/lemmings-windows-x86_64.exe) | [x86_64](https://github.com/$REPO/releases/download/$TAG/lemmings-linux-x86_64.AppImage)<br>[aarch64](https://github.com/$REPO/releases/download/$TAG/lemmings-linux-aarch64.AppImage) | [universal](https://github.com/$REPO/releases/download/$TAG/lemmings-macos-universal.zip) | OS cursor |
| Lotus Turbo Challenge 2<br>1991 Gremlin Graphics | [x86_64](https://github.com/$REPO/releases/download/$TAG/lotus2-windows-x86_64.exe) | [x86_64](https://github.com/$REPO/releases/download/$TAG/lotus2-linux-x86_64.AppImage)<br>[aarch64](https://github.com/$REPO/releases/download/$TAG/lotus2-linux-aarch64.AppImage) | [universal](https://github.com/$REPO/releases/download/$TAG/lotus2-macos-universal.zip) |  |
| Pinball Fantasies<br>1992 21st Century Entertainment | [x86_64](https://github.com/$REPO/releases/download/$TAG/pinballfantasies-windows-x86_64.exe) | [x86_64](https://github.com/$REPO/releases/download/$TAG/pinballfantasies-linux-x86_64.AppImage)<br>[aarch64](https://github.com/$REPO/releases/download/$TAG/pinballfantasies-linux-aarch64.AppImage) | [universal](https://github.com/$REPO/releases/download/$TAG/pinballfantasies-macos-universal.zip) |  |
| Sid Meier's Pirates!<br>1990 MicroProse | [x86_64](https://github.com/$REPO/releases/download/$TAG/pirates-windows-x86_64.exe) | [x86_64](https://github.com/$REPO/releases/download/$TAG/pirates-linux-x86_64.AppImage)<br>[aarch64](https://github.com/$REPO/releases/download/$TAG/pirates-linux-aarch64.AppImage) | [universal](https://github.com/$REPO/releases/download/$TAG/pirates-macos-universal.zip) |  |
| The Settlers<br>1993 Blue Byte | [x86_64](https://github.com/$REPO/releases/download/$TAG/settlers-windows-x86_64.exe) | [x86_64](https://github.com/$REPO/releases/download/$TAG/settlers-linux-x86_64.AppImage)<br>[aarch64](https://github.com/$REPO/releases/download/$TAG/settlers-linux-aarch64.AppImage) | [universal](https://github.com/$REPO/releases/download/$TAG/settlers-macos-universal.zip) | OS cursor |

This release rebuilds $REBUILT. The other games' files are copied unchanged from the previous release; `manifest.txt` gives each file's SHA-256 and the tag and commit it was built from.
