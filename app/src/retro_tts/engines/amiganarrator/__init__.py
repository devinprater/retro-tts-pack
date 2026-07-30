from __future__ import annotations

import io
import os
import shutil
import subprocess
import struct
import wave
from collections.abc import Callable
from functools import lru_cache
from pathlib import Path

from ..audio import trim_leading_audio


_ARPABET = {
    "AA": "AA", "AE": "AE", "AH": "AH", "AO": "AO", "AW": "AW",
    "AY": "AY", "B": "B", "CH": "CH", "D": "D", "DH": "DH",
    "EH": "EH", "ER": "ER", "EY": "EY", "F": "F", "G": "G",
    "HH": "/H", "IH": "IH", "IY": "IY", "JH": "J", "K": "K",
    "L": "L", "M": "M", "N": "N", "NG": "NX", "OW": "OW",
    "OY": "OY", "P": "P", "R": "R", "S": "S", "SH": "SH",
    "T": "T", "TH": "TH", "UH": "UH", "UW": "UW", "V": "V",
    "W": "W", "Y": "Y", "Z": "Z", "ZH": "ZH",
}
_STRESS = {"0": "", "1": "4", "2": "2"}


@lru_cache(maxsize=2)
def _dictionary(path: str) -> dict[str, tuple[str, ...]]:
    words: dict[str, tuple[str, ...]] = {}
    with open(path, encoding="ascii", errors="replace") as source:
        for line in source:
            if not line.strip() or line.startswith(";;;"):
                continue
            try:
                word, phonemes = line.rstrip().split("\t", 1)
            except ValueError:
                continue
            # CMUdict alternatives are named WORD(2), WORD(3), ...
            words.setdefault(word.split("(", 1)[0].upper(), tuple(phonemes.split()))
    return words


def _narrator_phoneme(token: str) -> str:
    stress = _STRESS.get(token[-1], "") if token[-1:].isdigit() else ""
    base = token[:-1] if token[-1:].isdigit() else token
    return _ARPABET.get(base, "") + stress


def _translate(text: str, dictionary_path: str) -> str:
    import re

    dictionary = _dictionary(dictionary_path)
    result: list[str] = []
    for word in re.findall(r"[A-Za-z]+(?:'[A-Za-z]+)?", text.upper()):
        phonemes = dictionary.get(word)
        if phonemes:
            result.append("".join(_narrator_phoneme(item) for item in phonemes))
    if not result:
        raise RuntimeError("CMUdict could not translate any input words")
    return " ".join(result) + "."


def _translate_with_library(text: str, executable: str, library: str) -> str | None:
    try:
        result = subprocess.run(
            [executable, "-l", library, text],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    phonetic = result.stdout.decode("ascii", "replace").strip()
    return phonetic if result.returncode == 0 and phonetic else None


def _decode_audio(data: bytes) -> bytes:
    """Widen Narrator's S8 output and soften legacy audio-buffer seams."""
    signed = [sample if sample < 128 else sample - 256 for sample in data]
    # narrator.device v25 writes 161-byte legacy IOAudio buffers. The host
    # concatenates them, but their endpoints are not phase-continuous as real
    # Paula playback was. Fade the boundary correction over eight samples to
    # remove the resulting 138 Hz buzz/click without filtering the voice.
    block_size = 161
    fade = 8
    for boundary in range(block_size, len(signed), block_size):
        correction = signed[boundary - 1] - signed[boundary]
        for offset in range(min(fade, len(signed) - boundary)):
            signed[boundary + offset] += round(
                correction * (fade - offset) / fade
            )
    return struct.pack(
        f"<{len(signed)}h",
        *(max(-128, min(127, sample)) << 8 for sample in signed),
    )


def _prepare(text: str, rate: int, pitch: int) -> tuple[str, str, int, int]:
    executable = os.environ.get("RETRO_TTS_AMIGA_NARRATOR") or shutil.which("narrator")
    device = os.environ.get("RETRO_TTS_AMIGA_DEVICE")
    translator = os.environ.get("RETRO_TTS_AMIGA_TRANSLATOR") or shutil.which("translator")
    translator_library = os.environ.get("RETRO_TTS_AMIGA_TRANSLATOR_LIBRARY")
    cmudict = os.environ.get("RETRO_TTS_AMIGA_CMU_DICT")
    translation_mode = os.environ.get("RETRO_TTS_AMIGA_TRANSLATION", "auto").lower()
    if not executable:
        raise RuntimeError("set RETRO_TTS_AMIGA_NARRATOR to the native narrator binary")
    if not device or not Path(device).is_file():
        raise RuntimeError("set RETRO_TTS_AMIGA_DEVICE to narrator.device")

    phonetic = None
    if (
        translation_mode != "cmudict"
        and translator
        and translator_library
        and Path(translator_library).is_file()
    ):
        phonetic = _translate_with_library(text, translator, translator_library)
    if phonetic is None and cmudict and Path(cmudict).is_file():
        phonetic = _translate(text, cmudict)
    if phonetic is None:
        raise RuntimeError(
            "configure RETRO_TTS_AMIGA_TRANSLATOR and "
            "RETRO_TTS_AMIGA_TRANSLATOR_LIBRARY, or set "
            "RETRO_TTS_AMIGA_CMU_DICT as a fallback"
        )

    rate = max(0, min(100, rate))
    # Classic Narrator's rate value is much faster in practice than its
    # nominal WPM label suggests. Keep Orca's midpoint comfortably readable.
    if rate <= 50:
        native_rate = 40 + round(rate * 40 / 50)
    else:
        native_rate = 80 + round((rate - 50) * 170 / 50)
    native_pitch = 65 + round(max(0, min(100, pitch)) * 2.55)
    return executable, phonetic, native_rate, native_pitch


def stream_pcm(
    text: str,
    rate: int,
    pitch: int,
    on_audio: Callable[[bytes], bool],
) -> None:
    executable, phonetic, native_rate, native_pitch = _prepare(text, rate, pitch)
    device = os.environ["RETRO_TTS_AMIGA_DEVICE"]
    process = subprocess.Popen(
        [
            executable, "-d", device, "-r", str(native_rate),
            "-p", str(native_pitch), phonetic,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    assert process.stdout is not None
    previous: int | None = None
    try:
        while True:
            block = process.stdout.read(161)
            if not block:
                break
            signed = [sample if sample < 128 else sample - 256 for sample in block]
            if previous is not None and signed:
                correction = previous - signed[0]
                fade = min(8, len(signed))
                for offset in range(fade):
                    signed[offset] += round(correction * (fade - offset) / fade)
            if signed:
                previous = signed[-1]
            pcm = struct.pack(
                f"<{len(signed)}h",
                *(max(-128, min(127, sample)) << 8 for sample in signed),
            )
            if not on_audio(pcm):
                process.terminate()
                break
        returncode = process.wait(timeout=2)
    except BaseException:
        process.terminate()
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            process.kill()
        raise
    if returncode and returncode != -15:
        raise RuntimeError(f"Amiga Narrator synthesis failed ({returncode})")


def text_to_wav(text: str, rate: int = 50, pitch: int = 50) -> bytes:
    pcm = bytearray()

    def collect(block: bytes) -> bool:
        pcm.extend(block)
        return True

    stream_pcm(text, rate, pitch, collect)

    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        # Preserve the native 22.2 kHz clock and let PipeWire perform its
        # high-quality device-rate conversion instead of interpolating here.
        wav.setparams((1, 2, 22200, 0, "NONE", ""))
        wav.writeframes(pcm)
    return trim_leading_audio(output.getvalue(), threshold=300)


__all__ = ["text_to_wav"]
