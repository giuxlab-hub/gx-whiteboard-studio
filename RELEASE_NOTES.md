# GX Whiteboard Studio 0.1.1

**Tag:** `v0.1.1`
**Application version:** `0.1.1`
**Debian version:** `0.1.1-1`
**Platform:** Debian 13 amd64, XFCE/X11
**License:** GNU GPL-3.0-or-later · Copyright (C) 2026 GiuxLab

## What's new

### Image library — remove a thumbnail without deleting files

Right-click a thumbnail in the **Images** gallery and choose **Remove from library** (Italian: **Rimuovi dalla libreria**). This updates the image-library list only. The original file on disk is not deleted and any images already embedded in a scene or project remain intact. The change is written to QSettings/XDG, so the removed entry stays absent after restart. It can be added to the library again at any time.

### Complete offline guides in both languages

**Help → Guides** now offers **Complete manual (Italian)** and **Complete manual (English)**. Both HTML manuals include an index and 13 chapters, covering the interface, Wacom drawing, images and cameras, scenes, recording, backup/recovery, image-library maintenance, diagnostics and licensing.

### Version and credits

**Help → About** displays **GX Whiteboard Studio 0.1.1**, author **GiuxLab**, copyright **(C) 2026 GiuxLab** and license **GPL-3.0-or-later**. Updated English and Italian gettext catalogs are included.

## Existing features

Pressure-sensitive Wacom drawing and eraser, vector tools, selection, pan/zoom and Undo/Redo, resizable image/webcam boxes and drag-and-drop from Thunar. Multiple `.gxwp` scenes and `.gxwb` boards with autosave, backups and recovery. FFmpeg recording with H.264 `libx264`/NVIDIA NVENC, selectable PulseAudio microphone, AAC, Mute, MKV files, optional MP4 conversion and recording diagnostics. RTSP/HTTP-MJPEG network camera support remains experimental.

## Debian installation or upgrade

Download the **original approved** `gx-whiteboard-studio_0.1.1-1_amd64.deb` asset and run:

```bash
sudo apt install ./gx-whiteboard-studio_0.1.1-1_amd64.deb
```

To run: `gx-whiteboard-studio`. To remove: `sudo apt remove gx-whiteboard-studio`. Existing user projects and recordings are not deleted.

The Debian package declares Qt6 Core/GUI/Widgets/Network, FFmpeg, PulseAudio utilities, Qt6 QPA/SVG plugins, libxcb-cursor0 and the required C/C++ runtimes. See [Debian dependencies](https://github.com/giuxlab-hub/gx-whiteboard-studio/blob/v0.1.1/docs/DEBIAN_13.md).

## Verification

The approved F08_1 handoff reports **18 regression suites**, Qt context-menu testing, unchanged image-file SHA-256 and canvas content, QSettings persistence across restart, re-addition of removed images, both 13-chapter manuals, ASAN/UBSAN checks, Debian package checks and a successful simulated APT upgrade. The global 0.1.1 package installation was not performed in that validation. The GitHub publishing checkout and release asset have separate source/byte-integrity checks.

## Known limitations

- **Continuous real-world 30-minute recording has not yet been validated.** It remains for practical-use testing.
- **Authenticated IP-camera streaming remains unvalidated.** Detection alone does not establish streaming compatibility.
- **Per-scene Undo/Redo history is not persisted after application restart.** Saved scene contents are unaffected.

## Original Debian package checksum

**SHA-256:**

`2372e116f95c00c07be0264b6661e1930899d37550dd0e9cd3b1c870469f0a8a`

The previous `0.1.0` releases and packages remain in their historical GitHub releases; the `v0.1.1` release carries **only the new 0.1.1-1 Debian asset and its SHA256SUMS**.
