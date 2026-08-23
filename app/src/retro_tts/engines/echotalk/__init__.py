from __future__ import annotations

import ctypes
import io
import os
import threading
import wave
from collections.abc import Callable
from pathlib import Path

_lock = threading.Lock()
_current: tuple[str, ctypes.CDLL, int] | None = None


def _paths(voice: str | None):
    root = Path(os.environ.get("RETRO_TTS_ECHOTALK_DATA", Path(__file__).parent))
    requested = (voice or "Textalker 3.1.3").casefold()
    stem = "textalker_v13" if requested in (
        "textalker 1.3", "textalker_v13", "1.3", "v13"
    ) else "textalker"
    return stem, root / f"{stem}.ram.bin", root / f"{stem}.obj.bin"


def _instance(stem: str, loader: Path, obj: Path):
    global _current
    if _current and _current[0] == stem:
        return _current[1], _current[2]
    if _current:
        _current[1].echotalk_destroy(_current[2])
        _current = None
    library = Path(os.environ.get("RETRO_TTS_ECHOTALK_LIB", Path(__file__).with_name("libechotalk.so")))
    lib = ctypes.CDLL(str(library))
    p = ctypes.c_void_p
    lib.echotalk_create.restype = p
    lib.echotalk_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.echotalk_destroy.argtypes = [p]
    for name, kind in (("speed", ctypes.c_double), ("pitch", ctypes.c_int),
                       ("volume", ctypes.c_int), ("sample_rate", ctypes.c_uint)):
        fn = getattr(lib, "echotalk_set_" + name)
        fn.argtypes = [p, kind]
        fn.restype = ctypes.c_int
    lib.echotalk_speak.argtypes = [p, ctypes.c_char_p]
    lib.echotalk_speak.restype = ctypes.c_int
    lib.echotalk_available.argtypes = [p]
    lib.echotalk_available.restype = ctypes.c_size_t
    lib.echotalk_read.argtypes = [p, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t]
    lib.echotalk_read.restype = ctypes.c_size_t
    lib.echotalk_stop.argtypes = [p]
    error = ctypes.create_string_buffer(512)
    handle = lib.echotalk_create(str(loader).encode(), str(obj).encode(), error, len(error))
    if not handle:
        raise RuntimeError(error.value.decode("utf-8", "replace"))
    _current = (stem, lib, handle)
    return lib, handle


def text_to_wav(text: str, rate: int = 50, pitch: int = 50, volume: int = 90,
                voice: str | None = None, *, cancelled: Callable[[], bool] | None = None) -> bytes:
    with _lock:
        stem, loader, obj = _paths(voice)
        lib, handle = _instance(stem, loader, obj)
        lib.echotalk_set_speed(handle, 2.0 ** ((max(0, min(100, rate)) - 50) / 25.0))
        lib.echotalk_set_pitch(handle, round(max(0, min(100, pitch)) * 63 / 100))
        lib.echotalk_set_volume(handle, round(max(0, min(100, volume)) * 15 / 100))
        lib.echotalk_set_sample_rate(handle, 22050)
        if lib.echotalk_speak(handle, text.encode("utf-8")) != 0:
            raise RuntimeError("EchoTalk synthesis failed")
        pcm = bytearray()
        block = (ctypes.c_int16 * 2048)()
        while True:
            if cancelled and cancelled():
                lib.echotalk_stop(handle)
                return b""
            count = lib.echotalk_read(handle, block, len(block))
            if not count:
                break
            pcm.extend(memoryview(block).cast("B")[:count * 2])
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, 22050, 0, "NONE", ""))
        wav.writeframes(pcm)
    return output.getvalue()


__all__ = ["text_to_wav"]
