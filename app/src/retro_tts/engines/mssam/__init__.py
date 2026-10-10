"""Microsoft Sam, Mike and Mary (SAPI 5) through the vendored sam_say.

The renderer keeps the pack's interface: Speech Dispatcher's rate and pitch are
0..100 with 50 neutral, while sam_say takes a speed multiplier (1.0 is the
engine's own rate), a pitch multiplier, and a plain output gain.  sam_say has
no stdout mode, so this writes a temporary WAV file and reads it back.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


VOICES = {
    "Sam": "Sam",
    "Mike": "Mike",
    "Mary": "Mary",
}
DEFAULT_VOICE = "Sam"


def _percent(value: int) -> int:
    return max(0, min(100, value))


def _resolve_voice(voice: str | None) -> str:
    requested = voice or DEFAULT_VOICE
    for name, identifier in VOICES.items():
        if name.casefold() == requested.casefold():
            return identifier
    return VOICES[DEFAULT_VOICE]


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, volume: int = 90,
    voice: str | None = None,
) -> bytes:
    # rate 0..100 -> --speed 0.5..2.0, pitch 0..100 -> --pitch 0.5..1.5.
    speed = 0.5 + _percent(rate) * 1.5 / 100.0
    pitch_scale = 0.5 + _percent(pitch) * 1.0 / 100.0
    # --gain is a plain multiplier, 1.0 being the engine's own output level.
    gain = _percent(volume) / 100.0
    executable = Path(os.environ.get("RETRO_TTS_MSSAM_CLI", "bin/sam_say"))
    data = Path(os.environ.get("RETRO_TTS_MSSAM_DATA", "assets/mssam"))

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as handle:
        output = handle.name
    try:
        result = subprocess.run(
            [
                str(executable.resolve()), "--data", str(data.resolve()),
                "--voice", _resolve_voice(voice),
                "--speed", f"{speed:.6f}", "--pitch", f"{pitch_scale:.6f}",
                "--gain", f"{gain:.6f}",
                text.encode("utf-8"), output,
            ],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        rendered = Path(output)
        if result.returncode or not rendered.is_file() or rendered.stat().st_size <= 44:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(
                f"Microsoft Sam synthesis failed: {detail or result.returncode}"
            )
        return rendered.read_bytes()
    finally:
        try:
            os.unlink(output)
        except FileNotFoundError:
            pass
