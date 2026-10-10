# Changelog

## Unreleased

- Moved the pause shortener into C. It runs on the output of every engine, so it
  was the one piece of per-sample work the whole pack paid for on every
  utterance, and inside the module it cost 1.6 ms on a line and 10.9 ms on a
  paragraph -- more, for BeSTspeech, than the engine that produced the audio.
  `native/audio` compiles a small shared library at install time, beside the
  engines and needing nothing they do not already need, and `engines/audio.py`
  uses it when it is there and its own Python version when it is not. A line now
  costs 0.06 ms and a paragraph about 0.44 ms; rendering a paragraph through the
  daemon went from 19.6 ms to 6.0 ms and a line from 4.0 ms to 2.3 ms. The audio
  is unchanged: the C mirrors the Python window for window, checked over nineteen
  cases that include a quiet run just under the shortening threshold, one exactly
  at it, a partial final window, 8-bit samples and empty input, and then again
  over real output from SAM, At Speech and BeSTspeech.

- Fixed the asset downloader, which could not finish. Its SoftVoice entry named
  a file the mirror has since renamed, and one failing entry stopped every entry
  behind it, so an install with `--download-assets` gave up at the sixth of
  twenty-three and never reached the four Apple MacinTalk trees or the eleven
  L&H packages -- the data the OutSpoken, Tiger, Leopard and Lion voices need.
  The entry names the file the site serves now, whose two DLLs hash exactly as
  they always did, and a failing entry now costs only its own asset and is
  reported at the end with a failing exit status. All twenty-three sources were
  checked, and all twenty-three answer.

- Made BeSTspeech answer sooner, in the three places where the pack's own code
  was doing by hand what the engine does faster. The +12 dB the language builds
  need was applied a sample at a time in Python, and measured, that loop was 84
  to 95% of a language voice's synthesis: 41.7 ms of a 44.1 ms four-sentence
  utterance. `bst_cli` grew `--gain-db` and scales in C, rounding to nearest
  even and clipping exactly as `round()` did, which brings that utterance down
  to 2.7 ms. Every render also ran the engine twice, once through `bst_length`
  to count the samples and again through `bst_say` to produce them, so the wait
  before any sound was paid twice; one pass into a buffer sized from the text is
  enough now, with the exact path kept for a text that overruns it, and a
  paragraph went from 5.5 ms to 3.5 ms. The pause shortener, which runs on the
  output of every engine, summed its window through a Python generator; it adds
  the same integers in the same order through `sum(map(mul, ...))`, so the total
  is the same number exactly with the loop in C. Nothing any engine says has
  changed: 390 renders through the thirteen builds and three settings came out
  byte for byte identical between the old front end and the new, as did 354 of
  those with the gain set against the Python loop, and every shortened file.

- Made SAM and Atari ST Speech faster without changing one sample either of
  them produces. The SAM renderer worked its sine out with `math.sin` about a
  million times for a paragraph, when the argument is always one byte and so
  has only 256 answers, and re-read its frame tables on each of the five steps
  of every frame when none of them move in between. The ST Speech renderer
  looked the same sound-register values up through the DAC table on every tick,
  and packed every sample with a separate call. Measured here, a SAM paragraph
  went from 606 ms to 251 ms and a mixed 41 line corpus from 4.37 s to 1.87 s;
  ST Speech gained about 15%. Every render was compared byte for byte with the
  previous revision -- 80 utterances across the two engines -- and none
  differed, so this buys Orca the first word sooner and nothing else.
