from __future__ import annotations

import io
import math
import wave
from array import array


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
