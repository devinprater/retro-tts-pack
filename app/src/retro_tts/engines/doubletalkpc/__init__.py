from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from ..audio import trim_leading_audio


def text_to_wav(text: str, rate: int = 50, pitch: int = 50, volume: int = 100) -> bytes:
    executable = os.environ.get("RETRO_TTS_DTALK_CLI") or shutil.which("dtalk_cli")
    rom = os.environ.get("RETRO_TTS_DTALK_ROM")
    if not executable:
        raise RuntimeError("dtalk_cli is unavailable; build upstream/doubletalk-pc/doubletalk")
    if not rom or not Path(rom).is_file():
        raise RuntimeError("set RETRO_TTS_DTALK_ROM to a DoubleTalk PC firmware dump")
    card_rate = round(max(0, min(100, rate)) * 9 / 100)
    card_pitch = round(max(0, min(100, pitch)) * 99 / 100)
    card_volume = round(max(0, min(100, volume)) * 9 / 100)
    controls = f"\x010O\x01{card_rate}S\x01{card_pitch}P\x01{card_volume}V\x0114B"
    clean = "".join(character if 0x20 <= ord(character) <= 0x7E else " " for character in text)
    with tempfile.TemporaryDirectory(prefix="retro-dtalk-") as temporary:
        output = Path(temporary) / "speech.wav"
        result = subprocess.run(
            [executable, rom, "say16", controls + clean, str(output), "3800"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode or not output.is_file():
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(f"DoubleTalk PC synthesis failed: {detail}")
        return trim_leading_audio(output.read_bytes())


__all__ = ["text_to_wav"]
