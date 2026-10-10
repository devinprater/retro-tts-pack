#!/bin/sh
# SAPI 4 (msttssyn.dll, 1999) as portable C, from Quinton Williams's
# sapi4-decomp (MIT). The engine reads its tables from Microsoft's own DLL at
# run time, so none of Microsoft's data is in this tree.
set -eu
cd "$(dirname "$0")"
OUT=${OUT:-build}
CC=${CC:-cc}
# gnu11 rather than c11: strict ISO mode hides setenv, which the CLI's -v calls.
FLAGS="-std=gnu11 -O2 -fno-strict-aliasing -ffp-contract=off -fno-builtin -DDECOMP_HOOK"
mkdir -p "$OUT"
$CC $FLAGS -Iemu -Isrc -Iharness \
    emu/x86.c emu/emu.c emu/crt.c emu/winapi.c emu/ole.c emu/registry.c emu/sapi4_tts.c \
    src/*.c \
    harness/hooks.c harness/hooktab.c harness/adapters.c harness/guest.c harness/win32.c \
    emu/sapi4_speak.c -o "$OUT/sapi4_speak" -lm
