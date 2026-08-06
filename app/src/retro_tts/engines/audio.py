from __future__ import annotations

import io
import math
import wave
from array import array
from collections.abc import Callable


class PCM16PauseShortener:
    """Stream mono PCM while shortening only sustained quiet runs."""

    def __init__(
        self,
        sample_rate: int,
        emit: Callable[[bytes], object],
        *,
        factor: float = 0.30,
        threshold: int = 200,
        window_ms: int = 5,
        minimum_pause_ms: int = 60,
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
        if self.sample_width == 1:
            samples = [(sample - 128) << 8 for sample in block]
        else:
            values = array("h")
            values.frombytes(block)
            samples = values
        if not samples:
            return True
        rms = math.isqrt(sum(sample * sample for sample in samples) // len(samples))
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


def shorten_wav_pauses(wav_data: bytes) -> bytes:
    """Apply the NVDA add-ons' 30% short-pause policy to a mono PCM WAV."""
    source = io.BytesIO(wav_data)
    with wave.open(source, "rb") as wav:
        params = wav.getparams()
        if params.sampwidth not in (1, 2) or params.nchannels != 1:
            return wav_data
        pcm = wav.readframes(params.nframes)
    shortened = bytearray()
    processor = PCM16PauseShortener(
        params.framerate, shortened.extend, sample_width=params.sampwidth
    )
    processor.feed(pcm)
    processor.finish()
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
        rms = math.isqrt(sum(sample * sample for sample in chunk) // len(chunk))
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
