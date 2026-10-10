from __future__ import annotations

import ctypes
import io
import math
import os
import wave
from array import array
from collections.abc import Callable
from operator import mul
from pathlib import Path

# The pause policy, in one place: the C shortener and the Python one below it
# both read these, so the two cannot drift apart.
PAUSE_FACTOR = 0.30
PAUSE_THRESHOLD = 200
PAUSE_WINDOW_MS = 5
PAUSE_MINIMUM_PAUSE_MS = 60


def load_audio_library():
    """The compiled audio helpers, when the install built them.

    libretro_audio.so is compiled at install time from native/audio beside the
    engines, and none of this is a new dependency: it needs a C compiler and
    libm, which the engines already need.  It carries the pause shortener and
    L&H's speaking-rate changer, both of which were sample-at-a-time Python on
    the path to every word.  Each use falls back to the Python here, so a
    machine without a compiler still works, only slower.
    """
    path = Path(os.environ.get("RETRO_TTS_AUDIO_LIB", "lib/libretro_audio.so"))
    try:
        library = ctypes.CDLL(str(path.resolve()))
        library.retro_shorten_pcm.restype = ctypes.c_long
        library.retro_shorten_pcm.argtypes = [
            ctypes.c_char_p, ctypes.c_long, ctypes.c_char_p, ctypes.c_long,
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, ctypes.c_double,
        ]
        library.retro_pcm_quiet.restype = ctypes.c_int
        library.retro_pcm_quiet.argtypes = [
            ctypes.c_char_p, ctypes.c_long, ctypes.c_int, ctypes.c_int,
        ]
        library.retro_time_scale.restype = ctypes.c_long
        library.retro_time_scale.argtypes = [
            ctypes.c_char_p, ctypes.c_long, ctypes.c_char_p, ctypes.c_long,
            ctypes.c_int, ctypes.c_double,
        ]
    except (OSError, AttributeError):
        return None
    return library


_LIBRARY = load_audio_library()


