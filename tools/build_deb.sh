#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 GiuxLab
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="$ROOT/package-root"
OUT="$ROOT/dist/gx-whiteboard-studio_0.1.1-1_amd64.deb"
BIN="$STAGE/usr/bin/gx-whiteboard-studio"
test -x "$BIN" || { printf 'Missing staged executable. Run cmake --install first.\n' >&2;exit 1; }
mkdir -p "$STAGE/DEBIAN" "$ROOT/dist" "$ROOT/debian"
# dpkg-shlibdeps needs a Debian control descriptor even when building
# the binary package with dpkg-deb rather than debhelper.
cat > "$ROOT/debian/control" <<'DEB_CONTROL'
Source: gx-whiteboard-studio
Section: graphics
Priority: optional
Maintainer: GiuxLab
Standards-Version: 4.7.0

Package: gx-whiteboard-studio
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: Native Wacom whiteboard with recording
DEB_CONTROL
# Installed paths follow Debian permissions, and uninstall never touches
# the user's XDG configuration, projects or recording files.
find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE/usr" -type f -exec chmod 644 {} +
chmod 755 "$BIN"
cd "$ROOT"
dependency_info="$(dpkg-shlibdeps -O -e"$BIN" 2>"$ROOT/results/dpkg-shlibdeps-warning.log")"
dynamic_deps="${dependency_info#shlibs:Depends=}"
test -n "$dynamic_deps"
size="$(du -sk "$STAGE/usr" | cut -f1)"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: gx-whiteboard-studio
Version: 0.1.1-1
Section: graphics
Priority: optional
Architecture: amd64
Maintainer: GiuxLab
Installed-Size: $size
Depends: $dynamic_deps, ffmpeg, pulseaudio-utils, qt6-qpa-plugins, libxcb-cursor0, qt6-svg-plugins
Description: Native Wacom whiteboard and FFmpeg video recorder
 GX Whiteboard Studio provides pressure-sensitive drawing, scene and
 project management, local and IP camera boxes and H.264/AAC recording.
 GNU gettext localization for Italian and English is included.
EOF
chmod 0644 "$STAGE/DEBIAN/control"
(
    cd "$STAGE"
    find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
chmod 0644 "$STAGE/DEBIAN/md5sums"
fakeroot dpkg-deb --build --root-owner-group "$STAGE" "$OUT"
dpkg-deb --info "$OUT"
dpkg-deb --contents "$OUT" > "$ROOT/results/package-manifest.txt"
sha256sum "$OUT" > "$ROOT/dist/SHA256SUMS"
printf '\nPACKAGE %s\n' "$OUT"
cat "$ROOT/dist/SHA256SUMS"
