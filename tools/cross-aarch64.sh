#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 abdulahmad and contributors
#
# Cross-build a port for 64-bit ARM Linux (Knulli, muOS, ROCKNIX on the Anbernic RG35XX Plus/H/SP and other handhelds).
#   sh cross-aarch64.sh <port-dir> [make args...]      e.g. sh native/overbar/tools/cross-aarch64.sh . build/nhl94
# Needs on a Debian/Ubuntu x86 host: gcc-aarch64-linux-gnu g++-aarch64-linux-gnu, and the arm64 apt source for SDL2.
# SDL2 headers and libraries come from a private sysroot (default build/sdl2-arm64, made with apt-get download + dpkg -x),
# so the host's own packages are untouched. The program is linked against SDL2 as a shared library, which the handheld
# provides; its dependencies are not in the sysroot, so unresolved symbols inside shared libraries are allowed.
set -e
PORT=${1:?usage: cross-aarch64.sh <port-dir> [make args]}; shift
cd "$PORT"
SYSROOT=${SDL2_SYSROOT:-$PWD/build/sdl2-arm64}
if [ ! -f "$SYSROOT/usr/include/SDL2/SDL.h" ]; then
    mkdir -p "$SYSROOT" build/sdl2-debs
    (cd build/sdl2-debs && apt-get download libsdl2-dev:arm64 libsdl2-2.0-0:arm64)
    for d in build/sdl2-debs/*.deb; do dpkg -x "$d" "$SYSROOT"; done
    ln -sf libSDL2-2.0.so.0 "$SYSROOT/usr/lib/aarch64-linux-gnu/libSDL2.so"
fi
export PKG_CONFIG_LIBDIR=$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig PKG_CONFIG_SYSROOT_DIR=$SYSROOT
export CPATH=$SYSROOT/usr/include/aarch64-linux-gnu
exec make CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ AR=aarch64-linux-gnu-ar \
    LDFLAGS="-Wl,--unresolved-symbols=ignore-in-shared-libs" "$@"