class PCM16PauseShortener:
    """Stream mono PCM while shortening only sustained quiet runs."""

    def __init__(
        self,
        sample_rate: int,
        emit: Callable[[bytes], object],
        *,
        factor: float = PAUSE_FACTOR,
        threshold: int = PAUSE_THRESHOLD,
        window_ms: int = PAUSE_WINDOW_MS,
        minimum_pause_ms: int = PAUSE_MINIMUM_PAUSE_MS,
        sample_width: int = 2,
    ) -> None:
        self.emit = emit
        self.factor = factor
        self.threshold = threshold
        self.window_samples = max(1, sample_rate * window_ms // 1000)
        if sample_width not in (1, 2):
            raise ValueError("pause shortener supports 8-bit or 16-bit PCM")
        self.sample_width = sample_width
        self.window_bytes = self.window_samples * sample_width
        self.minimum_windows = max(1, minimum_pause_ms // window_ms)
        self._input = bytearray()
        self._quiet: list[bytes] = []
        self._started = False

    def _is_quiet(self, block: bytes) -> bool:
        if _LIBRARY is not None:
            answer = _LIBRARY.retro_pcm_quiet(
                block, len(block), self.sample_width, self.threshold
            )
            return bool(answer)
        if self.sample_width == 1:
            samples = [(sample - 128) << 8 for sample in block]
        else:
            values = array("h")
            values.frombytes(block)
            samples = values
        if not samples:
            return True
        # Sum of squares as `sum(map(mul, samples, samples))` rather than a
        # generator: the same integers added in the same order, so the same
        # total exactly, but the loop stays in C.  This runs on every window of
        # every utterance, and it is the largest single cost in a long one.
        total = sum(map(mul, samples, samples))
        rms = math.isqrt(total // len(samples))
        return rms < self.threshold

    def _flush_quiet(self, *, trailing: bool = False) -> bool:
        if not self._quiet:
            return True
        if not self._started or trailing:
            blocks = self._quiet[-1:]
        elif len(self._quiet) >= self.minimum_windows:
            count = max(1, round(len(self._quiet) * self.factor))
            blocks = self._quiet[:count]
        else:
            blocks = self._quiet
        self._quiet = []
        return self.emit(b"".join(blocks)) is not False

    def feed(self, data: bytes) -> bool:
        self._input.extend(data)
        while len(self._input) >= self.window_bytes:
            block = bytes(self._input[: self.window_bytes])
            del self._input[: self.window_bytes]
            if self._is_quiet(block):
                self._quiet.append(block)
                continue
            if not self._flush_quiet():
                return False
            self._started = True
            if self.emit(block) is False:
                return False
        return True

    def finish(self) -> bool:
        if self._input:
            block = bytes(self._input)
            self._input.clear()
            if self._is_quiet(block):
                self._quiet.append(block)
            else:
                if not self._flush_quiet():
                    return False
                self._started = True
                if self.emit(block) is False:
                    return False
        return self._flush_quiet(trailing=True)


def _shorten_pcm(pcm: bytes, sample_width: int, sample_rate: int) -> bytes:
    """Shorten the pauses in a whole buffer of mono PCM."""
    if _LIBRARY is not None:
        room = len(pcm) + 1024
        out = ctypes.create_string_buffer(room)
        got = _LIBRARY.retro_shorten_pcm(
            pcm, len(pcm), out, room, sample_width, sample_rate,
            PAUSE_THRESHOLD, PAUSE_WINDOW_MS, PAUSE_MINIMUM_PAUSE_MS, PAUSE_FACTOR,
        )
        if got >= 0:
            return out.raw[:got]
    sink = bytearray()
    processor = PCM16PauseShortener(
        sample_rate, sink.extend, sample_width=sample_width
    )
    processor.feed(pcm)
    processor.finish()
    return bytes(sink)


def shorten_wav_pauses(wav_data: bytes) -> bytes:
    """Apply the NVDA add-ons' 30% short-pause policy to a mono PCM WAV.

    This runs on the output of every engine in the pack, so it is the one piece
    of per-sample work that everybody pays for.  Measured on a paragraph, it was
    10.9 ms against 4.1 ms for the engine that produced the audio; in C it is
    about 0.3 ms.
    """
    source = io.BytesIO(wav_data)
    with wave.open(source, "rb") as wav:
        params = wav.getparams()
        if params.sampwidth not in (1, 2) or params.nchannels != 1:
            return wav_data
        pcm = wav.readframes(params.nframes)
    shortened = _shorten_pcm(pcm, params.sampwidth, params.framerate)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams(params)
        wav.writeframes(shortened)
    return output.getvalue()


def trim_leading_audio(
    wav_data: bytes,
    *,
    threshold: int = 500,
    block_ms: int = 10,
    sustained_blocks: int = 3,
    preroll_ms: int = 20,
) -> bytes:
    """Remove startup silence/noise while retaining a short natural preroll."""
    source = io.BytesIO(wav_data)
    with wave.open(source, "rb") as wav:
        params = wav.getparams()
        if params.sampwidth != 2 or params.nchannels != 1:
            return wav_data
        pcm = wav.readframes(params.nframes)

    samples = array("h")
    samples.frombytes(pcm)
    block = max(1, params.framerate * block_ms // 1000)
    first_voice = None
    consecutive = 0
    for offset in range(0, len(samples), block):
        chunk = samples[offset : offset + block]
        if not chunk:
            break
        rms = math.isqrt(sum(map(mul, chunk, chunk)) // len(chunk))
        if rms >= threshold:
            consecutive += 1
            if consecutive >= sustained_blocks:
                first_voice = offset - (sustained_blocks - 1) * block
                break
        else:
            consecutive = 0
    if first_voice is None:
        return wav_data

    start = max(0, first_voice - params.framerate * preroll_ms // 1000)
    if start == 0:
        return wav_data
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams(params)
        wav.writeframes(samples[start:].tobytes())
    return output.getvalue()
