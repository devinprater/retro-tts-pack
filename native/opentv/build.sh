#!/bin/sh
# Build tv_cli from the vendored OpenTV sources.
#
# Upstream builds with mingw (harness/build.sh) because its differential tests
# drive the original DLL; it ships no Linux build.  The engine itself is
# portable C, so this is the same set of sources compiled by the host compiler.
# Everything OpenTV's Python tools generate -- the two struct headers, the
# Spanish rename header and the two table images -- is committed under
# generated/, so this needs nothing but a C compiler: no Python, no DLL, and no
# voice data, which is what lets TruVoice run on aarch64 too.
#
# OUT names the build directory (default: ./build).
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${OUT:-build}
CFLAGS="-std=gnu11 -O2 -fwrapv -fno-strict-aliasing -Isrc -Iinclude -Igenerated \
  -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable"
mkdir -p "$OUT/obj"

OBJS=""

# English: the 1997 engine and the port layer that owns one.  src/port/main.c
# is upstream's own 'tv' front end and defines its own main, so it is skipped;
# tv_cli.c takes its place.
for src in $(find src -name '*.c' | sort); do
    case "$src" in src/port/main.c) continue ;; esac
    obj="$OUT/obj/$(echo "$src" | sed 's|^src/||; s|/|_|g; s|\.c$|.o|')"
    $CC $CFLAGS -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done
$CC $CFLAGS -c generated/tvdata.s -o "$OUT/obj/tvdata.o"
OBJS="$OBJS $OUT/obj/tvdata.o"

# Spanish: a separate decompilation of a separate DLL, with its own struct
# layout, so its whole tree is compiled with every name of its own prefixed
# es_ (generated/es_rename.h) and its data laid out under the same prefix.
for src in $(find es es_port -name '*.c' | sort); do
    obj="$OUT/obj/$(echo "$src" | sed 's|/|_|g; s|\.c$|.o|')"
    $CC $CFLAGS -Ies -include generated/es_rename.h -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done
$CC $CFLAGS -c generated/tvdata_es.s -o "$OUT/obj/tvdata_es.o"
OBJS="$OBJS $OUT/obj/tvdata_es.o"

# Japanese: not a decompilation.  It builds the 22-track frames the 1997
# synthesiser already reads and hands them to the English engine, so it carries
# no data of its own and needs no prefixing.
for src in $(find lang/jpn/port -name '*.c' | sort); do
    obj="$OUT/obj/$(echo "$src" | sed 's|/|_|g; s|\.c$|.o|')"
    $CC $CFLAGS -Ilang/jpn/port -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done

$CC $CFLAGS -c tv_cli.c -o "$OUT/obj/tv_cli.o"

# The engine stores 32-bit addresses inside its tables, so the data is not
# position independent; -no-pie keeps that harmless, and -z noexecstack keeps
# the generated assembly, which has no .note.GNU-stack, from asking for an
# executable stack.
$CC -o "$OUT/tv_cli" "$OUT/obj/tv_cli.o" $OBJS -lm -no-pie -Wl,-z,noexecstack
