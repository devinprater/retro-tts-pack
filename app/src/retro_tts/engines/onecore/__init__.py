"""Microsoft David, Zira and Mark (Windows OneCore) through the vendored
zira_say.

zira_say renders 16 kHz PCM from the original voice files and writes a WAV
file, so this writes a temporary one and reads it back.  Its text is read as
SAPI XML unless --no-xml is given, and screen-reader text is literal, so the
flag is always passed.

zira_say exposes rate and volume only -- it is the plain verification front
end, not the library CLI -- so the pitch setting is accepted here and ignored.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


VOICES = {
    "David": "David",
    "Zira": "Zira",
    "Mark": "Mark",
}
DEFAULT_VOICE = "David"


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
    # zira_say --rate is the SAPI scale: -10..10, with 0 the engine's own rate.
    sapi_rate = round((_percent(rate) - 50) * 10 / 50)
    executable = Path(os.environ.get("RETRO_TTS_ONECORE_CLI", "bin/zira_say"))
    data = Path(os.environ.get("RETRO_TTS_ONECORE_DATA", "assets/onecore"))

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as handle:
        output = handle.name
    try:
        result = subprocess.run(
            [
                str(executable.resolve()), "--voice", _resolve_voice(voice),
                "--dir", str(data.resolve()), "--rate", str(sapi_rate),
                "--volume", str(_percent(volume)), "--no-xml",
                text.encode("utf-8"), output,
            ],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        rendered = Path(output)
        if result.returncode or not rendered.is_file() or rendered.stat().st_size <= 44:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(
                f"Microsoft OneCore synthesis failed: {detail or result.returncode}"
            )
        return rendered.read_bytes()
    finally:
        try:
            os.unlink(output)
        except FileNotFoundError:
            pass
