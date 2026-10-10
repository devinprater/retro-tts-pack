# openbst, the BeSTspeech engine

`src/` and `include/` are [openbst](https://github.com/Mudb0y/openbst), MIT, a
from-scratch portable-C reimplementation of the Berkeley Speech Technologies
formant synthesizer that was sold as BeSTspeech and shipped in Humanware's
Keynote GOLD. Its tables are checked in under `src/data/`, so the engine builds
and speaks with nothing but a C compiler: no DLL, no voice file, no emulator,
and nothing to import at install time.

`build.sh` builds `bst_cli`, this pack's front end, from those sources.

## What the front end does besides speak

Two things it does are worth knowing about, because both were once done in
Python and both showed up as the wait before a word is heard.

The language builds come out about 12 dB below the classic one. The pack makes
that up, and it used to do so a sample at a time in Python, which measured 84
to 95% of a language voice's render time -- 41.7 ms of a 44.1 ms utterance. The
`--gain-db` option does it in C instead, rounding to nearest even and clipping,
which is what Python's `round()` did on the same double; the samples are
unchanged. A four-sentence utterance went from 44.1 ms to 2.4 ms of Python-side
work.

It also renders once rather than twice. `bst_length` runs the whole engine to
count the samples and `bst_say` then runs it again to produce them, so the wait
before any sound was paid twice. `bst_say` reports how much it made, so a buffer
sized from the text is enough, and only a text that somehow overruns it asks the
engine for the exact size and says itself again. A paragraph went from 18.4 ms to
9.3 ms.

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
