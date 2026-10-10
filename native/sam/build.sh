#!/bin/sh
# Build sam_say (Microsoft Sam, Mike and Mary) from the vendored sources.
#
# This is the upstream Makefile's sam_say target, spelled out so the pack can
# build it anywhere a C compiler exists.  Voice data (Sam.spd and the two .LXA
# files) is read at run time and is not part of this tree.
#
# OUT names the build directory (default: ./build).
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${OUT:-build}
mkdir -p "$OUT"
$CC -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter \
    src/sam.c src/sam_lex.c src/sam_morph.c src/sam_pos.c src/sam_norm.c \
    src/sam_front.c src/sam4fx.c src/sam_say.c \
    -lm -o "$OUT/sam_say"
