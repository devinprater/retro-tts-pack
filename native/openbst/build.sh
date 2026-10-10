#!/bin/sh
# Build bst_cli from the vendored openbst sources.
#
# openbst builds with plain make and needs nothing but a C compiler, and its
# tables are checked in under src/data, so the engine is complete: no DLL, no
# voice file, no emulator.  This is the same set of sources
# tools/synth/Makefile links into libbst, plus this pack's own front end.
#
# OUT names the build directory (default: ./build).
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${OUT:-build}
CFLAGS="-O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Iinclude"
mkdir -p "$OUT/obj"

OBJS=""
for src in $(find src -name '*.c' | sort); do
    obj="$OUT/obj/$(echo "$src" | sed 's|^src/||; s|/|_|g; s|\.c$|.o|')"
    $CC $CFLAGS -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done

$CC $CFLAGS -c bst_cli.c -o "$OUT/obj/bst_cli.o"
$CC -o "$OUT/bst_cli" "$OUT/obj/bst_cli.o" $OBJS -lm
