# retro_audio, the pack's audio helpers in C

Two jobs that were sample-at-a-time Python on the path to every word the pack
speaks. Nothing here is vendored from anywhere; it is this pack's own code, and
it needs only a C compiler and libm, which the engines already need.

`build.sh` compiles `retro_audio.c` into `libretro_audio.so`, which `install.sh`
copies to `lib/`. `engines/audio.py` loads it through ctypes, and every use
falls back to the Python in the pack when the library is not there, so a machine
with no compiler still works and is only slower. `RETRO_TTS_AUDIO_LIB` names it.

## retro_shorten_pcm, the pause shortener

The pack applies the NVDA add-ons' 30% short-pause policy to the output of every
engine it renders, which makes it the one piece of per-sample work the whole pack
shares. In Python it cost 1.42 ms on a line and 11.05 ms on a paragraph; for
BeSTspeech that was larger than the engine itself. Now 0.06 ms and 0.44 ms.

`retro_shorten_pcm(in, in_bytes, out, out_capacity, sample_width, sample_rate,
threshold, window_ms, minimum_pause_ms, factor)` shortens a whole buffer of mono
PCM and returns how many bytes it wrote, or -1 if the arguments are unusable.
`retro_pcm_quiet(block, bytes, sample_width, threshold)` answers whether a single
window counts as quiet, which is what the Python shortener's streaming path asks
it, one window at a time.

## retro_time_scale, L&H's speaking-rate changer

TTS3000's private manager ignores SAPI rate settings on its file-render path, so
the pack changes duration itself with an overlap-add that keeps the voices' pitch
mostly intact. In Python it searched for the best matching window with a
generator inside a double loop, 2.9 million iterations for a paragraph. It runs
at every rate, neutral included, because at neutral it is deliberately speeding
the voice up by a quarter: TTS3000's factory cadence is slow and the pack's
slider spans 0.70x to 2.25x.

`retro_time_scale(in, in_samples, out, out_capacity, sample_rate, factor)` takes
and returns 16-bit mono samples and returns how many samples it wrote, or -1 if
they will not fit. A paragraph at the neutral rate went from 127.7 ms to 0.6 ms.

The whole engine is dominated by the emulated TTS3000 rather than by this, so
L&H end to end only went from 901 ms to 695 ms for a paragraph. That is where the
roadmap's L&H item comes in, not here.

## Keeping the two implementations in step

Each C function mirrors the Python window for window and sample for sample:
which windows count as quiet, how a run of quiet windows is shortened, and the
two cases where only the last window of a run survives, namely before any audio
has been emitted and at the end; and for the scaler, the search order, ties going
to the earliest candidate, Python's floor division on negative sums, the
clamping, and `round()` for the hop, which is round-half-to-even.

The policy values for the shortener live in `engines/audio.py` as `PAUSE_FACTOR`,
`PAUSE_THRESHOLD`, `PAUSE_WINDOW_MS` and `PAUSE_MINIMUM_PAUSE_MS`, and are passed
in rather than compiled in, so the two cannot drift apart by accident.

Any change to either side has to be checked against the other by running the same
audio both ways and comparing the bytes, because a difference here is audible.
The checks that were run:

- The shortener, over nineteen cases: a quiet run shorter than the minimum, one
  exactly at it, one longer with a partial final window, leading quiet, trailing
  quiet, alternating, values on the threshold, noise, 8-bit samples, empty input,
  and audio with no quiet in it; then over the paragraph each of SAM, At Speech
  and BeSTspeech produces. No case differed.
- The scaler, over 72 buffers: real L&H output, silence, noise, alternating
  full-scale, the loudest sample, and inputs shorter than, exactly one, and one
  sample longer than the frame, each at eight factors from 0.70 to 3.00. No case
  differed.

Both checks compare directories of results, so check that the corpus is not empty
before believing a pass: an empty directory compares equal to an empty one.
