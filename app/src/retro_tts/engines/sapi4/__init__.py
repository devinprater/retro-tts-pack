"""SAPI 4 (msttssyn.dll, 1999) through the vendored sapi4_speak.

The renderer keeps the pack's interface: Speech Dispatcher's rate and pitch are
0..100 with 50 neutral.  sapi4_speak takes words per minute, 30 to 450 with 150
the engine's own, and a pitch of 50 to 200 with 100 the engine's own, so both
are mapped around those neutral points.  It writes its WAV to a path and nothing
to stdout, so this writes a temporary file and reads it back, as the SAPI 5 and
OneCore modules do.  It has no volume control of its own, so volume is accepted
and ignored rather than pretended.

The nineteen modes are the engine's own, as `sapi4_speak -d DIR -l` reports them:
Sam, Mike and Mary with in Hall, in Space, in Stadium and for Telephone variants,
the two whispers, and RoboSoft One through Six.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


VOICES = {
    "Mary": "Mary",
    "Mary (for Telephone)": "Mary (for Telephone)",
    "Mike": "Mike",
    "Mike (for Telephone)": "Mike (for Telephone)",
    "Sam": "Sam",
    "Female Whisper": "Female Whisper",
    "Mary in Space": "Mary in Space",
    "Mary in Hall": "Mary in Hall",
    "Mary in Stadium": "Mary in Stadium",
    "RoboSoft Six": "RoboSoft Six",
    "RoboSoft Five": "RoboSoft Five",
    "RoboSoft Four": "RoboSoft Four",
    "Male Whisper": "Male Whisper",
    "RoboSoft One": "RoboSoft One",
    "RoboSoft Two": "RoboSoft Two",
    "RoboSoft Three": "RoboSoft Three",
    "Mike in Hall": "Mike in Hall",
    "Mike in Stadium": "Mike in Stadium",
    "Mike in Space": "Mike in Space",
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
    # The SAPI 5 and OneCore modules map a modest span around the engine's own
    # rate rather than its whole range, so this does the same: 0..100 becomes
    # 75..300 wpm, with 50 landing on the engine's own 150.
    speed = max(30, min(450, round(150 * (0.5 + _percent(rate) * 1.5 / 100.0))))
    # Pitch the same way: 50 becomes the engine's own 100, the ends are 50 and 150.
    engine_pitch = max(50, min(200, round(100 * (0.5 + _percent(pitch) / 100.0))))
    executable = Path(os.environ.get("RETRO_TTS_SAPI4_CLI", "bin/sapi4_speak"))
    data = Path(os.environ.get("RETRO_TTS_SAPI4_DATA", "assets/sapi4"))

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as handle:
        output = handle.name
    try:
        result = subprocess.run(
            [
                str(executable.resolve()), "-d", str(data.resolve()),
                "-m", _resolve_voice(voice), "-s", str(speed),
                "-p", str(engine_pitch), "-o", output, text,
            ],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        rendered = Path(output)
        if result.returncode or not rendered.is_file() or rendered.stat().st_size <= 44:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(
                f"SAPI 4 synthesis failed: {detail or result.returncode}"
            )
        return rendered.read_bytes()
    finally:
        try:
            os.unlink(output)
        except FileNotFoundError:
            pass
