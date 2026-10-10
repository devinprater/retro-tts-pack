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

## The 2006 builds stop at the first comma

Worth knowing before offering them to a screen reader. A 2006 language build
says only as far as the first comma in the text it is given, and everything
after that comma is dropped. On a fixed line of twelve words the audio that
comes out tracks the comma's position: 14% of the line when the comma follows
the first word, 94% when it follows the eleventh. A paragraph that the classic
build speaks in 19.4 s comes back from `2006ENG` in 2.6 s.

This is the engine's own behaviour, not this front end's. `bst_cli` and upstream
`bstspeak` return identical lengths, and `make selftest` passes 4906 of 4906
goldens -- sample counts and hashes taken from the original Windows binaries --
including the thirteen that contain a comma. The `b32_tts.dll` the pack used
before openbst did the same.

The classic 1995 build is not affected, which is why it is the default voice.
`docs/ROADMAP.md` records the workaround, if the pack ever wants one.

## One change to upstream

`expose-classic-settings.patch` adds two settings to `bst_set`, `exc` and
`unvoiced`. The engine's own text escapes, `~e` and `~u`, set them, but the five
settings openbst's API carries -- `pitch`, `top`, `level`, `voice` and `rate` --
do not, and the pack's voice presets need them to be reproducible without
writing an escape into the text.

The change is additive and nothing upstream reads the new names. The patched
files are `src/speak.c` and `include/bst.h`; reapply the patch after re-syncing
from upstream.
