# retro_audio, the pause shortener in C

The pack applies the NVDA add-ons' 30% short-pause policy to the output of every
engine it renders. That runs on every utterance, which makes it the one piece of
per-sample work the whole pack shares. In Python it cost 1.6 ms on a line and
10.9 ms on a paragraph; for BeSTspeech that was more than the engine itself cost.

`build.sh` compiles `retro_audio.c` into `libretro_audio.so`, which `install.sh`
copies to `lib/`. `engines/audio.py` loads it through ctypes and uses its Python
version when the library is not there, so a machine with no C compiler still
works and is only slower.

Nothing here is vendored from anywhere. It is this pack's own code, and it needs
only a C compiler and libm, which the engines already need.

## The two entry points

`retro_shorten_pcm(in, in_bytes, out, out_capacity, sample_width, sample_rate,
threshold, window_ms, minimum_pause_ms, factor)` shortens a whole buffer of mono
PCM and returns how many bytes it wrote, or -1 if the arguments are unusable.

`retro_pcm_quiet(block, bytes, sample_width, threshold)` answers whether a single
window counts as quiet, which is what the Python shortener's streaming path asks
it, one window at a time.

## Keeping the two implementations in step

The C mirrors the Python window for window: which windows count as quiet, how a
run of quiet windows is shortened, and the two cases where only the last window
of a run survives, which are before any audio has been emitted and at the end.
The policy itself -- the factor, the threshold, the window length and the minimum
pause -- lives in `engines/audio.py` as `PAUSE_FACTOR`, `PAUSE_THRESHOLD`,
`PAUSE_WINDOW_MS` and `PAUSE_MINIMUM_PAUSE_MS`, and is passed in rather than
compiled in, so the two cannot drift apart by accident.

Any change to either side has to be checked against the other by shortening the
same audio both ways and comparing the bytes, because a difference here is
audible. The check that was run covered nineteen cases: a quiet run shorter than
the minimum, one exactly at it, one longer with a partial final window, leading
quiet, trailing quiet, alternating quiet and loud, values sitting on the
threshold itself, random noise, 8-bit samples, empty input, and audio with no
quiet in it at all; then the same again over the paragraph each of SAM, At
Speech and BeSTspeech produces. No case differed.

The Python fallback is not dead weight. It is what a machine without a compiler
runs, and it is the reference the C is checked against.
