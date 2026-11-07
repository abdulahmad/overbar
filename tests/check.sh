#!/bin/sh
# What CI runs: the tests with warnings as errors, 64- and 32-bit, then every source with the
# strictest port's flags on Linux and on both Windows cross compilers (the ports build Overbar
# 64-bit with UCRT64 and 32-bit with MINGW32). A compiler that is not installed is skipped.
# SDL2_MINGW: the unpacked SDL2-devel-*-mingw folder, for the Windows SDL headers.
set -e
cd "$(dirname "$0")/.."
make clean >/dev/null
make test WERROR=-Werror
if echo 'int main(void){return 0;}' | ${CC:-cc} -m32 -x c - -o /dev/null 2>/dev/null; then
    make clean >/dev/null
    make test CFLAGS="-m32 -std=c99 -Wall -Wextra -Werror -Iinclude"
fi
make clean >/dev/null
make check
for t in i686 x86_64; do
    cc=$t-w64-mingw32-gcc
    command -v $cc >/dev/null 2>&1 || { echo "skip $cc (not installed)"; continue; }
    sdl=
    [ -n "$SDL2_MINGW" ] && sdl="-I$SDL2_MINGW/$t-w64-mingw32/include/SDL2"
    make check CC=$cc SDL_CFLAGS="$sdl"
done
echo "check ok"
