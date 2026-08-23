from __future__ import annotations

import io
import sys
import threading
import wave
from collections.abc import Callable
from pathlib import Path

_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

import engine as macintalk1  # type: ignore  # noqa: E402
import macintalk2  # type: ignore  # noqa: E402
import macintalk3  # type: ignore  # noqa: E402
import macintalkpro  # type: ignore  # noqa: E402
import rom  # type: ignore  # noqa: E402

RATE_MIN, RATE_MAX = 60.0, 900.0
PITCH_SEMITONES = 12
OUT_RATE = 22254
_lock = threading.Lock()
_engine = None
_selection: tuple[str, str] | None = None


def _catalogue():
    result = [
        ("Male (MacinTalk 1)", "mt1", "Male", 110),
        ("Female (MacinTalk 1)", "mt1", "Female", 250),
    ]
    roots = rom.search_roots()
    for module, kind, suffix in (
        (macintalk2, "mt2", "MacinTalk 2"),
        (macintalk3, "mt3", "MacinTalk 3"),
        (macintalkpro, "pro", "MacinTalk Pro"),
    ):
        try:
            _files, voices = module.find(roots)
            result.extend((f"{v.name} ({suffix})", kind, v.name, v) for v in voices)
        except Exception:
            continue
    return result


def voice_names() -> list[str]:
    return [entry[0] for entry in _catalogue()]


def _entry(requested: str | None):
    catalogue = _catalogue()
    key = (requested or "Male (MacinTalk 1)").strip().casefold()
    for entry in catalogue:
        if key in (entry[0].casefold(), entry[2].casefold()):
            return entry
    return catalogue[0]


def _open(entry):
    global _engine, _selection
    _label, kind, name, payload = entry
    selection = (kind, name)
    if _engine is not None and _selection == selection:
        return _engine
    if _engine is not None:
        _engine.close()
    roots = rom.search_roots()
    if kind == "mt1":
        found, _missing = rom.find()
        _engine = macintalk1.Engine(found)
    elif kind == "mt2":
        files, voices = macintalk2.find(roots)
        _engine = macintalk2.Engine(files, voices, payload)
        _engine.select(payload)
    elif kind == "mt3":
        folder, voices = macintalk3.find(roots)
        _engine = macintalk3.Engine(folder, voices, payload)
        _engine.select(payload)
    else:
        folder, voices = macintalkpro.find(roots)
        _engine = macintalkpro.Engine(folder, voices, payload)
    _selection = selection
    return _engine


def _scale_volume(pcm: bytes, volume: int) -> bytes:
    gain = max(0, min(100, volume)) / 100.0
    if gain >= 0.995:
        return pcm
    return bytes(max(0, min(255, 128 + round((sample - 128) * gain))) for sample in pcm)


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, volume: int = 90,
    voice: str | None = None, *, cancelled: Callable[[], bool] | None = None,
) -> bytes:
    with _lock:
        if cancelled and cancelled():
            return b""
        entry = _entry(voice)
        eng = _open(entry)
        engine_rate = round(RATE_MIN * (RATE_MAX / RATE_MIN) ** (max(0, min(100, rate)) / 100.0))
        eng.set_rate(engine_rate)
        pitch_offset = round((max(0, min(100, pitch)) - 50) * PITCH_SEMITONES * 10 / 50.0)
        if entry[1] == "mt1":
            eng.set_voice(entry[3] * 2.0 ** (pitch_offset / 120.0))
            pcm = eng.speak(eng.translate(text))
        else:
            eng.set_pitch(pitch_offset)
            pcm = eng.speak(eng.translate(text))
        if cancelled and cancelled():
            return b""
        pcm = _scale_volume(pcm, volume)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 1, OUT_RATE, 0, "NONE", ""))
        wav.writeframes(pcm)
    return output.getvalue()


__all__ = ["text_to_wav", "voice_names"]
