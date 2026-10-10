# openbst, the BeSTspeech engine

`src/` and `include/` are [openbst](https://github.com/Mudb0y/openbst), MIT, a
from-scratch portable-C reimplementation of the Berkeley Speech Technologies
formant synthesizer that was sold as BeSTspeech and shipped in Humanware's
Keynote GOLD. Its tables are checked in under `src/data/`, so the engine builds
and speaks with nothing but a C compiler: no DLL, no voice file, no emulator,
and nothing to import at install time.

`build.sh` builds `bst_cli`, this pack's front end, from those sources.

## The 1998 builds are not offered

openbst carries twenty builds. This pack exposes the 1995 build and the
thirteen 2006 builds. The six 1998 modules are deliberately left out, because
they are known to have issues: openbst's own `tests/cmdtest.sh` records, for
instance, that 1998 French diverges from the engine on any two-word sentence
and that nothing else covers it.

`bst_cli` refuses a 1998 build by name even if it is asked for one, and
`config/modules/bestspeech-generic.conf` lists no 1998 voice.

## One change to upstream

`expose-classic-settings.patch` adds two settings to `bst_set`, `exc` and
`unvoiced`. The engine's own text escapes, `~e` and `~u`, set them, but the five
settings openbst's API carries -- `pitch`, `top`, `level`, `voice` and `rate` --
do not, and the pack's voice presets need them to be reproducible without
writing an escape into the text.

The change is additive and nothing upstream reads the new names. The patched
files are `src/speak.c` and `include/bst.h`; reapply the patch after re-syncing
from upstream.
