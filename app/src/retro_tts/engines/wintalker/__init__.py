from __future__ import annotations

import os
import select
import struct
import subprocess
import threading
from collections.abc import Callable
from pathlib import Path


_host: subprocess.Popen[bytes] | None = None
_host_key: tuple[str, str, str, str | None] | None = None
_lock = threading.Lock()


def _path(variable: str, default: str, description: str) -> Path:
    path = Path(os.environ.get(variable, default))
    if not path.is_file():
        raise RuntimeError(f"set {variable} to {description}")
    return path.resolve()


def _native_pitch(pitch: int) -> int:
    value = max(0, min(100, pitch))
    if value <= 50:
        return 40 + round(value * 2.8)
    return 180 + round((value - 50) * 4.4)


def _stop_host() -> None:
    global _host, _host_key
    process, _host, _host_key = _host, None, None
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def _get_host() -> subprocess.Popen[bytes]:
    global _host, _host_key
    runner = _path(
        "RETRO_TTS_WINTALKER_CLI",
        "native/wintalker/wintalker_cli.exe",
        "wintalker_cli.exe",
    )
    dll = _path("RETRO_TTS_WINTALKER_DLL", "", "the x64 WinTalker.dll")
    lex = _path("RETRO_TTS_WINTALKER_LEX", "", "English.lex")
    wine = os.environ.get("RETRO_TTS_WINE")
    key = (str(runner), str(dll), str(lex), wine)
    if _host is not None and _host.poll() is None and _host_key == key:
        return _host
    _stop_host()
    command = [str(runner), "--server", str(dll), str(lex)]
    if wine:
        command.insert(0, wine)
    _host = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )
    _host_key = key
    return _host


def _read_exact(
    process: subprocess.Popen[bytes],
    length: int,
    cancelled: Callable[[], bool] | None,
) -> bytes:
    assert process.stdout is not None
    result = bytearray()
    while len(result) < length:
        if cancelled is not None and cancelled():
            _stop_host()
            raise RuntimeError("WinTalker synthesis cancelled")
        readable, _, _ = select.select([process.stdout], [], [], 0.01)
        if not readable:
            if process.poll() is not None:
                raise RuntimeError("WinTalker host exited")
            continue
        block = os.read(process.stdout.fileno(), length - len(result))
        if not block:
            raise RuntimeError("WinTalker host disconnected")
        result.extend(block)
    return bytes(result)


def text_to_wav(
    text: str,
    rate: int = 50,
    pitch: int = 50,
    *,
    cancelled: Callable[[], bool] | None = None,
) -> bytes:
    payload = text.encode("ascii", "replace")
    native_rate = 40 + round(max(0, min(100, rate)) * 9.6)
    request = struct.pack("<III", len(payload), native_rate, _native_pitch(pitch)) + payload
    with _lock:
        process = _get_host()
        assert process.stdin is not None
        try:
            process.stdin.write(request)
            process.stdin.flush()
            length = struct.unpack("<I", _read_exact(process, 4, cancelled))[0]
            if not length:
                raise RuntimeError("WinTalker synthesis failed")
            wav = _read_exact(process, length, cancelled)
        except (BrokenPipeError, OSError):
            _stop_host()
            raise RuntimeError("WinTalker host failed") from None
    if not wav.startswith(b"RIFF"):
        raise RuntimeError("WinTalker returned invalid audio")
    return wav


__all__ = ["text_to_wav"]
