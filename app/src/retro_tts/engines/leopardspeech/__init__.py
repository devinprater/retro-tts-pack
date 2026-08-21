from __future__ import annotations

import atexit
import os
import re
import select
import signal
import struct
import subprocess
import threading
import time
import wave
from collections.abc import Callable
from io import BytesIO
from pathlib import Path


REQ_MAGIC = 0x54475233  # TGR3
REQ_MAGIC_STREAM = 0x54475234  # TGR4
RSP_MAGIC = 0x54475253  # TGRS
READY_MAGIC = 0x59445254  # TRDY
SAMPLE_RATE = 22050
VOLUME_CLEAN = 90
VOLUME_MAX_VOLM = 2.0
VOLUME_NORM_CEILING = 1.80
VOLUME_NORM = {
    "Agnes": 1.00, "Albert": 1.70, "Alex": 1.80, "BadNews": 1.80,
    "Bahh": 1.70, "Bells": 1.70, "Boing": 1.70, "Bruce": 1.00,
    "Bubbles": 1.70, "Cellos": 1.70, "Deranged": 1.70, "Fred": 1.80,
    "GoodNews": 1.80, "Hysterical": 1.70, "Junior": 1.80, "Kathy": 1.73,
    "Organ": 1.70, "Princess": 1.70, "Ralph": 1.70, "Trinoids": 1.70,
    "Vicki": 1.20, "Victoria": 1.00, "Whisper": 1.80, "Zarvox": 1.70,
}
_COLON = re.compile(r"\bcolon\b", re.IGNORECASE)

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


def _binary_kind(path: Path) -> str:
    with path.open("rb") as executable:
        magic = executable.read(4)
    if magic == b"\x7fELF":
        return "elf"
    if magic[:2] == b"MZ":
        return "pe"
    raise RuntimeError(f"{path} is neither a Linux ELF nor a Windows PE host")


def _runner(package_root: Path) -> tuple[Path, str]:
    backend = os.environ.get("RETRO_TTS_LEOPARD_BACKEND", "auto").lower()
    if backend not in {"auto", "native", "wine"}:
        raise RuntimeError("RETRO_TTS_LEOPARD_BACKEND must be auto, native, or wine")
    override = os.environ.get("RETRO_TTS_LEOPARD_HOST")
    native = package_root / "native" / "leopardspeech" / "leopard_host"
    windows = package_root / "native" / "leopardspeech" / "leopard_host.exe"
    if override:
        runner = _file("RETRO_TTS_LEOPARD_HOST", Path(override), "a Leopard host")
    elif backend == "wine":
        runner = _file("RETRO_TTS_LEOPARD_HOST", windows, "leopard_host.exe")
    elif native.is_file():
        runner = native.resolve()
    elif backend == "native":
        raise RuntimeError(f"native Leopard host not found at {native}")
    else:
        runner = _file("RETRO_TTS_LEOPARD_HOST", windows, "leopard_host.exe")
    kind = _binary_kind(runner)
    if backend == "native" and kind != "elf":
        raise RuntimeError("native backend requires a Linux ELF host")
    if backend == "wine" and kind != "pe":
        raise RuntimeError("wine backend requires a Windows PE host")
    wine = (os.environ.get("RETRO_TTS_WINE", "wine")
            if kind == "pe" and os.name != "nt" else "")
    return runner, wine


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
    runner, wine = _runner(package_root)
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
    else:
        env["TIGER_READY_HANDSHAKE"] = "1"
    stderr = None if env.get("TIGER_HOST_VERBOSE") else subprocess.DEVNULL
    _host = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=stderr, bufsize=0, env=env,
    )
    _host_key = key
    if not wine:
        ready = struct.unpack("<I", _read_exact(_host, 4, None))[0]
        if ready != READY_MAGIC:
            _stop_host()
            raise RuntimeError("native Leopard host failed its ready handshake")
    return _host


def preload() -> None:
    """Load a native host and its Mach-O images before the first utterance."""
    with _lock:
        _get_host()


def _read_exact(
    process: subprocess.Popen[bytes], length: int,
    cancelled: Callable[[], bool] | None,
) -> bytes:
    assert process.stdout is not None
    result = bytearray()
    # MacinTalk normally produces the first response in well under a second.
    # Bound a wedged request so rapid screen-reader interruptions cannot mute
    # the synthesizer indefinitely.  Keep this configurable for unusually
    # slow 32-bit hosts.
    timeout = float(os.environ.get("RETRO_TTS_LEOPARD_TIMEOUT", "8"))
    deadline = time.monotonic() + timeout
    while len(result) < length:
        # A native host can be reaped reliably, so abandon it when Orca moves
        # on instead of letting an interrupted utterance block every later
        # request.  Retain the drain-and-reuse behavior for Wine, where killing
        # the Unix launcher can leave the actual Windows process orphaned.
        if cancelled is not None and cancelled() and _host_key and not _host_key[4]:
            raise InterruptedError("Leopard speech request was cancelled")
        if time.monotonic() >= deadline:
            raise TimeoutError("Leopard speech host response timed out")
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


