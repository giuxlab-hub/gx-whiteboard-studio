# GX Whiteboard Studio 0.1.0-rc1 — QA status

This report summarizes the actual F08 development and owner-test evidence. It does not turn an unperformed test into a pass.

## Accepted F08 checks

- **Release build** on Debian 13.
- **17 Release regression suites** including drawing, image composition, synthetic USB/IP cameras, media Z-order, CPU and NVIDIA H.264/AAC test recordings, recording UI in English and Italian, image drag-and-drop while recording, scenes, autosave, independent per-scene Undo/Redo, synthetic scene changes during recording, FFmpeg failure handling, backup and recovery, and a 24-scene synthetic-media stress project (~49 MB).
- **Four ASAN/UBSAN suites:** scenes, scene Undo, recording resilience and image drag-and-drop during recording.
- **APT installation simulation**, including dependency resolution. An actual global `apt install` on the development workstation was intentionally not performed.
- **Extraction of the original Debian package** into an isolated folder and MD5 verification of its eight installed files.
- **Opening `.gxwp` and `.gxwb`** through the executable extracted from the Debian package with isolated XDG settings.
- Desktop entry, SVG, MIME declarations, gettext `.mo` catalogs, HTML manuals and Help menu checked in F08.
- Owner-performed tests using the **real Wacom tablet**, **real webcam microphone**, **Mute** and **drag-and-drop from Thunar**. These statements come from the original owner acceptance and are not new hardware tests performed for GitHub.

The accepted binary inside the original `.deb` has been compared byte-for-byte against the approved F08 Release executable. The two compiled gettext catalogs and other installed resources also match the F08 files.

## Explicitly incomplete or unsupported claims

1. **Continuous real-world 30-minute recording: NOT TESTED.** Short H.264/AAC or NVENC tests do not prove uninterrupted long-duration operation.
2. **Real network-camera streaming: NOT WORKING in the owner's practical trials.** Local IP discovery succeeded; authentication was not investigated by owner choice. The local synthetic RTSP/MJPEG tests cannot validate the real cameras.
3. **Undo/Redo history is not persisted across program restarts.** History is separate for each scene only during the running session.
4. **Global Debian installation: NOT PERFORMED** as part of F08 testing; an APT simulation and isolated package extraction were used.
5. No assertion of general Wayland, other distributions, other architectures or exhaustive external-camera compatibility.

## Synthetic regression fixtures included with this source release

The original development reports identify these files as **synthetic**:

- `assets/f03_test_chart.png` and `assets/f03_test_microscope.png` — synthetic F03 test images.
- `assets/f05_test_320x240.h264` — synthetic F05 RTSP/H.264 test bitstream.
- `assets/f07_3_noise_stress.png` — synthetic F07.3 high-entropy stress image.

The image fixtures do not carry captured real-camera content. Synthetic MJPEG and RTSP test servers live in `tools/test_mjpeg_server.py` and `tools/test_rtsp_server.py` and bind to `127.0.0.1`. No personal screenshots, recordings or camera credentials have been placed in the source tree.

The C++ application contains opt-in `--smoke` and other `--smoke-*` regression entry points. These can create files: use a separate checkout with isolated XDG settings and an untracked `results/` directory. The GitHub publication workflow does not claim that every F08 hardware and sanitizer case was rerun.

## Original documents used

- F08 release report: `REPORT_F08.md` in the owner's approved F08 working directory.
- F08 installation/test handoff: `TEST_UTENTE_IT.md`.
- Original project `PLAN.md` and `README.md`, including owner acknowledgments for the hardware and Mute checks.

These development handoffs were consulted to create this concise public-ready QA summary, rather than copied wholesale with personal workstation paths or development results.
