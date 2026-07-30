from __future__ import annotations

import io
import os
import wave
from pathlib import Path

from . import core


def text_to_wav(text: str, rate: int = 5, pitch: int = 5, volume: int = 9, tone: int = 0) -> bytes:
    image = Path(
        os.environ.get(
            "RETRO_TTS_SMOOTHTALKER_IMAGE",
            str(Path(__file__).with_name("engine.bin")),
        )
    )
    if not image.is_file():
        raise RuntimeError("set RETRO_TTS_SMOOTHTALKER_IMAGE to engine.bin")
    engine = core.Engine(image)
    engine.configure((core.DEFAULT_GENDER, tone, volume, pitch, rate))
    pcm, sample_rate = engine.speak(text)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)
    return output.getvalue()


__all__ = ["text_to_wav"]
