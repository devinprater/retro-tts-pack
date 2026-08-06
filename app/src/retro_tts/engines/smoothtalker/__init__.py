from __future__ import annotations

import io
import os
import threading
import wave
from collections.abc import Callable
from pathlib import Path

from . import core


_engine: core.Engine | None = None
_engine_image: Path | None = None
_lock = threading.Lock()


def _get_engine(image: Path) -> core.Engine:
    global _engine, _engine_image
    if _engine is None or _engine_image != image:
        _engine = core.Engine(image)
        _engine_image = image
    return _engine


def text_to_wav(
    text: str, rate: int = 5, pitch: int = 5, volume: int = 9, tone: int = 0,
    *, cancelled: Callable[[], bool] | None = None,
) -> bytes:
    image = Path(
        os.environ.get(
            "RETRO_TTS_SMOOTHTALKER_IMAGE",
            str(Path(__file__).with_name("engine.bin")),
        )
    )
    if not image.is_file():
        raise RuntimeError("set RETRO_TTS_SMOOTHTALKER_IMAGE to engine.bin")
    with _lock:
        engine = _get_engine(image)
        engine.configure((core.DEFAULT_GENDER, tone, volume, pitch, rate))
        pcm, sample_rate = engine.speak(text, should_cancel=cancelled)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)
    return output.getvalue()


__all__ = ["text_to_wav"]
