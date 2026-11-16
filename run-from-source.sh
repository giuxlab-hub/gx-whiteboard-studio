#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT/build/gx-whiteboard-studio"
if [[ ! -x "$BIN" ]]; then
  printf 'Binario mancante: %s\nCompilare con CMake prima di avviare.\n' "$BIN" >&2
  exit 1
fi
if [[ -z "${DISPLAY:-}" ]]; then
  if [[ -S /tmp/.X11-unix/X0 ]]; then
    export DISPLAY=:0
    if [[ -z "${XAUTHORITY:-}" && -f "$HOME/.Xauthority" ]]; then
      export XAUTHORITY="$HOME/.Xauthority"
    fi
  else
    printf 'Display X11 non disponibile. Aprire dalla sessione grafica XFCE.\n' >&2
    exit 1
  fi
fi
exec "$BIN" "$@"
