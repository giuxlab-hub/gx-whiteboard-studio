# GX Whiteboard Studio

**Version 0.1.0 — release candidate `v0.1.0-rc2`**
Native C++17 / Qt6 desktop whiteboard and audiovisual recorder for **Debian 13 (amd64), XFCE/X11**.

GX Whiteboard Studio is an independent GiuxLab application, separate from GX-TXT.

## Features

- **Wacom and drawing:** pressure-sensitive vector ink, pen, highlighter, eraser, shapes, text, object selection, Undo/Redo, pan, zoom and fullscreen.
- **Multimedia canvas:** resizable PNG/JPEG/WebP and USB webcam boxes, image insertion directly from Thunar via drag-and-drop, crop/aspect/opacity, adjustable media Z-order with drawing always above media, and experimental RTSP/HTTP-MJPEG sources.
- **Scenes and projects:** multiple scenes, `.gxwp` project files, `.gxwb` standalone whiteboards, embedded images, atomic save, automatic checkpoints, previous-version backups and recovery.
- **FFmpeg recording:** canvas-only H.264 video via `libx264` or supported NVIDIA `h264_nvenc`; selectable PulseAudio microphone with AAC sound and application Mute; MKV, optional MP4 remux and local diagnostic recording reports.
- **Languages and desktop:** GNU gettext English and Italian, local HTML manuals, Debian desktop integration, SVG icon and MIME associations.

## Install the Debian package

Download `gx-whiteboard-studio_0.1.0-2_amd64.deb` from the [0.1.0-rc1 GitHub release](https://github.com/giuxlab-hub/gx-whiteboard-studio/releases/tag/v0.1.0-rc2), then run:

```bash
sudo apt install ./gx-whiteboard-studio_0.1.0-2_amd64.deb
gx-whiteboard-studio
```

The application appears in the Graphics menu. Double-click `.gxwp` and `.gxwb` files in Thunar. The package contains the executable, compiled language catalogs, HTML manuals, icon, desktop file and MIME declarations. No background service is installed.

**Original approved package SHA-256:**

```text
81f4201acd354398ce748655f50feead9a94ea21b3982c47a5c8b1d89730d3c5
```

To uninstall:

```bash
sudo apt remove gx-whiteboard-studio
```

Removing the application does not delete documents, recordings or user XDG settings.

## Build from source

For Debian 13 amd64:

```bash
sudo apt install build-essential cmake qt6-base-dev python3 \
    dpkg-dev fakeroot ffmpeg pulseaudio-utils \
    qt6-qpa-plugins qt6-svg-plugins libxcb-cursor0

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel 2
./run-from-source.sh
```

CMake requires version 3.20+ and C++17. Only Qt6 Widgets and Network are linked; the application uses external FFmpeg and PulseAudio utilities for recording. The included `tools/compile_po.py` compiles gettext `.po` sources into `.mo` catalogs. Use an X11 desktop session to launch interactively.

To create a **new local test Debian package**, after building:

```bash
mkdir -p results
DESTDIR="$PWD/package-root" cmake --install build
./tools/build_deb.sh
```

The test package is written to `dist/`, which is ignored by Git. This operation does not install packages on the host. The GitHub release asset is the **unchanged approved F08 original**, not a rebuilt test artifact.

See [Debian dependencies](docs/DEBIAN_13.md) and [QA status](docs/QA_STATUS.md).

## Documentation

- [English user manual](docs/manual_en.html)
- [Manuale utente italiano](docs/manual_it.html)
- [Documentazione italiana](README.it.md)
- [Release notes](RELEASE_NOTES.md)

Synthetic test images and a synthetic H.264 stream in `assets/` support the original C++ smoke options. The loopback camera test servers in `tools/` bind to localhost only. The test options can create documents and video files: use a separate checkout and isolated XDG environment.

## Release candidate limitations

1. **A continuous 30-minute real-world recording has not been tested.** Practical-use endurance verification is outstanding.
2. **Real IP cameras were discovered but their streams did not work.** Authentication was deliberately not investigated. Synthetic local RTSP/MJPEG tests are not evidence of working real IP cameras.
3. Undo/Redo history is separate for each scene while the program runs, **but history is not persisted across application restarts**.

F08 recorded successful Release build, 17 regression suites, four ASAN/UBSAN suites, simulated APT installation and verification of the extracted Debian package. The owner tested physical Wacom, webcam microphone, Mute and Thunar drag-and-drop. **Global Debian installation was not performed** during F08 qualification.

**License:** GX Whiteboard Studio is released under the **GNU General Public License, version 3 (GPL-3.0-or-later)**, as specified by the owner. See [LICENSE](LICENSE) for the complete terms. This repository is public.

Debian revision 0.1.0-2 includes the owner-approved GPL-3.0-or-later SPDX and copyright metadata, full license text and installed Debian copyright file. It upgrades the previous 0.1.0-1 Debian revision while the application version remains 0.1.0.
