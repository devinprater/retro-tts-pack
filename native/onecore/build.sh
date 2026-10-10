#!/bin/sh
# Build zira_say (Microsoft David, Zira and Mark -- the Windows OneCore
# voices) from the vendored sources.
#
# This is the upstream Makefile's 'voice' target for zira_say, spelled out so
# the pack can build it anywhere a C compiler exists.  Voice data
# (MSTTSLocEnUS.dat and M1033<Voice>.{APM,BEP,INI}) is read at run time and is
# not part of this tree.
#
# The floating point flags are not optional: the engine has to produce the same
# samples as the real one, so no contraction (-ffp-contract=off), no fast-math
# and no finite-math-only.  See the note at the top of src/zb.h.
#
# OUT names the build directory (default: ./build).
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${OUT:-build}
CFLAGS="-std=c99 -O2 -Wall -Wextra -Wno-unused-parameter \
  -ffp-contract=off -fno-fast-math -fno-finite-math-only -Isrc"
mkdir -p "$OUT"

# Front end 1 (text -> phones), front end 2 (phones -> prosody/units), and the
# acoustic model plus vocoder.  Exactly the sets the upstream Makefile names;
# the zf1_t_*.c and zf2_t_*.c files are the upstream unit tests and are not
# part of the voice.
FE1="src/zf1_ana.c src/zf1_dat.c src/zf1_engine.c src/zf1_frag.c src/zf1_fst.c \
     src/zf1_fstpm.c src/zf1_lex.c src/zf1_lts.c src/zf1_modules.c \
     src/zf1_morph.c src/zf1_poly.c src/zf1_pos.c src/zf1_post.c src/zf1_pron.c \
     src/zf1_pron_oov.c src/zf1_ss.c src/zf1_tn.c src/zf1_util.c src/zf1_wb.c"
FE2="src/zf2_feat.c src/zf2_main.c src/zf2_prosody.c src/zf2_tree.c src/zf2_units.c"
ZB="src/zb_apm.c src/zb_dur.c src/zb_mlpg.c src/zb_vocoder.c src/zb_vocoder_int.c \
    src/zb_synth.c src/zb_synth_int.c src/zb_io.c src/zb_sonic.c src/zb_wave.c \
    src/zb_ratechg.c"

$CC $CFLAGS $FE1 $FE2 $ZB src/zira_say.c -lm -o "$OUT/zira_say"
