# Retro TTS Pack — pending work

Ordered by priority, most valuable first. Each item is a real, currently-open piece
of work; nothing here is speculative except where explicitly marked.

---

## Recently completed

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

## 2. Native TrueVoice engine on ARM64 via OpenTV (#6)

TruVoice 5.10 has been decompiled to portable C as
[OpenTV](https://github.com/RetroBunn/tv-decomp) (MIT, verified byte-for-byte
against the 1997 binary). It builds and speaks natively on aarch64 — measured:
34/34 English + 14/14 Japanese + 37/37 Spanish sources compiled, `libtvtts.so`
linked as ELF 64-bit aarch64, and a probe produced 69,080 samples of which 57,136
were non-silent.

Remaining work is an adapter, not research: the pack's shim exports
`tv_create`/`tv_init`/`tv_select_voice`/`tv_speak`; OpenTV exports
`tvtts_create`/`tvtts_set_voice`/`tvtts_speak_utf8`. The engine module binds via
`ctypes` by name, so only that layer changes.

⛔ **This is TruVoice, not L&H.** OpenTV has 10 voices, English only. It would lose
Carol, British English, the other seven languages, and Michael/Michelle. Present it
as its own engine, never as a replacement for lhtts.

See #6.

---

## 3. Document the install scope (#1)

`sudo ./install.sh` installs into `/root/.local` and is unreachable by the user's
screen reader, because under `sudo` there is no user session. The installer has no
system-wide mode and no `--system` flag; every path derives from `$HOME`. This is a
scope property rather than a bug, but it keeps getting reported, so it needs a
clear note in the README.

---

## 4. Decompile the L&H TTS3000 engine — lowest priority

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

**Until then:** mark `lhtts` and `truevoice` unavailable on aarch64, naming the
missing shim and the architecture. Do not report it as "missing proprietary
assets" — that sends users hunting for DLLs they already have.
