from __future__ import annotations

import atexit
import os
import select
import struct
import subprocess
import threading
import wave
from collections.abc import Callable
from io import BytesIO
from pathlib import Path


REQ_MAGIC = 0x54475233  # TGR3
RSP_MAGIC = 0x54475253  # TGRS
SAMPLE_RATE = 22050

_host: subprocess.Popen[bytes] | None = None
_host_key: tuple[str, str, str, str, str] | None = None
_lock = threading.Lock()


def _file(variable: str, default: Path, description: str) -> Path:
    path = Path(os.environ.get(variable, default))
    if not path.is_file():
        raise RuntimeError(f"set {variable} to {description}")
    return path.resolve()


def _tree() -> Path:
    value = os.environ.get("RETRO_TTS_LEOPARD_TREE") or os.environ.get("LEOPARD_TREE")
    if not value:
        raise RuntimeError("set RETRO_TTS_LEOPARD_TREE to the extracted leopardspeech-data directory")
    tree = Path(value).resolve()
    if not (tree / "Speech" / "Voices").is_dir():
        raise RuntimeError(f"{tree} does not contain Speech/Voices")
    return tree


def _wine_path(path: Path, wine: str) -> str:
    """Translate an absolute Unix path without starting an extra Wine process."""
    if os.name == "nt" or not wine:
        return str(path)
    return "Z:" + str(path).replace("/", "\\")


def _stop_host() -> None:
    global _host, _host_key
    process, _host, _host_key = _host, None, None
    if process is not None and process.poll() is None:
        # Closing the protocol pipe lets the real Wine child exit as well as
        # its Unix-side launcher.  Terminating only the launcher can orphan
        # leopard_host.exe and repeated Orca cancellations then accumulate
        # hosts until synthesis fails.
        if process.stdin is not None:
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


atexit.register(_stop_host)


def _get_host() -> subprocess.Popen[bytes]:
    global _host, _host_key
    tree = _tree()
    package_root = Path(__file__).resolve().parents[4]
    runner = _file(
        "RETRO_TTS_LEOPARD_HOST",
        package_root / "native" / "leopardspeech" / "leopard_host.exe",
        "leopard_host.exe from the NVDA add-on",
    )
    engine = _file(
        "RETRO_TTS_LEOPARD_ENGINE",
        tree / "Speech/Synthesizers/MacinTalk.SpeechSynthesizer/Contents/MacOS/MacinTalk",
        "the Leopard MacinTalk engine",
    )
    dictionary = _file(
        "RETRO_TTS_LEOPARD_DICTIONARY",
        tree / "SpeechDictionary.framework/Versions/A/SpeechDictionary",
        "the Leopard SpeechDictionary binary",
    )
    wine = os.environ.get("RETRO_TTS_WINE", "wine" if os.name != "nt" else "")
    key = (str(runner), str(engine), str(dictionary), str(tree), wine)
    if _host is not None and _host.poll() is None and _host_key == key:
        return _host
    _stop_host()
    command = [
        str(runner), "--serve", _wine_path(engine, wine),
        _wine_path(dictionary, wine), _wine_path(tree / "Speech/Voices", wine),
    ]
    if wine:
        command.insert(0, wine)
    env = os.environ.copy()
    if wine:
        # The native Windows AAC MFT wants a bare HEAACWAVEINFO extension.
        # Wine's GStreamer bridge instead needs the AudioSpecificConfig
        # appended to it so raw AAC caps contain codec_data.
        env.setdefault("TIGER_WINE_AAC", "1")
    stderr = None if env.get("TIGER_HOST_VERBOSE") else subprocess.DEVNULL
    _host = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=stderr, bufsize=0, env=env,
    )
    _host_key = key
    return _host


def _read_exact(
    process: subprocess.Popen[bytes], length: int,
    cancelled: Callable[[], bool] | None,
) -> bytes:
    assert process.stdout is not None
    result = bytearray()
    while len(result) < length:
        # The host protocol has no cancellation message.  Drain the current
        # response even when Orca has moved on, then reuse the host for the
        # next word.  Killing Wine here only kills its launcher on some Wine
        # versions and leaves an orphaned Leopard host behind.
        readable, _, _ = select.select([process.stdout], [], [], 0.01)
        if not readable:
            if process.poll() is not None:
                raise RuntimeError("Leopard speech host exited")
            continue
        block = os.read(process.stdout.fileno(), length - len(result))
        if not block:
            raise RuntimeError("Leopard speech host disconnected")
        result.extend(block)
    return bytes(result)


def _mac_roman(text: str) -> bytes:
    folds = str.maketrans({
        "\u00a0": " ", "\u2007": " ", "\u2009": " ", "\u202f": " ",
        "\u2011": "-", "\u2012": "-", "\u2015": "-", "\u2212": "-",
        "\u2032": "'", "\u2033": '"', "\u02bc": "'", "\u2044": "/",
    })
    result = bytearray()
    for character in text.translate(folds):
        try:
            result.extend(character.encode("mac_roman"))
        except UnicodeEncodeError:
            result.extend(b" ")
    return bytes(result)


def _pcm_to_wav(pcm: bytes) -> bytes:
    output = BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm)
    return output.getvalue()


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, *,
    voice: str | None = None,
    cancelled: Callable[[], bool] | None = None,
) -> bytes:
    default_voice = "Alex"
    voice_name = voice or os.environ.get("RETRO_TTS_LEOPARD_VOICE", default_voice)
    voice = voice_name.encode("utf-8")
    payload = _mac_roman(text.strip())
    if not payload:
        return _pcm_to_wav(b"")
    wpm = 80 + round(max(0, min(100, rate)) * 3.2)
    pitch_offset = round((max(0, min(100, pitch)) - 50) * 2.4)
    request = struct.pack(
        "<IiiIII", REQ_MAGIC, wpm, pitch_offset, 0, len(voice), len(payload)
    ) + voice + payload
    with _lock:
        process = _get_host()
        assert process.stdin is not None
        try:
            process.stdin.write(request)
            process.stdin.flush()
            magic, status, frames = struct.unpack(
                "<IiI", _read_exact(process, 12, cancelled)
            )
            if magic != RSP_MAGIC:
                raise RuntimeError("Leopard speech host returned a bad response")
            pcm = _read_exact(process, frames * 2, cancelled)
        except (BrokenPipeError, OSError):
            _stop_host()
            raise RuntimeError("Leopard speech host failed") from None
    if status:
        raise RuntimeError(f"Leopard speech engine returned OSErr {status}")
    if not pcm:
        raise RuntimeError("Leopard speech produced no audio")
    if not any(pcm):
        raise RuntimeError(
            f"Leopard voice {voice_name} decoded to silence; use Fred, Bruce, "
            "Victoria, or another non-AAC voice under Wine"
        )
    return _pcm_to_wav(pcm)


__all__ = ["text_to_wav"]
