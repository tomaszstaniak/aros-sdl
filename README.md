# aros-sdl

SDL for AROS programs, as one kit per system: static SDL 3 with SDL3_image and the
libiconv it uses, built the way the AROS contrib tree builds them. A program links the
kit instead of carrying its own SDL build.

| Target | System |
| --- | --- |
| `aros-x86_64-abiv1-pc` | mainline AROS, x86_64 |
| `aros-aarch64-abiv1-raspi` | mainline AROS, Raspberry Pi |
| `aros-x86_64-abiv11-pc` | ABIv11 distributions, x86_64 |

All three kits build. [LibreSprite for AROS](https://github.com/tomaszstaniak/aros-libresprite)
uses them on all three systems.

## Contents

- SDL 3.4.12 with the AROS port from [contrib](https://github.com/aros-development-team/contrib)
  (`SDL3/main`): Intuition windows, CyberGraphics, AHI audio, software and OpenGL renderers.
- SDL3_image 3.4.4, configured as contrib configures it (PNG through the system libpng,
  JPEG, GIF, BMP and other formats through SDL's built-in decoders).
- GNU libiconv 1.19 with contrib's AROS changes.

Sources are downloaded by the build script and checked against the SHA-256 values in
`upstreams.json`. contrib is used at the pinned commit.

## Changes to SDL 3

`patches/sdl3/` holds changes on top of the contrib port, applied in the order of `series`:

- 0001: button events at the pointer position of the click; pointer deltas only in
  relative mouse mode.
- 0002: the window size SDL reports matches the window after `WA_AutoAdjust`.
- 0003: typed text through the keymap and the system character set (dead keys included).
- 0004: iconify to a Workbench AppIcon, and replies to Workbench messages so dropped
  icons do not stop Workbench.
- 0005: a program can put its own menu strip into the window's Intuition menu.
- 0006: window titles, clipboard text and dropped file names are converted between
  UTF-8 and the system character set.
- 0007: the ABIv11 SDK has no OOP headers, so ABIv11 kits use SDL's dummy joystick driver.
- 0008: `langinfo.h` is included only where it is used.
- 0009: windows open as smart refresh, so moving or covering a window does not make the
  program redraw it; vertical-only window moves are reported.
- 0010: the window surface of `SDL_GetWindowSurface` is plain memory shown with
  `WritePixelArray`. Writing into a bitmap's own memory gave slanted stripes on a
  display driver that keeps bitmaps in video memory.

`build.py` also adds the GL link library where static SDL3 needs it (ABIv11) and lets
SDL3_image link after SDL3.

## Build

    python3 build.py <target>

Compilers and SDKs come from `local.json` (copy `local.example.json`) or from `AROS_CC`,
`AROS_CXX`, `AROS_AR` and `AROS_SDK`. The kit appears in `build/<target>/kit`:
`include/`, `lib/`, `lib/cmake/SDL3` for `find_package(SDL3)`, licence texts and
`BUILD.json` (sources, hashes, compiler).

Before building, fetch contrib:

    git clone --filter=blob:none --no-checkout https://github.com/aros-development-team/contrib.git upstream/contrib
    git -C upstream/contrib sparse-checkout set --no-cone /SDL2/ /SDL3/ /development/libs/libiconv/
    git -C upstream/contrib checkout <commit from upstreams.json>

## Using it

    <cc> -I<kit>/include ... <kit>/lib/libSDL3_image.a <kit>/lib/libSDL3_static.a <kit>/lib/libiconv.a -lpng -lz

With CMake: `-DSDL3_DIR=<kit>/lib/cmake/SDL3`. A kit built for one target does not work
on another.

## Self-test

`tests/sdl3-selftest.c` checks start-up, iconv, a PNG round trip, a window and each
renderer; `aros-sdl3-selftest [software|opengl|all] [seconds]`.

## Licences

SDL and SDL3_image: zlib licence. libiconv: LGPL 2.1 or later (static linking: see its
terms). The build scripts in this repository: MIT.
