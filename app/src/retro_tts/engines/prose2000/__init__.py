from __future__ import annotations

import os
import io
import shutil
import subprocess
import struct
import threading
import unicodedata
import wave
from pathlib import Path


ROM_NAMES = (
    "v3.4.1__2000__2.u22",
    "v3.4.1__2000__3.u45",
    "v3.4.1__2000__0.u21",
    "v3.4.1__2000__1.u44",
    "v3.12__8-9-88__dsp_prog.u29",
    "v3.12__8-9-88__dsp_data.u29",
)
_MAGIC = 0x4B325250
_SPEAK = 1
_READY = 101
_AUDIO = 102
_DONE = 103
_ERROR = 104
_CANCELLED = 105
_host: subprocess.Popen[bytes] | None = None
_generation = 0
_lock = threading.Lock()


def _native_rate(value: int) -> int:
    return 50 + max(0, min(100, value)) * 2


def _native_pitch(value: int) -> int:
    value = max(0, min(100, value))
    if value <= 50:
        return 50 + round(value * 35 / 50)
    return 85 + round((value - 50) * 115 / 50)


def _clean_text(text: str) -> str:
    text = unicodedata.normalize("NFKD", text)
    return text.encode("ascii", "replace").decode("ascii")


def _trim_silence(data: bytes) -> bytes:
    source = io.BytesIO(data)
    with wave.open(source, "rb") as wav:
        params = wav.getparams()
        pcm = wav.readframes(wav.getnframes())
    if params.sampwidth != 2 or params.nchannels != 1:
        return data
    samples = struct.unpack(f"<{len(pcm) // 2}h", pcm)
    window = max(1, params.framerate // 100)
    active: list[bool] = []
    for offset in range(0, len(samples), window):
        block = samples[offset:offset + window]
        rms_squared = sum(sample * sample for sample in block) / max(1, len(block))
        active.append(rms_squared >= 300 * 300)
    first = next(
        (index for index in range(max(0, len(active) - 2)) if all(active[index:index + 3])),
        0,
    )
    last = next(
        (index + 1 for index in range(len(active) - 1, 1, -1)
         if all(active[index - 2:index + 1])),
        len(active),
    )
    padding = 3
    start = max(0, first - padding) * window
    end = min(len(samples), (last + padding) * window)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams(params)
        wav.writeframes(struct.pack(f"<{end - start}h", *samples[start:end]))
    return output.getvalue()


def _read_exact(stream, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        block = stream.read(size - len(result))
        if not block:
            raise RuntimeError("Prose 2000 host disconnected")
        result.extend(block)
    return bytes(result)


def _read_message(process: subprocess.Popen[bytes]) -> tuple[int, int, bytes]:
    assert process.stdout is not None
    magic, kind, generation, size = struct.unpack(
        "<IIII", _read_exact(process.stdout, 16)
    )
    if magic != _MAGIC or size > 1 << 20:
        raise RuntimeError("invalid Prose 2000 host response")
    return kind, generation, _read_exact(process.stdout, size)


def _get_host(rom_dir: str) -> subprocess.Popen[bytes]:
    global _host
    if _host is not None and _host.poll() is None:
        return _host
    configured = os.environ.get("RETRO_TTS_PROSE_HOST")
    cli = os.environ.get("RETRO_TTS_PROSE_CLI")
    executable = configured
    if not executable and cli:
        sibling = Path(cli).with_name("ProseHost")
        executable = str(sibling) if sibling.is_file() else None
    executable = executable or shutil.which("ProseHost")
    if not executable:
        raise RuntimeError("ProseHost is unavailable; build native/prose2000")
    process = subprocess.Popen(
        [executable, rom_dir],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    kind, _generation, payload = _read_message(process)
    if kind != _READY:
        process.kill()
        detail = payload.decode("utf-8", "replace")
        raise RuntimeError(f"Prose 2000 initialization failed: {detail}")
    _host = process
    return process


def text_to_wav(text: str, rate: int = 50, pitch: int = 50, volume: int = 100) -> bytes:
    rom_dir = os.environ.get("RETRO_TTS_PROSE_ROMS")
    if not rom_dir or not all((Path(rom_dir) / name).is_file() for name in ROM_NAMES):
        raise RuntimeError("set RETRO_TTS_PROSE_ROMS to the six-file Prose 2000 ROM directory")
    attenuation = round((100 - max(0, min(100, volume))) * 15 / 100)
    controls = (
        f"\x1b[{_native_rate(rate)}r"
        "\x1b[0V"
        f"\x1b[{_native_pitch(pitch)}p"
        f"\x1b[{attenuation}a"
    )
    payload = (controls + _clean_text(text)).encode("ascii", "replace")
    pcm = bytearray()
    global _generation
    with _lock:
        process = _get_host(rom_dir)
        _generation += 1
        generation = _generation
        assert process.stdin is not None
        process.stdin.write(struct.pack("<IIII", _MAGIC, _SPEAK, generation, len(payload)))
        process.stdin.write(payload)
        process.stdin.flush()
        while True:
            kind, response_generation, data = _read_message(process)
            if response_generation != generation:
                continue
            if kind == _AUDIO:
                pcm.extend(data)
            elif kind == _DONE:
                break
            elif kind in (_ERROR, _CANCELLED):
                detail = data.decode("utf-8", "replace")
                raise RuntimeError(f"Prose 2000 synthesis failed: {detail}")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setparams((1, 2, 10_000, 0, "NONE", ""))
        wav.writeframes(pcm)
    return _trim_silence(output.getvalue())


__all__ = ["text_to_wav"]
