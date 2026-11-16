# GX Whiteboard Studio 0.1.1 — QA status

Licensing: **GPL-3.0-or-later**, Copyright (C) 2026 GiuxLab.

This is a source-grounded summary of the approved **F08_1/REPORT_F08_1.md**, **F08_1/GITHUB_RELEASE_HANDOFF.md** and **F08_1/TEST_UTENTE_IT.md**. Earlier test campaigns are not silently reclassified.

## Verified in the F08_1 release workflow

- C++17 / Qt6 Release build on Debian 13.
- **18 Release regression suites**, covering the established drawing, media, scene, recording, persistence, recovery and image-gallery workflows.
- **Qt GUI context-menu tests:** the right-click action targets the thumbnail under the pointer, not another previously selected image; no removal is offered on empty gallery space.
- **Non-destructive removal:** the original image remains on disk, with an unchanged SHA-256; an image embedded in the canvas and serialized scene state remains unchanged.
- **Persistence and recovery:** library references are saved with QSettings/XDG, the removed entry stays absent after simulated restart with a fresh application instance, and it can be added again.
- **Help/About:** IT/EN menu strings, About metadata, two offline complete HTML manuals with a table of contents and **13 chapters**.
- **AddressSanitizer and UndefinedBehaviorSanitizer**, including image-box and multi-scene workflows.
- The original **Debian 0.1.1-1 package** passed package checks and MD5 validation of its **ten installed files**.
- An extracted executable was launched in an isolated environment, opening both `.gxwp` and `.gxwb` documents.
- **APT upgrade simulation** succeeded; no actual system-wide upgrade was performed as part of F08_1 acceptance.

Historical owner checks include real Wacom pen input, webcam microphone, Mute and drag-and-drop from Thunar. These were performed on prior approved stages; they are not represented as repeated physical-device tests on F08_1.

## Publication integrity checks

The GitHub source is copied from the approved F08_1 tree and checked byte-for-byte before committing. Published `.deb` bytes must equal the accepted F08_1 package, whose SHA-256 is:

`2372e116f95c00c07be0264b6661e1930899d37550dd0e9cd3b1c870469f0a8a`

The package must be attached to GitHub Release `v0.1.1`, **not** committed to the Git repository. Locally rebuilt test packages have their own archive metadata and are never substituted for the approved file.

## Known limits and test boundaries

1. **Continuous real-world 30-minute recording remains unverified**; the owner will test this during practical use.
2. **Authenticated real-world IP-camera streams remain unvalidated.** Discovery and synthetic network tests are not evidence of successfully receiving those streams.
3. **Undo/Redo history is not persisted across application restarts**, although scene contents are saved.
4. The global installation of the new package was not performed during the F08_1 release checks; APT upgrade was simulated.

## Synthetic regression materials

The repository carries only four previously verified synthetic image/video fixtures used by C++ smoke checks, plus the localhost test servers. It does not include personal recordings, screenshots, webcam captures, real camera credentials, user projects or user XDG settings.
