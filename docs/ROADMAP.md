# Retro TTS Pack — pending work

Ordered by priority, most valuable first. Each item is a real, currently-open piece
of work; nothing here is speculative except where explicitly marked.

---

## Recently completed

- **The pure-Python engines were the slow half.** A SAM paragraph cost 606 ms of
  synthesis against about 6 ms for the native engines, and ST Speech 127 ms.
  Neither figure is a limit of the engine, only of how the Python was written:
  SAM recomputed a 256-value sine with `math.sin` a million times a paragraph and
  re-read frame data that does not move inside the inner loop, and ST Speech
  looked up fixed values through a table on every tick. Both now do that work
  once. SAM is 2.4x faster and ST Speech 1.15x, with every output byte identical
  to the revision before, checked over 80 utterances.

- **Install scope**, was #1. The README now states plainly that this is a per-user
  install with no system-wide mode, that `sudo ./install.sh` puts everything
  under `/root` where the user's screen reader never looks, and how to recover
  from having run it. The installer was already correct; only the documentation
  was missing, which is why the question kept coming back.
- **Broken upstream links** (was #2). Audited every URL in the docs, config
  and installer with redirect detection. Fixed `tgeczy/tiger-speech` ->
  `tgeczy/panthera-speech` (a rename, so it still returned 200), switched
  the fake6502 link to https (http returns 503), and replaced the dead
  `daiverd/rusty_tts` URL with an attribution line noting the upstream is
  unpublished. Commit `e487958`.

---

## 1. Run x86-only engines on ARM64 through Wine (Hangover / FEX-Emu)

The single highest-value item, because it unlocks **four** engines at once on
aarch64, not one.

`WinTalker`, `Tiger Speech`, `Leopard Speech`, `Lion Speech` and `L&H TTS3000` are
x86_64-only today. Four of those are already **Wine-based on x86_64**, so the x86
code path exists and is known to work. On ARM the only missing piece is an x86
execution layer for Wine: `hangover` (Wine + FEX-Emu) or FEX-Emu directly.

Why this is worth trying before anything else:

* The engines are ordinary x86 PE code with **no JIT tricks**. The reason the L&H
  *shim* could not be emulated on ARM was that the shim is itself a JIT (Unicorn) —
  a JIT inside an emulator is broken by construction. The Windows DLLs it drives
  are plain code and do not have that problem.
* It reuses the architecture the pack already ships for other engines, rather than
  introducing a new one.
* It needs no upstream decompilation or vendor cooperation.

**Status:** not yet tested. Hangover is not in the Debian 13 archive, so it needs a
real machine and probably a third-party repo or a build. Needs a Pi.

---

## 2. Decompile the L&H TTS3000 engine — lowest priority

**Do not start this unless every item above is done and someone actively wants
Carol back on ARM.** It is a multi-month reverse-engineering effort with no
existing starting point, and there is every chance it is never worth doing.

L&H TTS3000 on ARM64 is unavailable, and stays unavailable until someone produces
a decompilation. There is:

* **no SDK** — it was never published. Cisco Community, WinWorld and linux.org.ru
  all reach the same wall: the engine is easy to find, the SDK is not. The direct
  headers (`TTSSDK32.h`, `lhtypes.h`) and the
  `TtsInitialize`/`TtsSpeakText`/`TtsSelectVoice` interface have no distribution.
* **no decompilation** — OpenTV covers TruVoice, a different and earlier engine.
* **no shim source** — `liblhtts_shim.x86_64.so` is a bespoke x86 emulator built on
  Unicorn, shipped as a binary with no source in any branch, commit or repository
  and no license entry. Retargeting it without source is a from-scratch emulator
  project.

If it is ever undertaken, the target is a portable reimplementation equivalent to
the L&H TTS3000 concatenative engine across its nine languages (American English,
British English, French, German, Italian, Spanish, Dutch, Russian, Korean), which
is a much larger surface than TruVoice's ten English voices.

**Until then:** mark `lhtts` unavailable on aarch64, naming the missing shim
and the architecture. Do not report it as "missing proprietary assets" — that
sends users hunting for DLLs they already have.

---

## 3. The 2006 BeSTspeech builds stop at the first comma

Measured, and reproduced from the original binaries, so it is the engine's
behaviour and not the front end's.

A language voice -- `English - fred`, `Spanish - fred` and the other ten -- says
only as far as the first comma in what it is given; everything after that comma
is dropped. On a fixed line of twelve words, the audio that comes out tracks the
comma's position exactly, from 14% of the line with the comma after the first
word to 94% with it after the eleventh. A paragraph the classic build speaks in
19.4 s comes back from `2006ENG` in 2.6 s.

`bst_cli` and upstream `bstspeak` return identical lengths, and openbst's own
`make selftest` passes 4906 of 4906 goldens, which are sample counts and hashes
recorded from the original Windows binaries, including the thirteen that contain
a comma. The DLL the pack used before openbst did the same thing, so this is not
new. The classic 1995 build is unaffected, which is why it is the default voice.

**The fix, if it is wanted:** split the text at commas for the 2006 builds and
say each piece in turn, joining them -- the same phrase machinery `lhtts` and
`truevoice` already use. Each piece would then hold no comma and be said whole.
It changes what those voices say, from part of a sentence to all of it, so it is
a decision rather than a bug fix.

---

## 4. Atari ST Speech sounds different depending on whether numpy is installed

Measured, and left alone because it is a behaviour question rather than a bug
with one right answer.

`engines/stspeech/audio.py` carries two renderers: a vectorised one used when
`numpy` can be imported, and a pure-Python fallback. They do not produce the
same samples, and the vectorised one is only about 1.3x faster, so which of the
two a user hears depends on whether numpy happens to be on their machine — a
package that is not a dependency of this pack, though plenty of distributions
install it for something else.

Three ways out, none obviously right:

* Depend on numpy and use the vectorised path always. Uniform, but adds a
  dependency to a pack that has one, and changes what everyone hears today.
* Never use it: prefer the fallback whatever is installed. Uniform the other
  way, and gives up the 1.3x where it is available.
* Leave it. The two are close enough that the difference has not been reported,
  and both paths are tested.

Worth deciding before the next release, because the answer changes the shipped
sound.
