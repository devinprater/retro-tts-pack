from __future__ import annotations

import ctypes
import io
import math
import os
import platform
import threading
import wave
from array import array
from pathlib import Path


SAMPLE_RATE = 11_025

# Registry order is part of the engine's public ABI.  The supplied Japanese
# and Brazilian Portuguese engines register, but do not render usable audio.
LANG_VOICES: dict[str, tuple[str | None, ...]] = {
    "American English": (
        "Michael", "Michelle", "Julia", "Wanda", "Alex", "Melvin",
        "Grandpa Amos", "Biff", "Deep Douglas", "Eager Eddie", "Sidney",
        "Peter",
    ),
    "French": ("Pierre", "Veronique"),
    "German": ("Stefan", "Anna"),
    "Italian": ("Stefano", "Barbara"),
    "Spanish": ("Julio", "Carmen"),
    "Dutch": ("Alexander", "Linda"),
    "Russian": ("Boris", "Svetlana"),
    "British English": ("Peter", "Carol"),
    # Slot two is the broken 8 kHz Shin-Ah voice.  Keep the hole so the
    # following registry id remains correct.
    "Korean": ("Jun-Ho", None, "Shin-Ah"),
}

LANG_TAGS = {
    "American English": "en-US",
    "British English": "en-GB",
    "French": "fr-FR",
    "German": "de-DE",
    "Italian": "it-IT",
    "Spanish": "es-ES",
    "Dutch": "nl-NL",
    "Russian": "ru-RU",
    "Korean": "ko-KR",
}

LANG_CODEPAGES = {"Russian": "cp1251", "Korean": "cp949"}

VOICE_CATALOG = {
    f"{name} ({language})": (language, voice_id)
    for language, voices in LANG_VOICES.items()
    for voice_id, name in enumerate(voices, 1)
    if name is not None
}

DEFAULT_VOICE = "Michael (American English)"
_SAMPLE_CB = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t, ctypes.c_void_p
)

_library: ctypes.CDLL | None = None
_engine: int | None = None
_selected: tuple[int, int] | None = None
_engine_ids: dict[str, int] = {}
_lock = threading.Lock()


def _architecture() -> str:
    machine = platform.machine().lower()
    return "aarch64" if machine in {"aarch64", "arm64"} else "x86_64"


def _library_path() -> Path:
    configured = os.environ.get("RETRO_TTS_LHTTS_SHIM")
    if configured:
        return Path(configured)
    return Path(f"lib/liblhtts_shim.{_architecture()}.so")


def _data_path() -> Path:
    return Path(os.environ.get("RETRO_TTS_LHTTS_DATA", "assets/lhtts"))


def _load_library() -> ctypes.CDLL:
    library = ctypes.CDLL(str(_library_path()))
    library.tv_create.argtypes = [ctypes.c_char_p]
    library.tv_create.restype = ctypes.c_void_p
    library.tv_destroy.argtypes = [ctypes.c_void_p]
    library.tv_init.argtypes = [ctypes.c_void_p]
    library.tv_init.restype = ctypes.c_int
    library.tv_select_voice.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    library.tv_select_voice.restype = ctypes.c_int
    library.tv_engine_id_for_language.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    library.tv_engine_id_for_language.restype = ctypes.c_int
    library.tv_sample_rate.argtypes = [ctypes.c_void_p]
    library.tv_sample_rate.restype = ctypes.c_int
    library.tv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, _SAMPLE_CB, ctypes.c_void_p]
    library.tv_speak.restype = ctypes.c_int
    return library


def _ensure_engine() -> tuple[ctypes.CDLL, int]:
    global _library, _engine
    if _engine is not None and _library is not None:
        return _library, _engine
    library = _load_library()
    engine = library.tv_create(str(_data_path().resolve()).encode())
    if not engine:
        raise RuntimeError("L&H TTS could not create its emulated engine")
    if library.tv_init(engine) != 0:
        library.tv_destroy(engine)
        raise RuntimeError("L&H TTS initialization failed; check the extracted DLL set")
    _library, _engine = library, engine
    return library, engine


