#!/bin/sh
# Build libretro_audio, the pack's pause shortener in C.
#
# Nothing here needs more than a C compiler and libm, which every engine in the
# pack already needs.  OUT names the output directory (default: ./build).
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${OUT:-build}
mkdir -p "$OUT"
$CC -O2 -fPIC -shared -std=gnu11 -Wall -Wextra -o "$OUT/libretro_audio.so" \
    retro_audio.c -lm
