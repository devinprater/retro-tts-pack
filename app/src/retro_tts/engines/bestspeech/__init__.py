from __future__ import annotations

import ctypes
import io
import os
import threading
import wave
from array import array
from pathlib import Path

from ..audio import trim_leading_audio


_SAMPLE_CB = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t, ctypes.c_void_p
)
_library: ctypes.CDLL | None = None
_engine: int | None = None
_engine_path: Path | None = None
_classic_personality: str | None = None
_language_library: ctypes.CDLL | None = None
_language_engine: int | None = None
_language_engine_path: Path | None = None
_lock = threading.Lock()

_LANGUAGES = {
    "classic": ("b32_tts.dll", "cp1252", "classic", 11025),
    "eng": ("dll_eng.dll", "utf-8", "tilde", 11025),
    "dut": ("dll_dut.dll", "utf-8", "tilde", 11025),
    "fre": ("dll_fre.dll", "utf-8", "tilde", 11025),
    "ger": ("dll_ger.dll", "utf-8", "tilde", 11025),
    "gre": ("dll_gre.dll", "utf-8", "none", 11025),
    "heb": ("dll_heb.dll", "utf-8", "tilde", 11025),
    "ita": ("dll_ita.dll", "utf-8", "tilde", 11025),
    "jpn": ("dll_jpn.dll", "utf-8", "none", 11025),
    "pol": ("dll_pol.dll", "utf-8", "none", 11025),
    "por": ("dll_por.dll", "utf-8", "tilde", 11025),
    "rus": ("dll_rus.dll", "utf-8", "tilde", 10800),
    "spa": ("dll_spa.dll", "utf-8", "tilde", 11025),
}
_VOICES = {
    "fred": (1, 3, 0, 0), "sara": (2, 3, -20, 0),
    "hary": (3, 3, 10, 0), "wendy": (2, 1, 50, 0),
    "dexter": (6, 6, 0, -25), "alien": (4, 6, -50, -20),
    "kit": (5, 3, 40, 0), "bruno": (3, 3, 50, 0),
    "ghost": (3, 2, 50, 0), "peeper": (2, 2, 0, 5),
    "dracula": (3, 3, 45, -5), "granny": (4, 3, -60, 0),
    "martha": (6, 4, 100, -5), "tim": (3, 4, -10, 0),
}


def _native_pitch(value: int) -> int:
    """Center the live Speech Dispatcher value on Fred's native 80 Hz."""
    value = max(0, min(100, value))
    if value <= 50:
        return 40 + round(value * 40 / 50)
    return 80 + round((value - 50) * 80 / 50)


def _character_pitch(text: str, pitch: int) -> int:
    """Ensure Orca character echo distinguishes an uppercase letter."""
    if len(text.strip()) == 1 and text.strip().isalpha() and text.strip().isupper():
        return min(100, pitch + 20)
    return pitch


def _get_engine(engine_dll: Path | None = None) -> tuple[ctypes.CDLL, int]:
    global _library, _engine, _engine_path
    if engine_dll is None:
        engine_dll = Path(os.environ.get("RETRO_TTS_BESTSPEECH_DLL", ""))
    if _library is not None and _engine is not None and _engine_path == engine_dll:
        return _library, _engine
    if _library is not None and _engine is not None:
        _library.bst_destroy(_engine)
        _library, _engine = None, None
    library = Path(os.environ.get("RETRO_TTS_BESTSPEECH_SHIM", "build/libbst_shim.so"))
    if not library.is_file():
        raise RuntimeError("set RETRO_TTS_BESTSPEECH_SHIM to libbst_shim.so")
    if not engine_dll.is_file():
        raise RuntimeError(f"BestSpeech language DLL is unavailable: {engine_dll.name}")
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
    _library, _engine, _engine_path = lib, engine, engine_dll
    return lib, engine


def _reset_engine() -> None:
    """Discard an exhausted emulator instance so the next request starts fresh."""
    global _library, _engine, _engine_path
    if _library is not None and _engine is not None:
        _library.bst_destroy(_engine)
    _library = None
    _engine = None
    _engine_path = None


def _boost_v2_pcm(pcm: bytearray) -> None:
    """Match the newer NVDA language profile's +12 dB default gain."""
    samples = array("h")
    samples.frombytes(pcm)
    factor = 10.0 ** (12.0 / 20.0)
    for index, sample in enumerate(samples):
        amplified = round(sample * factor)
        samples[index] = max(-32768, min(32767, amplified))
    pcm[:] = samples.tobytes()


