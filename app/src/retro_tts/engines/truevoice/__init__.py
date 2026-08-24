from __future__ import annotations

import ctypes
import os
import platform
import threading
from pathlib import Path


VOICES = {
    "Peter": 0,
    "Sidney": 1,
    "Eager Eddie": 2,
    "Deep Douglas": 3,
    "Biff": 4,
    "Grandpa Amos": 5,
    "Melvin": 6,
    "Alex": 7,
    "Wanda": 8,
    "Julia": 9,
}
DEFAULT_VOICE = "Peter"
_SAMPLE_CB = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t, ctypes.c_void_p
)

_library: ctypes.CDLL | None = None
_engine: int | None = None
_lock = threading.Lock()


def _architecture() -> str:
    return "aarch64" if platform.machine().lower() in {"aarch64", "arm64"} else "x86_64"


def _load() -> tuple[ctypes.CDLL, int]:
    global _library, _engine
    if _library is not None and _engine is not None:
        return _library, _engine
    shim = Path(os.environ.get(
        "RETRO_TTS_TRUEVOICE_SHIM",
        f"lib/libtruevoice_shim.{_architecture()}.so",
    ))
    data = Path(os.environ.get("RETRO_TTS_TRUEVOICE_DATA", "assets/truevoice"))
    library = ctypes.CDLL(str(shim))
    library.tv_create.argtypes = [ctypes.c_char_p]
    library.tv_create.restype = ctypes.c_void_p
    library.tv_destroy.argtypes = [ctypes.c_void_p]
    library.cgrm_init.argtypes = [ctypes.c_void_p]
    library.cgrm_init.restype = ctypes.c_int
    library.cgrm_speak.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p,
        ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        _SAMPLE_CB, ctypes.c_void_p,
    ]
    library.cgrm_speak.restype = ctypes.c_int
    engine = library.tv_create(str(data.resolve()).encode())
    if not engine or library.cgrm_init(engine) != 0:
        if engine:
            library.tv_destroy(engine)
        raise RuntimeError("Centigram TruVoice initialization failed")
    _library, _engine = library, engine
    return library, engine


def _percent(value: int) -> int:
    return max(0, min(100, value))


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, volume: int = 90,
    voice: str | None = None,
) -> bytes:
    requested = voice or DEFAULT_VOICE
    voice_id = next(
        (identifier for name, identifier in VOICES.items()
         if name.casefold() == requested.casefold()),
        VOICES[DEFAULT_VOICE],
    )
    native_rate = 50 + round(_percent(rate) * 2.0)
    native_pitch = 50 + round(_percent(pitch) * 3.5)
    native_volume = round(_percent(volume) * 16 / 100)
    output = bytearray()

    @_SAMPLE_CB
    def receive(samples, count, _context):
        output.extend(ctypes.string_at(samples, count * 2))

    with _lock:
        library, engine = _load()
        rc = library.cgrm_speak(
            engine, text.encode("cp1252", "replace"), voice_id,
            native_rate, native_pitch, native_volume, receive, None,
        )
    if rc or len(output) <= 44 or output[:4] != b"RIFF":
        raise RuntimeError(f"Centigram TruVoice synthesis failed ({rc})")
    return bytes(output)
