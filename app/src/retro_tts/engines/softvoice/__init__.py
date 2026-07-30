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
_lock = threading.Lock()
_library: ctypes.CDLL | None = None
_engine: int | None = None


def _get_engine() -> tuple[ctypes.CDLL, int]:
    global _library, _engine
    if _library is not None and _engine is not None:
        return _library, _engine

    library = Path(os.environ.get("RETRO_TTS_SOFTVOICE_SHIM", "build/libsv_shim.so"))
    base_dll = os.environ.get("RETRO_TTS_SOFTVOICE_BASE_DLL")
    language_dll = os.environ.get("RETRO_TTS_SOFTVOICE_LANGUAGE_DLL")
    if not library.is_file():
        raise RuntimeError("set RETRO_TTS_SOFTVOICE_SHIM to libsv_shim.so")
    if not base_dll or not Path(base_dll).is_file():
        raise RuntimeError("set RETRO_TTS_SOFTVOICE_BASE_DLL to tibase32.dll")
    if not language_dll or not Path(language_dll).is_file():
        raise RuntimeError("set RETRO_TTS_SOFTVOICE_LANGUAGE_DLL to tieng32.dll")
    lib = ctypes.CDLL(str(library.resolve()))
    lib.sv_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.sv_create.restype = ctypes.c_void_p
    lib.sv_destroy.argtypes = [ctypes.c_void_p]
    lib.sv_open.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.sv_open.restype = ctypes.c_int
    lib.sv_set_personality.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.sv_speak.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, _SAMPLE_CB, ctypes.c_void_p
    ]
    lib.sv_speak.restype = ctypes.c_int
    engine = lib.sv_create(
        os.fsencode(base_dll), os.fsencode(language_dll), b"tieng"
    )
    if not engine:
        raise RuntimeError("SoftVoice engine initialization failed")
    if lib.sv_open(engine, 1):
        lib.sv_destroy(engine)
        raise RuntimeError("SoftVoice language initialization failed")
    _library, _engine = lib, engine
    return lib, engine


def text_to_wav(text: str, personality: int = 0) -> bytes:
    pcm = bytearray()

    @_SAMPLE_CB
    def receive(samples, count, _context):
        pcm.extend(ctypes.string_at(samples, count * 2))

    with _lock:
        lib, engine = _get_engine()
        lib.sv_set_personality(engine, max(0, min(19, personality)))
        result = lib.sv_speak(
            engine, text.encode("cp1252", "replace"), receive, None
        )
    if result or not pcm:
        raise RuntimeError(f"SoftVoice synthesis failed ({result})")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, 11025, 0, "NONE", ""))
        wav.writeframes(pcm)
    return trim_leading_audio(output.getvalue(), threshold=300)


__all__ = ["text_to_wav"]
