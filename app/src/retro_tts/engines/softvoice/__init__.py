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
_engines: dict[str, tuple[ctypes.CDLL, int]] = {}

_PERSONALITIES = {
    "male": 0, "female": 1, "large_male": 2, "child": 3, "giant_male": 4,
    "mellow_female": 5, "mellow_male": 6, "crisp_male": 7, "the_fly": 8,
    "robotoid": 9, "martian": 10, "colossus": 11, "fast_fred": 12,
    "old_woman": 13, "munchkin": 14, "troll": 15, "nerd": 16,
    "milktoast": 17, "tipsy": 18, "choirboy": 19,
}


def _get_engine(language: str) -> tuple[ctypes.CDLL, int]:
    cached = _engines.get(language)
    if cached is not None:
        return cached

    library = Path(os.environ.get("RETRO_TTS_SOFTVOICE_SHIM", "build/libsv_shim.so"))
    base_dll = os.environ.get("RETRO_TTS_SOFTVOICE_BASE_DLL")
    language_dll = os.environ.get(
        "RETRO_TTS_SOFTVOICE_SPANISH_DLL" if language == "es"
        else "RETRO_TTS_SOFTVOICE_LANGUAGE_DLL"
    )
    if not library.is_file():
        raise RuntimeError("set RETRO_TTS_SOFTVOICE_SHIM to libsv_shim.so")
    if not base_dll or not Path(base_dll).is_file():
        raise RuntimeError("set RETRO_TTS_SOFTVOICE_BASE_DLL to tibase32.dll")
    if not language_dll or not Path(language_dll).is_file():
        raise RuntimeError(f"SoftVoice {language} language DLL is unavailable")
    lib = ctypes.CDLL(str(library.resolve()))
    lib.sv_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.sv_create.restype = ctypes.c_void_p
    lib.sv_destroy.argtypes = [ctypes.c_void_p]
    lib.sv_open.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.sv_open.restype = ctypes.c_int
    lib.sv_set_personality.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.sv_set_rate.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.sv_speak.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, _SAMPLE_CB, ctypes.c_void_p
    ]
    lib.sv_speak.restype = ctypes.c_int
    lib.sv_sample_rate.argtypes = [ctypes.c_void_p]
    lib.sv_sample_rate.restype = ctypes.c_int
    engine = lib.sv_create(
        os.fsencode(base_dll), os.fsencode(language_dll),
        b"tispan" if language == "es" else b"tieng"
    )
    if not engine:
        raise RuntimeError("SoftVoice engine initialization failed")
    # SVOpenSpeech's "voice" argument selects the language module: the NVDA
    # add-on and its 32-bit wrapper use 1 for English and 2 for Spanish.
    if lib.sv_open(engine, 2 if language == "es" else 1):
        lib.sv_destroy(engine)
        raise RuntimeError("SoftVoice language initialization failed")
    _engines[language] = (lib, engine)
    return _engines[language]


def text_to_wav(
    text: str, personality: int = 0, voice: str | None = None, rate: int = 50,
) -> bytes:
    pcm = bytearray()

    @_SAMPLE_CB
    def receive(samples, count, _context):
        pcm.extend(ctypes.string_at(samples, count * 2))

    with _lock:
        requested = (voice or "male").lower().replace(" ", "_")
        language = "es" if requested.startswith("spanish") else "en"
        if requested.startswith("spanish_"):
            requested = requested.removeprefix("spanish_")
        if ":" in requested:
            requested = requested.split(":", 1)[1]
        personality = _PERSONALITIES.get(requested, personality)
        lib, engine = _get_engine(language)
        lib.sv_set_personality(engine, max(0, min(19, personality)))
        native_rate = 20 + round(max(0, min(100, rate)) * 4.8)
        lib.sv_set_rate(engine, native_rate)
        result = lib.sv_speak(
            engine, text.encode("cp1252", "replace"), receive, None
        )
    if result or not pcm:
        raise RuntimeError(f"SoftVoice synthesis failed ({result})")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, lib.sv_sample_rate(engine) or 11025, 0, "NONE", ""))
        wav.writeframes(pcm)
    return trim_leading_audio(output.getvalue(), threshold=300)


__all__ = ["text_to_wav"]
