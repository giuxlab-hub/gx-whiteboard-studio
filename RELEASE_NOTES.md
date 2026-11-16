# GX Whiteboard Studio 0.1.0 — Release Candidate 2

**Tag:** `v0.1.0-rc2`
**Platform:** Debian 13 amd64, XFCE/X11
**Debian asset:** `gx-whiteboard-studio_0.1.0-2_amd64.deb`

GX Whiteboard Studio is an independent C++17/Qt6 desktop whiteboard with audiovisual recording.

## Features

- Wacom pressure-sensitive pen and eraser, vector drawing, selection, shapes, Undo/Redo, pan and zoom.
- Images inserted directly from Thunar by drag-and-drop; resizable image and USB webcam boxes with media Z-order, with drawings always above the media.
- Experimental RTSP/HTTP-MJPEG sources and bounded private-subnet discovery.
- Multi-scene projects (`.gxwp`) and single boards (`.gxwb`), embedded images, autosave, atomic backups and recovery.
- FFmpeg H.264 (`libx264` or NVIDIA NVENC), selectable PulseAudio microphone with AAC and Mute, MKV output, optional MP4 conversion and diagnostic recording reports.
- Italian and English gettext catalogs, HTML manuals, SVG icon, desktop file and MIME associations.

## Install

Download the attached original, verified Debian package and run:

```bash
sudo apt install ./gx-whiteboard-studio_0.1.0-2_amd64.deb
```

Launch: `gx-whiteboard-studio` or the Graphics menu. Uninstall: `sudo apt remove gx-whiteboard-studio`. User projects, recordings and XDG settings are not deleted by uninstall.

The package declares dependencies on Qt6 Core/GUI/Network/Widgets, FFmpeg, PulseAudio utilities, Qt6 QPA and SVG plugins, libxcb-cursor0, and the Debian C/C++ runtimes. Refer to [docs/DEBIAN_13.md](docs/DEBIAN_13.md) for exact versions and CMake build instructions.

## Verification

The approved F08 development report records a successful Release build, 17 regression suites, four ASAN/UBSAN suites, APT installation simulation, and verification of the eight installed files in the earlier package revision. The updated 0.1.0-2 Debian package has ten installed files; their integrity is checked separately during this release publication. The owner also tested real Wacom input, the webcam microphone, Mute and actual Thunar drag-and-drop.

## Release candidate limitations

1. A **continuous real-world 30-minute recording has NOT been tested**. This is deferred to practical use and is required before calling the release fully endurance-validated.
2. **Real IP-camera streaming did not work** after network discovery. Authentication was deliberately not investigated. Local synthetic RTSP/MJPEG tests succeeded but do not establish real-camera interoperability.
3. **Per-scene Undo/Redo history is not saved across application restarts**. Scene content and projects can be saved, but their edit-command history does not persist.

The global Debian installation was not executed in F08 testing; only simulated APT installation and isolated package extraction were performed. No new endurance or live IP-camera validation is claimed here.

**Original Debian SHA-256:**

```text
81f4201acd354398ce748655f50feead9a94ea21b3982c47a5c8b1d89730d3c5
```

## Sources and documentation

- [README and build instructions](README.md)
- [Manual in English](docs/manual_en.html)
- [Manuale in italiano](docs/manual_it.html)
- [QA status](docs/QA_STATUS.md)

**License:** GNU General Public License version 3 (GPL-3.0-or-later), as confirmed by the owner. Full license text: [LICENSE](LICENSE).

Licensing update in Debian 0.1.0-2: the owner confirmed GPL-3.0-or-later. All current source/resource SPDX declarations, the full GPL text and the Debian copyright metadata are included. The application remains version 0.1.0. This candidate supersedes the earlier Debian revision.