def _resolve_voice(voice: str | None) -> tuple[str, int]:
    requested = voice or DEFAULT_VOICE
    entry = VOICE_CATALOG.get(requested)
    if entry:
        return entry
    # Direct CLI callers often use the short registry name.  Only accept a
    # unique match; Peter intentionally requires its language suffix.
    matches = [value for label, value in VOICE_CATALOG.items()
               if label.rsplit(" (", 1)[0].casefold() == requested.casefold()]
    return matches[0] if len(matches) == 1 else VOICE_CATALOG[DEFAULT_VOICE]


def _time_scale(pcm: bytes, sample_rate: int, factor: float) -> bytes:
    """Change speaking rate with a compact speech-oriented overlap-add.

    TTS3000's private manager ignores SAPI rate settings on its file-render
    path. Aligning overlapping waveform windows changes duration while keeping
    the voices' characteristic pitch substantially intact.
    """
    if abs(factor - 1.0) < 0.015:
        return pcm
    source = array("h")
    source.frombytes(pcm)
    frame = max(96, sample_rate * 30 // 1000)
    overlap = max(32, sample_rate * 10 // 1000)
    output_hop = frame - overlap
    input_hop = max(1, round(output_hop * factor))
    search = max(8, sample_rate * 4 // 1000)
    if len(source) <= frame:
        return pcm
    output = array("h", source[:frame])
    position = input_hop
    while position + frame < len(source):
        low = max(0, position - search)
        high = min(len(source) - frame, position + search)
        tail = output[-overlap:]
        best = position
        best_score = None
        for candidate in range(low, high + 1, 2):
            score = sum(tail[index] * source[candidate + index]
                        for index in range(overlap))
            if best_score is None or score > best_score:
                best_score, best = score, candidate
        for index in range(overlap):
            mixed = (
                output[-overlap + index] * (overlap - index)
                + source[best + index] * index
            ) // overlap
            output[-overlap + index] = max(-32768, min(32767, mixed))
        output.extend(source[best + overlap:best + frame])
        position = best + input_hop
    return output.tobytes()


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, volume: int = 90,
    voice: str | None = None,
) -> bytes:
    del pitch, volume  # The recovered 6.x driver currently uses native pitch/volume.
    global _selected
    language, voice_id = _resolve_voice(voice)
    payload = text.encode(LANG_CODEPAGES.get(language, "cp1252"), "replace")
    pcm = bytearray()

    @_SAMPLE_CB
    def receive(samples, count, _context):
        pcm.extend(ctypes.string_at(samples, count * 2))

    with _lock:
        library, engine = _ensure_engine()
        engine_id = _engine_ids.get(language)
        if engine_id is None:
            engine_id = library.tv_engine_id_for_language(engine, language.encode("ascii"))
            _engine_ids[language] = engine_id
        if engine_id < 1:
            raise RuntimeError(f"L&H TTS language is not installed: {language}")
        if _selected != (engine_id, voice_id):
            rc = library.tv_select_voice(engine, engine_id, voice_id)
            if rc:
                raise RuntimeError(f"L&H TTS voice selection failed ({rc})")
            _selected = (engine_id, voice_id)
        rc = library.tv_speak(engine, payload, receive, None)
        sample_rate = library.tv_sample_rate(engine) or SAMPLE_RATE
    if rc or not pcm:
        raise RuntimeError(f"L&H TTS synthesis failed ({rc})")

    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, sample_rate, 0, "NONE", ""))
        # Neutral Orca rate is intentionally a little faster than TTS3000's
        # unusually slow factory cadence. The full slider spans 0.70x–2.25x.
        speed = 0.70 * math.pow(2.25 / 0.70, max(0, min(100, rate)) / 100)
        wav.writeframes(_time_scale(bytes(pcm), sample_rate, speed))
    return output.getvalue()
