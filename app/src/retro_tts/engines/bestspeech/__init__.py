"""BeSTspeech / Keynote Gold through the vendored openbst engine.

The pack used to run the original b32_tts.dll and the twelve dll_*.dll language
modules under Unicorn, through ctypes and a shim.  openbst is the same engine in
portable C with its tables compiled in, built at install time, so there is no
DLL, no shim and no emulator, and it runs on any CPU the pack supports.

The voice names and the settings they stand for are unchanged.  The six 1998
modules are not offered, because they are known to have issues; see
native/openbst/README.md.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from array import array
from pathlib import Path

from ..audio import trim_leading_audio


# The build each voice speaks through, and the code page its tables are written
# in.  openbst's own tests/words2006.sh is the authority for the code pages: the
# language builds read the page their tables are written in, which is not always
# the Windows one -- Spanish takes MS-DOS Latin-1, Polish MS-DOS Central
# European, Hebrew MS-DOS Hebrew.
_BUILDS = {
    "classic": ("1995", "cp1252"),
    "eng": ("2006ENG", "cp1252"),
    "dut": ("2006DUT", "cp1252"),
    "fre": ("2006FRE", "cp1252"),
    "ger": ("2006GER", "cp1252"),
    "gre": ("2006GRE", "cp1253"),
    "heb": ("2006HEB", "cp862"),
    "ita": ("2006ITA", "cp1252"),
    "jpn": ("2006JPN", "cp932"),
    "pol": ("2006POL", "cp852"),
    "por": ("2006POR", "cp1252"),
    "rus": ("2006RUS", "cp1251"),
    "spa": ("2006SPA", "cp850"),
}
_VOICES = {
    "fred": (1, 3, 0, 0), "sara": (2, 3, -20, 0),
    "hary": (3, 3, 10, 0), "wendy": (2, 1, 50, 0),
    "dexter": (6, 6, 0, -25), "alien": (4, 6, -50, -20),
    "kit": (5, 3, 40, 0), "bruno": (3, 3, 50, 0),
    "ghost": (3, 2, 50, 0), "peeper": (2, 2, 0, 5),
    "dracula": (3, 3, 45, -5), "granny": (4, 3, -60, 0),
    "martha": (6, 4, 100, -5), "tim": (3, 4, -10, 0),
}
_LANGUAGE_NAMES = {
    "english": "eng", "dutch": "dut", "french": "fre", "german": "ger",
    "greek": "gre", "hebrew": "heb", "italian": "ita", "japanese": "jpn",
    "polish": "pol", "portuguese": "por", "russian": "rus", "spanish": "spa",
}


def _native_pitch(value: int) -> int:
    """Center the live Speech Dispatcher value on Fred's native 80 Hz."""
    value = max(0, min(100, value))
    if value <= 50:
        return 40 + round(value * 40 / 50)
    return 80 + round((value - 50) * 80 / 50)


def _character_pitch(text: str, pitch: int) -> int:
    """Ensure Orca character echo distinguishes an uppercase letter."""
    if len(text.strip()) == 1 and text.strip().isalpha() and text.strip().isupper():
        return min(100, pitch + 20)
    return pitch


def _boost_v2_pcm(pcm: bytearray) -> None:
    """Match the newer NVDA language profile's +12 dB default gain.

    The 1995 build already comes out at that level and the 2006 ones do not, so
    only the language voices are lifted.
    """
    samples = array("h")
    samples.frombytes(pcm)
    factor = 10.0 ** (12.0 / 20.0)
    for index, sample in enumerate(samples):
        amplified = round(sample * factor)
        samples[index] = max(-32768, min(32767, amplified))
    pcm[:] = samples.tobytes()


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, voice: str | None = None,
) -> bytes:
    # This engine uses an inverted native range: 200 is slowest and -90
    # fastest. The previous -20..20 mapping covered very little of it.
    native_rate = round(200 - max(0, min(100, rate)) * 2.9)
    effective_pitch = _character_pitch(text, pitch)
    requested = (voice or "fred").lower()
    if " - " in requested:
        language_name, personality_name = requested.split(" - ", 1)
        requested = (
            f"{_LANGUAGE_NAMES.get(language_name, language_name)}:{personality_name}"
        )
    language, separator, personality = requested.partition(":")
    if not separator:
        language, personality = "classic", language
    build, codepage = _BUILDS.get(language, _BUILDS["classic"])
    head, excitation, inflection, unvoiced = _VOICES.get(personality, _VOICES["fred"])
    # The presets are the engine's own escape parameters, which openbst carries
    # as settings: ~e wrote its value times sixteen and ~u its value less
    # eighteen, each falling back to its own default when out of range.
    exc = excitation * 0x10 if excitation >= 1 else 0x30
    gain = unvoiced - 0x12 if -0x47 < unvoiced < 0x15 else -0x12
    executable = Path(os.environ.get("RETRO_TTS_BESTSPEECH_CLI", "bin/bst_cli"))

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as handle:
        output = handle.name
    try:
        result = subprocess.run(
            [
                str(executable.resolve()), "--build", build,
                "--voice", str(head), "--rate", str(native_rate),
                "--pitch", str(_native_pitch(effective_pitch)),
                "--top", str(inflection), "--exc", str(exc),
                "--unvoiced", str(gain), "--filename", output,
                text.encode(codepage, "replace"),
            ],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        rendered = Path(output)
        if result.returncode or not rendered.is_file() or rendered.stat().st_size <= 44:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(
                f"BestSpeech synthesis failed: {detail or result.returncode}"
            )
        data = rendered.read_bytes()
    finally:
        try:
            os.unlink(output)
        except FileNotFoundError:
            pass
    if language != "classic":
        pcm = bytearray(data[44:])
        _boost_v2_pcm(pcm)
        data = data[:44] + bytes(pcm)
    return trim_leading_audio(data)


__all__ = ["text_to_wav"]
