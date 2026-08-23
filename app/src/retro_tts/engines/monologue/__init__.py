from __future__ import annotations

import io
import os
import threading
import wave
from collections.abc import Callable
from pathlib import Path

from . import core

_engines: dict[str, core.Engine] = {}
_engine_lock = threading.Lock()
_synthesis_lock = threading.Lock()

_VOICE_NAMES = {
    display.lower(): voice_id
    for voice_id, (_dll, display, _rate, _bits) in core.VOICES.items()
}


def _voice_id(voice: str | None) -> str:
    """Accept both internal IDs and the names published by the NVDA addon."""
    requested = (voice or core.DEFAULT_VOICE).strip().lower()
    if requested in core.VOICES:
        return requested
    return _VOICE_NAMES.get(requested, core.DEFAULT_VOICE)


def _get_engine(voice_id: str) -> core.Engine:
    with _engine_lock:
        engine = _engines.get(voice_id)
        if engine is None:
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
            engine = core.Engine(bin_dir)
            engine.set_voice(voice_id)
            _engines[voice_id] = engine
        return engine


def text_to_wav(
    text: str,
    rate: int = 5,
    pitch: int = 5,
    volume: int = 9,
    voice: str = core.DEFAULT_VOICE,
    *,
    cancelled: Callable[[], bool] | None = None,
) -> bytes:
    pcm = bytearray()
    voice_id = _voice_id(voice)
    with _synthesis_lock:
        # The two tables use different native sample formats. Switching their
        # control blocks rapidly in one emulated Windows process can corrupt
        # its waveOut state, so each table owns a persistent emulator.
        engine = _get_engine(voice_id)
        engine.configure(volume=volume, pitch=pitch, rate=rate)
        engine.speak(text, should_cancel=cancelled, on_block=pcm.extend)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(core.CHANNELS)
        wav.setsampwidth(core.BITS // 8)
        wav.setframerate(core.OUT_RATE)
        wav.writeframes(pcm)
    return output.getvalue()


__all__ = ["text_to_wav"]