def _get_language_engine(engine_dll: Path) -> tuple[ctypes.CDLL, int]:
    global _language_library, _language_engine, _language_engine_path
    if (
        _language_library is not None
        and _language_engine is not None
        and _language_engine_path == engine_dll
    ):
        return _language_library, _language_engine
    if _language_library is not None and _language_engine is not None:
        _language_library.bstl_destroy(_language_engine)
    shim = Path(
        os.environ.get(
            "RETRO_TTS_BESTSPEECH_LANGUAGE_SHIM", "build/libbst_lang_shim.so"
        )
    )
    if not shim.is_file():
        raise RuntimeError("set RETRO_TTS_BESTSPEECH_LANGUAGE_SHIM to libbst_lang_shim.so")
    if not engine_dll.is_file():
        raise RuntimeError(f"BestSpeech language DLL is unavailable: {engine_dll.name}")
    lib = ctypes.CDLL(str(shim.resolve()))
    lib.bstl_create.argtypes = [ctypes.c_char_p]
    lib.bstl_create.restype = ctypes.c_void_p
    lib.bstl_destroy.argtypes = [ctypes.c_void_p]
    lib.bstl_speak.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint16), _SAMPLE_CB, ctypes.c_void_p
    ]
    lib.bstl_speak.restype = ctypes.c_int
    lib.bstl_sample_rate.argtypes = [ctypes.c_void_p]
    lib.bstl_sample_rate.restype = ctypes.c_int
    engine = lib.bstl_create(os.fsencode(engine_dll))
    if not engine:
        raise RuntimeError(f"BestSpeech {engine_dll.name} initialization failed")
    _language_library, _language_engine, _language_engine_path = lib, engine, engine_dll
    return lib, engine


def _reset_language_engine() -> None:
    global _language_library, _language_engine, _language_engine_path
    if _language_library is not None and _language_engine is not None:
        _language_library.bstl_destroy(_language_engine)
    _language_library = None
    _language_engine = None
    _language_engine_path = None


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, voice: str | None = None,
) -> bytes:
    pcm = bytearray()

    @_SAMPLE_CB
    def receive(samples, count, _context):
        pcm.extend(ctypes.string_at(samples, count * 2))

    # This engine uses an inverted native range: 200 is slowest and -90
    # fastest. The previous -20..20 mapping covered very little of it.
    native_rate = round(200 - max(0, min(100, rate)) * 2.9)
    effective_pitch = _character_pitch(text, pitch)
    requested = (voice or "fred").lower()
    language_names = {
        "english": "eng", "dutch": "dut", "french": "fre", "german": "ger",
        "greek": "gre", "hebrew": "heb", "italian": "ita", "japanese": "jpn",
        "polish": "pol", "portuguese": "por", "russian": "rus", "spanish": "spa",
    }
    if " - " in requested:
        language_name, personality_name = requested.split(" - ", 1)
        requested = f"{language_names.get(language_name, language_name)}:{personality_name}"
    language, separator, personality = requested.partition(":")
    if not separator:
        language, personality = "classic", language
    filename, encoding, command_mode, sample_rate = _LANGUAGES.get(
        language, _LANGUAGES["classic"]
    )
    classic = Path(os.environ.get("RETRO_TTS_BESTSPEECH_DLL", ""))
    engine_dll = classic if language == "classic" else classic.parent / filename
    head, excitation, inflection, unvoiced = _VOICES.get(
        personality, _VOICES["fred"]
    )
    if command_mode == "classic":
        prepared = (
            f"~r{native_rate}]~e{excitation}]~v{head}]"
            f"~f{_native_pitch(effective_pitch)}]~u{unvoiced}]~h{inflection}]"
            f"{text} ~|"
        )
    elif command_mode == "tilde":
        prepared = (
            f"~r{native_rate}]~e{excitation}]~f{_native_pitch(effective_pitch)}]"
            f"~u{unvoiced}]{text} ~|"
        )
    else:
        prepared = text
    global _classic_personality
    with _lock:
        if language == "classic":
            # Extreme personalities such as Alien alter enough engine state
            # that changing directly back to Fred can jump through corrupted
            # emulated state. A fresh emulator on personality changes is cheap
            # and keeps a native fault from killing the persistent renderer.
            if _classic_personality != personality:
                _reset_engine()
                _classic_personality = personality
            payload = prepared.encode(encoding, "replace")
            lib, engine = _get_engine()
            result = lib.bst_speak(engine, payload, receive, None)
        else:
            # The 2006 language DLL family exports Say_TTS(wchar_t *), unlike
            # classic b32_tts.dll's byte-string API. Passing these DLLs through
            # the classic shim happened to work for Latin text but reduced
            # Greek, Hebrew, Japanese, and Russian to silent buffers.
            utf16 = prepared.encode("utf-16-le") + b"\0\0"
            buffer = ctypes.create_string_buffer(utf16, len(utf16))
            text_pointer = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint16))
            lib, engine = _get_language_engine(engine_dll)
            result = lib.bstl_speak(engine, text_pointer, receive, None)
            sample_rate = lib.bstl_sample_rate(engine) or sample_rate
        # The native shim uses a bump allocator for the emulated Windows heap.
        # A screen reader can eventually exhaust it. Recreate the inexpensive
        # emulator instance and retry instead of killing the Speech Dispatcher
        # output module.
        if result == -1:
            pcm.clear()
            if language == "classic":
                _reset_engine()
                lib, engine = _get_engine()
                result = lib.bst_speak(engine, payload, receive, None)
            else:
                _reset_language_engine()
                lib, engine = _get_language_engine(engine_dll)
                result = lib.bstl_speak(engine, text_pointer, receive, None)
        if not result and language != "classic":
            _boost_v2_pcm(pcm)
    if result or not pcm:
        raise RuntimeError(f"BestSpeech synthesis failed ({result})")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, sample_rate, 0, "NONE", ""))
        wav.writeframes(pcm)
    return trim_leading_audio(output.getvalue())


__all__ = ["text_to_wav"]
