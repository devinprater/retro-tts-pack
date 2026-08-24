from __future__ import annotations

import os
import io
import subprocess
import wave
from pathlib import Path

from ...text import legacy_bytes


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
# TV_ENG32.DLL's own per-voice pitch table at image address 0x100BBE50.
# These values are restored by tts_Reset and are what the original cgrm_spk
# uses when no embedded {{pitch}} command is supplied.
DEFAULT_PITCHES = (85, 50, 125, 73, 129, 89, 117, 203, 208, 152)


def _percent(value: int) -> int:
    return max(0, min(100, value))


def _native_pitch(value: int, voice_id: int) -> int:
    percent = _percent(value)
    default = DEFAULT_PITCHES[voice_id]
    if percent <= 50:
        return 50 + round(percent * (default - 50) / 50)
    return default + round((percent - 50) * (400 - default) / 50)


def _combine(parts: list[bytes]) -> bytes:
    pcm = bytearray()
    params = None
    for part in parts:
        with wave.open(io.BytesIO(part), "rb") as source:
            params = params or source.getparams()
            pcm.extend(source.readframes(source.getnframes()))
    output = io.BytesIO()
    with wave.open(output, "wb") as target:
        target.setparams(params)
        target.writeframes(pcm)
    return output.getvalue()


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
    native_pitch = _native_pitch(pitch, voice_id)
    native_volume = round(_percent(volume) * 16 / 100)
    executable = Path(os.environ.get("RETRO_TTS_TRUEVOICE_CLI", "bin/cgrm_spk"))
    data = Path(os.environ.get("RETRO_TTS_TRUEVOICE_DATA", "assets/truevoice"))

    def synthesize(fragment: str) -> bytes:
        result = subprocess.run(
            [
                str(executable.resolve()), "--data", str(data.resolve()),
                "--filename", "-", "--voice", str(voice_id),
                "--rate", str(native_rate), "--pitch", str(native_pitch),
                "--volume", str(native_volume), legacy_bytes(fragment),
            ],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        if not result.returncode and len(result.stdout) > 44 and result.stdout[:4] == b"RIFF":
            return result.stdout
        # Some Centigram text paths return 0x10000 for a full sentence even
        # though each phrase is valid. Split only failed input, then join its
        # PCM, so arbitrary Orca text cannot poison or mute later requests.
        words = fragment.split()
        if len(words) > 1:
            middle = len(words) // 2
            return _combine([
                synthesize(" ".join(words[:middle])),
                synthesize(" ".join(words[middle:])),
            ])
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise RuntimeError(f"Centigram TruVoice synthesis failed: {detail or result.returncode}")

    return synthesize(text)
