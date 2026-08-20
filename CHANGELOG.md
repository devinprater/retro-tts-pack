# Changelog

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
