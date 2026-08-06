from __future__ import annotations

import json
import os
import select
import shutil
import socket
import struct
import subprocess
import threading
from pathlib import Path

from .cli import _receive_exact, _render


_amiga_state_lock = threading.Lock()
_amiga_render_lock = threading.Lock()
_amiga_cancel: threading.Event | None = None


def _preload() -> None:
    """Pay one-time import/engine startup costs when the service starts."""
    from .engines import stspeech  # noqa: F401

    loaders = []
    try:
        from .engines.monologue import _get_engine
        loaders.append(_get_engine)
    except Exception:
        pass
    try:
        from .engines.bestspeech import _get_engine
        loaders.append(_get_engine)
    except Exception:
        pass
    try:
        from .engines.softvoice import _get_engine
        loaders.append(_get_engine)
    except Exception:
        pass
    try:
        from .engines.wintalker import _get_host
        loaders.append(_get_host)
    except Exception:
        pass
    for load in loaders:
        try:
            load()
        except Exception:
            # Optional engines report their actionable error when selected;
            # one missing asset must not prevent the others from warming up.
            pass


def _connected(connection: socket.socket) -> bool:
    readable, _, _ = select.select([connection], [], [], 0)
    if not readable:
        return True
    try:
        return bool(connection.recv(1, socket.MSG_PEEK))
    except (BlockingIOError, ConnectionResetError):
        return False


def _stream_amiga(
    connection: socket.socket,
    request: dict[str, object],
    cancelled: threading.Event,
) -> None:
    from .engines.amiganarrator import stream_pcm

    with _amiga_render_lock:
        if cancelled.is_set() or not _connected(connection):
            return
        player = shutil.which("pw-play")
        if not player:
            raise RuntimeError("pw-play is required for streaming Amiga Narrator")
        playback = subprocess.Popen(
            [
                player, "--raw", "--rate", "22200", "--channels", "1",
                "--format", "s16", "--latency", "10ms", "-",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            bufsize=0,
        )
        assert playback.stdin is not None

        def send_audio(block: bytes) -> bool:
            if (
                cancelled.is_set()
                or playback.poll() is not None
                or not _connected(connection)
            ):
                return False
            try:
                playback.stdin.write(block)
                return True
            except BrokenPipeError:
                return False

        try:
            stream_pcm(
                str(request["text"]),
                int(request.get("rate", 50)),
                int(request.get("pitch", 50)),
                send_audio,
            )
            playback.stdin.close()
            playback.wait(timeout=10)
        finally:
            if playback.poll() is None:
                playback.terminate()
                try:
                    playback.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    playback.kill()
                    playback.wait()
            if not playback.stdin.closed:
                playback.stdin.close()


def _new_amiga_request() -> threading.Event:
    global _amiga_cancel
    cancelled = threading.Event()
    with _amiga_state_lock:
        if _amiga_cancel is not None:
            _amiga_cancel.set()
        _amiga_cancel = cancelled
    return cancelled


def _serve(connection: socket.socket) -> None:
    with connection:
        try:
            length = struct.unpack("!I", _receive_exact(connection, 4))[0]
            request = json.loads(_receive_exact(connection, length))
            if request.get("play") and request["engine"] == "amiganarrator":
                _stream_amiga(connection, request, _new_amiga_request())
                wav = b""
            elif request["engine"] == "wintalker":
                from .engines.wintalker import text_to_wav

                wav = text_to_wav(
                    str(request["text"]),
                    int(request.get("rate", 50)),
                    int(request.get("pitch", 50)),
                    cancelled=lambda: not _connected(connection),
                )
            elif request["engine"] in ("smoothtalker", "monologue"):
                rate = max(0, min(100, int(request.get("rate", 50))))
                pitch = max(0, min(100, int(request.get("pitch", 50))))
                if request["engine"] == "smoothtalker":
                    from .engines.smoothtalker import text_to_wav
                else:
                    from .engines.monologue import text_to_wav
                wav = text_to_wav(
                    str(request["text"]),
                    rate=round(rate * 0.09),
                    pitch=round(pitch * 0.09),
                    cancelled=lambda: not _connected(connection),
                )
            else:
                wav = _render(
                    request["engine"],
                    request["text"],
                    int(request.get("rate", 50)),
                    int(request.get("pitch", 50)),
                )
            response = struct.pack("!BI", 0, len(wav)) + wav
        except Exception as error:
            message = str(error).encode("utf-8", "replace")
            response = struct.pack("!BI", 1, len(message)) + message
        try:
            connection.sendall(response)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main() -> int:
    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp"))
    path = Path(os.environ.get("RETRO_TTS_SOCKET", runtime / "retro-tts.sock"))
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        path.unlink()
    except FileNotFoundError:
        pass
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
        listener.bind(str(path))
        path.chmod(0o600)
        listener.listen(16)
        _preload()
        while True:
            connection, _ = listener.accept()
            threading.Thread(target=_serve, args=(connection,), daemon=True).start()


if __name__ == "__main__":
    raise SystemExit(main())
