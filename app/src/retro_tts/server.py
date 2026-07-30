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


def _stream_amiga(connection: socket.socket, request: dict[str, object]) -> None:
    from .engines.amiganarrator import stream_pcm

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

    def connected() -> bool:
        readable, _, _ = select.select([connection], [], [], 0)
        if not readable:
            return True
        try:
            return bool(connection.recv(1, socket.MSG_PEEK))
        except (BlockingIOError, ConnectionResetError):
            return False

    def send_audio(block: bytes) -> bool:
        if playback.poll() is not None or not connected():
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
        if not playback.stdin.closed:
            playback.stdin.close()


def _serve(connection: socket.socket) -> None:
    with connection:
        try:
            length = struct.unpack("!I", _receive_exact(connection, 4))[0]
            request = json.loads(_receive_exact(connection, length))
            if request.get("play") and request["engine"] == "amiganarrator":
                _stream_amiga(connection, request)
                wav = b""
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
        while True:
            connection, _ = listener.accept()
            threading.Thread(target=_serve, args=(connection,), daemon=True).start()


if __name__ == "__main__":
    raise SystemExit(main())
