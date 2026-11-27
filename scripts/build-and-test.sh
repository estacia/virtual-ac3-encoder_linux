#!/usr/bin/env bash
set -euo pipefail

if ! command -v meson >/dev/null 2>&1; then
  echo "meson not found. On CachyOS run: sudo pacman -S --needed meson ninja pkgconf" >&2
  exit 1
fi

meson setup build --buildtype=release --reconfigure 2>/dev/null || meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
