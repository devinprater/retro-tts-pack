from __future__ import annotations

import ctypes
import io
import os
import threading
import wave
from pathlib import Path

from ..audio import trim_leading_audio


_SAMPLE_CB = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t, ctypes.c_void_p
)
_library: ctypes.CDLL | None = None
_engine: int | None = None
_lock = threading.Lock()


def _get_engine() -> tuple[ctypes.CDLL, int]:
    global _library, _engine
    if _library is not None and _engine is not None:
        return _library, _engine
    library = Path(os.environ.get("RETRO_TTS_BESTSPEECH_SHIM", "build/libbst_shim.so"))
    engine_dll = os.environ.get("RETRO_TTS_BESTSPEECH_DLL")
    if not library.is_file():
        raise RuntimeError("set RETRO_TTS_BESTSPEECH_SHIM to libbst_shim.so")
    if not engine_dll or not Path(engine_dll).is_file():
        raise RuntimeError("set RETRO_TTS_BESTSPEECH_DLL to b32_tts.dll")
    lib = ctypes.CDLL(str(library.resolve()))
    lib.bst_create.argtypes = [ctypes.c_char_p]
    lib.bst_create.restype = ctypes.c_void_p
    lib.bst_destroy.argtypes = [ctypes.c_void_p]
    lib.bst_speak.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, _SAMPLE_CB, ctypes.c_void_p
    ]
    lib.bst_speak.restype = ctypes.c_int
    engine = lib.bst_create(os.fsencode(engine_dll))
    if not engine:
        raise RuntimeError("BestSpeech engine initialization failed")
    _library, _engine = lib, engine
    return lib, engine


def text_to_wav(text: str, rate: int = 50, pitch: int = 50) -> bytes:
    pcm = bytearray()

    @_SAMPLE_CB
    def receive(samples, count, _context):
        pcm.extend(ctypes.string_at(samples, count * 2))

    # This engine uses an inverted native range: 200 is slowest and -90
    # fastest. The previous -20..20 mapping covered very little of it.
    native_rate = round(200 - max(0, min(100, rate)) * 2.9)
    payload = f"~r{native_rate}]{text} ~|".encode("cp1252", "replace")
    with _lock:
        lib, engine = _get_engine()
        result = lib.bst_speak(engine, payload, receive, None)
    if result or not pcm:
        raise RuntimeError(f"BestSpeech synthesis failed ({result})")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, 11025, 0, "NONE", ""))
        wav.writeframes(pcm)
    return trim_leading_audio(output.getvalue())


__all__ = ["text_to_wav"]
