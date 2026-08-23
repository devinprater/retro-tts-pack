# Changelog

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