- Centigram TruVoice is native OpenTV now: built from source when `install.sh`
  runs, instead of running `TV_ENG32.DLL` under Unicorn. No DLL, no shim, and
  no x86_64-only binary, so TruVoice is available on aarch64 as well (issue
  #6). Its command line and ten-voice list are unchanged.
- BeSTspeech is native openbst now, built from source when `install.sh` runs,
  instead of running `b32_tts.dll` and the twelve `dll_*.dll` language modules
  under Unicorn. No DLLs, no shims, no emulator, and nothing to import. The
  voice list is unchanged, but the six 1998 builds are deliberately not
  offered: they are known to have issues that no test covers.
- Added Microsoft Sam, Mike and Mary (SAPI 5), built from the vendored
  `ms-sam-mike-mary-decomp` sources when `install.sh` runs.
- Added Microsoft David, Zira and Mark (Windows OneCore), built from the
  vendored `ms-david-zira-decomp` sources when `install.sh` runs. `zira_say`
  exposes no pitch control, so these three ignore the pitch setting.
- Vendored all three engines' C sources under `native/`, with their MIT
  licenses under `licenses/opentv/`, `licenses/mssam/` and `licenses/onecore/`.
- Added a `flake.nix` for NixOS (x86_64-linux): it patches the prebuilt
  binaries for the Nix store and runs the normal installer via
  `nix run github:devinprater/retro-tts-pack#install` (or the
  `retro-tts-pack-install` wrapper). install.sh now honours a
  RETRO_TTS_PYTHON variable (defaulting to `python3`) so the generated CLI
  wrapper can use Nix's python.

## 1.1.3 - 2026-10-08

- Fixed EchoTalk's default pitch: Orca's neutral setting now maps to the
  engine's native default pitch 38 instead of rendering flat (issue #8).
- Fixed BeSTSpeech pitch control being effectively frozen: its module config
  used GenericPitchMultiply 0.5, collapsing Speech Dispatcher's -100..100
  pitch range to a near-constant value. Restored to 50 like every other
  engine, so pitch and inflection follow Orca's slider again.
- Wired up the pack's patched Speech Dispatcher generic module
  (bin/sd_retro_generic.x86_64, built from
  native/speech-dispatcher/generic-real-voice-names.patch): the installer now
  places it where Speech Dispatcher looks before the system module, so Orca
  lists real voice names instead of every voice showing as "Male1" (issue
  #7). The patch source was previously shipped but never installed.

## 1.1.2 - 2026-08-24

- Stream L&H and Centigram TruVoice in phrase chunks so Orca can begin playback
  while the rest of an utterance is still being synthesized. L&H follows the
  text's punctuation to preserve continuous prosody; TrueVoice retains shorter
  safety chunks because its original engine rejects some longer strings.
- Prefer punctuation before TrueVoice's safety limit so short bullets and
  labels are not split at an audible arbitrary word boundary.
- Preserve natural comma, semicolon, and sentence pauses at L&H streaming joins
  instead of collapsing each separately rendered boundary to a few milliseconds.
- Remove modern UI chevrons before legacy codepage conversion so Centigram
  TrueVoice does not pronounce `›` as an "uh"-like replacement character.
- Centralize codepage-safe legacy encoding for TrueVoice and L&H, preventing
  box drawing, emoji, and unsupported Unicode symbols from becoming spoken
  replacement bytes while preserving supported language letters.
- Map Orca's neutral TrueVoice pitch to the original per-voice defaults stored
  in `TV_ENG32.DLL`; Peter now uses the engine's pitch 85 instead of 150.
- Make the recreated `cgrm_spk` preserve the selected voice's original pitch
  and volume when no explicit overrides are given.

## 1.1.1 - 2026-08-24

- Isolated each Centigram TruVoice utterance in the recreated `cgrm_spk`
  process and split only phrases rejected by the original engine, preventing a
  failed sentence from crashing or muting subsequent Orca speech.
- Restored TruVoice's original pitch 150 as the neutral Orca setting instead
  of the noticeably higher pitch 225.
- Added pitch-preserving rate control for L&H TTS3000 and made its neutral
  Orca rate faster than the unusually slow factory cadence.
- Kept L&H callback audio on its original streaming path while retaining the
  seekable file emulation required by Centigram TruVoice.

## 1.1.0 - 2026-08-24

- Added native x86_64 Centigram TruVoice 5.10 with all ten voices, working
  rate, pitch, and volume controls, and a recreated `cgrm_spk` command-line
  host. The original Windows DLL runs inside the existing Unicorn bridge;
  Wine is not required.
- Added native x86_64 L&H TTS3000 6.x with 28 usable voices across American
  and British English, French, German, Italian, Spanish, Dutch, Russian, and
  Korean.
- The installer now discovers the supplied TruVoice ZIP/installer and L&H
  self-extracting CAB files locally. Proprietary DLLs remain excluded from the
  pack and are imported only during installation.
- Added checksum-pinned DECtalk.nu downloads for Centigram TruVoice and all
  eleven L&H TTS3000 language packages, with direct verified CAB extraction.
- Moved the OutSpoken, Tiger, and Leopard downloads to DECtalk.nu's dedicated
  Apple directory and added the newly published Lion package.
- Made the persistent renderer tolerate legacy 8-bit punctuation from
  Speech Dispatcher so malformed input cannot mute EchoTalk or OutSpoken, and
  applied normal pause shortening to the SoftVoice worker path.

## 1.0.0 - 2026-08-23

- Added native x86_64 and ARM64 EchoTalk support for the Echo II's Textalker
  1.3 and 3.1.3 engines.
- Added native x86_64 and ARM64 OutSpoken support with the NVDA add-on's 34
  MacinTalk 1, MacinTalk 2, MacinTalk 3, and MacinTalk Pro voice names.
- Replaced the former Tiger and Leopard download sources with checksum-pinned
  DECtalk.nu packages. Lion and Sequoia remain available through local
  asset/add-on discovery.

## 0.6.0 - 2026-08-23

- Wired Speech Dispatcher rate through SoftVoice's native `SVSetRate`, using
  the current add-on's 20–500 mapping instead of silently ignoring the slider.
- Corrected SoftVoice's Spanish language selector from English (`1`) to
  Spanish (`2`), retained separate English and Spanish emulators during rapid
  changes, and report each DLL's actual sample rate. Responsive language-specific
  worker processes isolate its legacy DLL state from the shared renderer and
  renew the bump-only heap before exhaustion.
- Preserved WinTalker personalities' built-in pitch at Orca's neutral setting,
  applying the pitch slider as an offset instead of flattening Trinoids,
  Bubbles, Pipe Organ, and the other personalities to Fred's absolute pitch.
- Made installer executable updates atomic, avoiding `Text file busy` failures
  when Orca auto-spawns Speech Dispatcher during a reinstall.
- Restart the persistent renderer after installer upgrades so new engine code
  takes effect immediately instead of waiting for the old process to exit.
- Resolved Monolog's published NVDA voice names inside the engine, allowing
  Orca's persistent path to select the 11 kHz ProVoice table correctly, and
  isolated the two sample formats in separate emulators for safe rapid switching.
- Matched the newer BeSTSpeech language DLLs to classic English's output level
  using the add-on's +12 dB language default, and isolated classic personality
  changes so switching from Alien back to Fred cannot crash the renderer.
- Mirrored every usable NVDA voice, personality, variant, and language in the
  corresponding Speech Dispatcher person list. Added real voice selection for
  SAM, Monolog, DoubleTalk PC, BeSTSpeech, SoftVoice, Amiga Narrator, and
  WinTalker instead of advertising aliases that all rendered the default.
- Publish the NVDA add-ons' actual voice names to Orca with an unspecified
  variant, preventing repeated symbolic names such as `MALE1` from collapsing
  or selecting the wrong person in Orca's preferences.
- Added a dedicated UTF-16 v2 shim for BeSTSpeech Greek, Hebrew, Japanese, and
  Russian, alongside English, Dutch, French, German, Italian, Polish,
  Portuguese, and Spanish data discovery, plus SoftVoice Spanish. The installer now reads
  both Unix- and Windows-style member paths from nearby NVDA add-on archives.
- Added checksum-pinned DECtalk.nu sources for SmoothTalker, Monologue,
  DoubleTalk PC, WinTalker, SoftVoice, and Amiga Narrator assets, retaining
  byte-identical datajake mirrors as fallbacks. Older archived BeSTSpeech and SAM add-ons are
  ignored in favor of the newer versions already used by the pack.

## 0.5.0 - 2026-08-22

- Replaced the single Leopard integration with a Panthera-compatible shared
  backend exposing Tiger, Leopard, and Lion as separate Speech Dispatcher
  synthesizers, with generation-specific data, defaults, and volume profiles.
- Added checksum-pinned Tiger, Leopard, Lion, and Sequoia downloads with
  published-site and Internet Archive fallback support, plus discovery of
  nearby NVDA add-ons and their engine assets through `../../`.
- Added Wine-compatible Lion AAC decoding and reduced Lion's fixed completion
  guard from 300 ms to 150 ms without changing synthesized output.
- Added a native low-latency Speech Dispatcher client, removing roughly
  40–50 ms of Python startup from each utterance while retaining the Python
  client as an automatic fallback.
- Discarded stale Panthera requests after rapid cursor movement, prevented
  overlapping speech, and kept bounded WAV playback for reliable Wine
  cancellation.
- Made transient Panthera AAC silence restart and retry the host without
  terminating the Speech Dispatcher module or forcing Orca back to eSpeak.
- Preserved eSpeak-NG, Eloquence, and other installed native modules without
  changing Speech Dispatcher's configured default synthesizer.
- Normalized modern prompt characters such as `›` before MacRoman conversion,
  preventing MacinTalk from announcing them as unrelated accented letters.

## 0.4.1 - 2026-08-21

- Updated Leopard Speech behavior to upstream 0.7.3.
- Corrected Alex's stress when speaking the punctuation name "colon" before
  another word, without altering embedded MacinTalk commands.
- Added upstream per-voice volume normalization and sent volume on every
  utterance, preventing a zero-volume request from silencing later speech.
- Bounded native PipeWire feeding to 80 ms chunks for more responsive
  cancellation.

## 0.4.0 - 2026-08-20

- Added a native 32-bit Linux host for Leopard MacinTalk and Alex, with the
  existing Wine host retained as an automatic fallback.
- Streamed native PCM to PipeWire with low-latency, nonblocking playback and
  prompt cancellation during Orca navigation.
- Isolated Alex utterances in preloaded one-shot hosts to avoid reusing broken
  Leopard AudioConverter state while keeping warm response times low.
- Added a lightweight native Speech Dispatcher client and robust renderer
  startup, cancellation, playback-queue, and socket-retry handling.
- Made Leopard Speech survive sustained Orca use without overlapping, dropping,
  or permanently wedging later utterances.
- Enabled Speech Dispatcher's user socket during installation to prevent a
  competing auto-spawned daemon from leaving Orca attached to stale state.

## 0.3.0 - 2026-08-18

- Added Leopard Speech, bringing Mac OS X 10.5 MacinTalk voices to Speech
  Dispatcher through Wine.
- Added working AAC voice decoding under Wine, including Alex and Vicki.
- Made Alex the default Leopard Speech voice, with rate, pitch, cancellation,
  and persistent-host support.
- Prevented rapid Orca word navigation from orphaning Wine host processes and
  eventually disabling Leopard Speech.

## 0.2.2 - 2026-08-06

- Prevented BeSTSpeech from disappearing during long Orca sessions by
  recreating and retrying its emulator after the emulated heap is exhausted.
- Removed invalid empty `GenericStripPunctChars` directives that Speech
  Dispatcher 0.12.1 rejected while loading the generic modules.

## 0.2.1 - 2026-08-06

- Added an aarch64 Linux release for Raspberry Pi Compute Module 5 and similar
  systems, covering all engines except the x86_64-only WinTalker DLL host.
- Shortened sustained pauses across all engine output for more responsive Orca
  navigation while retaining brief boundaries between words and phrases.
- Enabled WinTalker's native 30 percent pause setting on x86_64.
- Made the installer architecture-aware and prevented an unusable WinTalker
  module from being registered on ARM64.

## 0.2.0 - 2026-08-06

- Added WinTalker, using its 64-bit rendering DLL and MacInTalk voices through
  a persistent Wine host.
- Reduced screen-reader latency by keeping compatible engines warm, streaming
  Amiga Narrator output as it is generated, and using low-buffer playback.
- Made cancellation more responsive and prevented interrupted Amiga,
  SmoothTalker, and Monolog utterances from speaking over later text.
- Corrected Amiga Narrator's legacy audio-buffer decoding, eliminating the
  secondary drone/distortion and stabilizing the native output rate.
- Preserved apostrophes through Speech Dispatcher and normalized curly quotes,
  apostrophes, dashes, ellipses, and nonbreaking spaces for vintage reciters.
- Added working BeSTSpeech pitch control, centered on Fred's native pitch, with
  a capital-letter pitch cue for typed single characters.
- Improved service shutdown so Wine helper processes are reaped reliably.

## 0.1.0 - 2026-07-30

- Initial nine-engine binary pack for Speech Dispatcher and Orca.
