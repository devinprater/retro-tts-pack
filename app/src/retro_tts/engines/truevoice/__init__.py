from __future__ import annotations

import os
import io
import subprocess
import wave
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


def _percent(value: int) -> int:
    return max(0, min(100, value))


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
    # The recovered cgrm_spk default is 150. Keep that at Orca's neutral
    # midpoint, with finer control below it and the remaining range above it.
    pitch_percent = _percent(pitch)
    native_pitch = (
        50 + round(pitch_percent * 2)
        if pitch_percent <= 50
        else 150 + round((pitch_percent - 50) * 5)
    )
    native_volume = round(_percent(volume) * 16 / 100)
    executable = Path(os.environ.get("RETRO_TTS_TRUEVOICE_CLI", "bin/cgrm_spk"))
    data = Path(os.environ.get("RETRO_TTS_TRUEVOICE_DATA", "assets/truevoice"))

    def synthesize(fragment: str) -> bytes:
        result = subprocess.run(
            [
                str(executable.resolve()), "--data", str(data.resolve()),
                "--filename", "-", "--voice", str(voice_id),
                "--rate", str(native_rate), "--pitch", str(native_pitch),
                "--volume", str(native_volume), fragment,
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
