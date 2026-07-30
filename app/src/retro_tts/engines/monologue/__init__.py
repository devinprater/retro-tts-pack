from __future__ import annotations

import io
import os
import threading
import wave
from pathlib import Path

from . import core

_engine: core.Engine | None = None
_engine_lock = threading.Lock()
_synthesis_lock = threading.Lock()


def _get_engine() -> core.Engine:
    global _engine
    with _engine_lock:
        if _engine is None:
            bin_dir = Path(
                os.environ.get(
                    "RETRO_TTS_MONOLOGUE_BIN",
                    str(Path(__file__).with_name("bin")),
                )
            )
            if not bin_dir.is_dir():
                raise RuntimeError(
                    "set RETRO_TTS_MONOLOGUE_BIN to the Monolog engine directory"
                )
            _engine = core.Engine(bin_dir)
        return _engine


def text_to_wav(
    text: str,
    rate: int = 5,
    pitch: int = 5,
    volume: int = 9,
    voice: str = core.DEFAULT_VOICE,
) -> bytes:
    pcm = bytearray()
    with _synthesis_lock:
        engine = _get_engine()
        engine.set_voice(voice)
        engine.configure(volume=volume, pitch=pitch, rate=rate)
        engine.speak(text, on_block=pcm.extend)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(core.CHANNELS)
        wav.setsampwidth(core.BITS // 8)
        wav.setframerate(core.OUT_RATE)
        wav.writeframes(pcm)
    return output.getvalue()


__all__ = ["text_to_wav"]
