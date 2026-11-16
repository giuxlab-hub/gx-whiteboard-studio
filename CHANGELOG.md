# Changelog

## 0.1.1 — 2026-11-16

- Added a right-click context menu to image-library thumbnails, with a localized **Remove from library / Rimuovi dalla libreria** command.
- Removal affects only the library reference. Original files and images embedded in any scene stay unchanged.
- Saved the updated image-library list in QSettings/XDG, surviving application restarts; removed images may be re-added.
- Expanded the English and Italian offline user manuals to **13 chapters** each, with a table of contents and **Help → Guides** navigation.
- Updated **Help → About** with version 0.1.1, author GiuxLab, copyright (C) 2026 and GPL-3.0-or-later.
- Updated the Debian package to **0.1.1-1**, including the current executable, manuals and gettext catalogs.
- F08_1 acceptance: 18 regression suites, Qt GUI menu tests, ASAN/UBSAN, package verification and APT upgrade simulation.

Known limits: real-world 30-minute recording test remains pending, authenticated IP-camera streams are unvalidated, and per-scene Undo/Redo histories are not saved across application restarts.

## 0.1.0 — release candidates (history)

- **v0.1.0-rc2:** introduced the approved GPL-3.0-or-later source/license metadata and Debian package 0.1.0-2.
- **v0.1.0-rc1:** initial published C++17/Qt6 whiteboard, multi-scene project, Wacom and FFmpeg release candidate.

These historical tags and their binaries are preserved unchanged.
