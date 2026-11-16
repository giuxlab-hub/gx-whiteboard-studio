# Debian 13: build and runtime dependencies

GX Whiteboard Studio 0.1.1-1 targets Debian 13 (trixie), amd64, Qt6, XFCE/X11.

## Build dependencies

```bash
sudo apt install build-essential cmake qt6-base-dev python3 \
    dpkg-dev fakeroot ffmpeg pulseaudio-utils \
    qt6-qpa-plugins qt6-svg-plugins libxcb-cursor0
```

CMake must be version 3.20 or later. The application uses C++17, Qt6 Widgets and Network. `tools/compile_po.py` compiles GNU gettext catalogs using Python3; Python3 is required to build catalogs but not to run the installed application.

`tools/build_deb.sh` uses `dpkg-shlibdeps`, `fakeroot` and `dpkg-deb`. It writes to the checkout-local `results/`, `package-root/` and `dist/` directories. Prepare the `results/` directory before packaging.

## Debian 0.1.1-1 control dependencies

The approved Debian package declares:

```text
libc6 (>= 2.34)
libgcc-s1 (>= 3.0)
libqt6core6t64 (>= 6.8.2)
libqt6gui6 (>= 6.4.0)
libqt6network6 (>= 6.1.2)
libqt6widgets6 (>= 6.3.0)
libstdc++6 (>= 5)
ffmpeg
pulseaudio-utils
qt6-qpa-plugins
libxcb-cursor0
qt6-svg-plugins
```

FFmpeg handles H.264 and AAC recording and optional MKV-to-MP4 remux. `libx264` and NVIDIA NVENC require corresponding encoder support in the installed FFmpeg environment; NVIDIA driver and encoder availability are machine-specific.

## Compile locally

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel 2
./run-from-source.sh
```

Use an XFCE/X11 graphical session. The executable is under `build/`; generated `.mo` catalogs are under `build/locale/`.

## Build a local test .deb

```bash
mkdir -p results
DESTDIR="$PWD/package-root" cmake --install build
./tools/build_deb.sh
```

The script computes runtime dependencies from the staged executable. Its result is a locally rebuilt test package and is distinct from the original, approved F08 binary package used for the release asset.

The accepted Debian 0.1.1-1 package includes exactly these ten regular files:

- `/usr/bin/gx-whiteboard-studio`
- `/usr/share/applications/gx-whiteboard-studio.desktop`
- `/usr/share/icons/hicolor/scalable/apps/gx-whiteboard-studio.svg`
- `/usr/share/mime/packages/gx-whiteboard-studio.xml`
- `/usr/share/locale/it/LC_MESSAGES/gx-whiteboard-studio.mo`
- `/usr/share/locale/en/LC_MESSAGES/gx-whiteboard-studio.mo`
- `/usr/share/doc/gx-whiteboard-studio/manual_it.html`
- `/usr/share/doc/gx-whiteboard-studio/manual_en.html`
- `/usr/share/doc/gx-whiteboard-studio/LICENSE`
- `/usr/share/doc/gx-whiteboard-studio/copyright`

The original `.deb` executable and catalog bytes are identical to the approved F08 Release build. All seven noncompiled installed resources are identical to the original F08 desktop and documentation files.

## Uninstall

```bash
sudo apt remove gx-whiteboard-studio
```

No automatic deletion of local `.gxwp`, `.gxwb`, recordings, or user XDG data is part of the package's removal workflow.

License is GPL-3.0-or-later with copyright (C) 2026 GiuxLab. The Debian package carries the original upstream GPLv3 full text and GPL-3+ metadata from packaging/copyright. The application version is 0.1.1.