def _fix_stress(text: str) -> str:
    """Apply LeopardSpeech's measured correction outside embedded commands."""
    def replace(match: re.Match[str]) -> str:
        word = match.group(0)
        fixed = "colen"
        if word.isupper():
            return fixed.upper()
        return fixed.capitalize() if word[:1].isupper() else fixed

    parts = re.split(r"(\[\[.*?\]\])", text, flags=re.DOTALL)
    return "".join(part if part.startswith("[[") else _COLON.sub(replace, part)
                   for part in parts)


def _prepare_text(text: str, volume: int, voice: str) -> str:
    level = max(0, min(100, volume))
    normalization = min(VOLUME_NORM_CEILING, VOLUME_NORM.get(voice, 1.0))
    volm = min(VOLUME_MAX_VOLM, normalization * level / VOLUME_CLEAN)
    return f"[[volm {volm:.3f}]]" + _fix_stress(text.strip())


def _pcm_to_wav(pcm: bytes) -> bytes:
    output = BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm)
    return output.getvalue()


def _request(
    text: str, rate: int, pitch: int, volume: int, voice_name: str, magic: int,
) -> bytes:
    voice = voice_name.encode("utf-8")
    payload = _mac_roman(_prepare_text(text, volume, voice_name))
    wpm = 80 + round(max(0, min(100, rate)) * 3.2)
    pitch_offset = round((max(0, min(100, pitch)) - 50) * 2.4)
    return struct.pack(
        "<IiiIII", magic, wpm, pitch_offset, 0, len(voice), len(payload)
    ) + voice + payload


def stream_pcm(
    text: str, rate: int, pitch: int, send_audio: Callable[[bytes], bool], *,
    volume: int = VOLUME_CLEAN, voice: str | None = None,
    cancelled: Callable[[], bool] | None = None,
) -> None:
    """Stream native-host PCM as it is synthesized.

    Linux uses SIGUSR1 as the non-blocking equivalent of the Windows cancel
    event.  We still drain the stream terminator so the persistent protocol
    remains synchronized for the next Orca utterance.
    """
    voice_name = voice or os.environ.get("RETRO_TTS_LEOPARD_VOICE", "Alex")
    request = _request(text, rate, pitch, volume, voice_name, REQ_MAGIC_STREAM)
    with _lock:
        process = _get_host()
        assert process.stdin is not None
        try:
            process.stdin.write(request)
            process.stdin.flush()
            magic, status = struct.unpack("<Ii", _read_exact(process, 8, None))
            if magic != RSP_MAGIC:
                raise RuntimeError("Leopard speech host returned a bad response")
            if status:
                raise RuntimeError(f"Leopard speech engine returned OSErr {status}")
            interrupted = False
            while True:
                if not interrupted and cancelled is not None and cancelled():
                    interrupted = True
                    if _host_key and not _host_key[4]:
                        process.send_signal(signal.SIGUSR1)
                frames = struct.unpack("<I", _read_exact(process, 4, None))[0]
                if not frames:
                    break
                pcm = _read_exact(process, frames * 2, None)
                if not interrupted and not send_audio(pcm):
                    interrupted = True
                    if _host_key and not _host_key[4]:
                        process.send_signal(signal.SIGUSR1)
            if _host_key and not _host_key[4]:
                # Leopard's Linux AudioConverter path is currently reliable
                # for one Alex utterance only. Retire it after every streamed
                # response and preload a clean process before releasing the
                # lock. This also makes cancellation recovery deterministic.
                _stop_host()
                _get_host()
        except (BrokenPipeError, OSError):
            _stop_host()
            raise RuntimeError("Leopard speech host failed") from None


def text_to_wav(
    text: str, rate: int = 50, pitch: int = 50, volume: int = VOLUME_CLEAN, *,
    voice: str | None = None,
    cancelled: Callable[[], bool] | None = None,
) -> bytes:
    default_voice = "Alex"
    voice_name = voice or os.environ.get("RETRO_TTS_LEOPARD_VOICE", default_voice)
    payload = _mac_roman(text.strip())
    if not payload:
        return _pcm_to_wav(b"")
    request = _request(text, rate, pitch, volume, voice_name, REQ_MAGIC)
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
            f"Leopard voice {voice_name} decoded to silence; check the host's "
            "AAC decoder or use Fred, Bruce, Victoria, or another non-AAC voice"
        )
    return _pcm_to_wav(pcm)


__all__ = ["text_to_wav"]
