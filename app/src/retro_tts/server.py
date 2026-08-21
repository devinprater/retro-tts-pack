from __future__ import annotations

import json
import os
import queue
import select
import shutil
import socket
import struct
import subprocess
import threading
import time
from pathlib import Path

from .cli import _receive_exact, _render
from .engines.audio import PCM16PauseShortener, shorten_wav_pauses


_amiga_state_lock = threading.Lock()
_amiga_render_lock = threading.Lock()
_amiga_cancel: threading.Event | None = None
_leopard_state_lock = threading.Lock()
_leopard_cancel: threading.Event | None = None


def _preload() -> None:
    """Keep service startup cheap; engines remain resident after first use.

    Preloading every optional engine retained several emulators and Wine hosts
    even when the user selected only one synthesizer.  Lazy loading preserves
    the persistent renderer's benefit for subsequent utterances without the
    large idle-memory cost.
    """
    if os.environ.get("RETRO_TTS_LEOPARD_BACKEND", "auto") == "native":
        from .engines.leopardspeech import preload
        preload()


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

        shortener = PCM16PauseShortener(22_200, send_audio)
        try:
            stream_pcm(
                str(request["text"]),
                int(request.get("rate", 50)),
                int(request.get("pitch", 50)),
                shortener.feed,
            )
            shortener.finish()
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


def _new_leopard_request() -> threading.Event:
    global _leopard_cancel
    cancelled = threading.Event()
    with _leopard_state_lock:
        if _leopard_cancel is not None:
            _leopard_cancel.set()
        _leopard_cancel = cancelled
    return cancelled


def _stream_leopard(
    connection: socket.socket,
    request: dict[str, object],
    cancelled: threading.Event,
) -> None:
    from .engines.leopardspeech import stream_pcm

    player = shutil.which("pw-play")
    if not player:
        raise RuntimeError("pw-play is required for streaming LeopardSpeech")
    playback = subprocess.Popen(
        [
            player, "--raw", "--rate", "22050", "--channels", "1",
            "--format", "s16", "--latency", "10ms", "-",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )
    assert playback.stdin is not None
    os.set_blocking(playback.stdin.fileno(), False)
    # Match LeopardSpeech 0.7.2's 80 ms feed horizon. Keeping at most two
    # chunks queued prevents completed synthesis from running far ahead of
    # Orca cancellation while still insulating PipeWire from jitter.
    audio: queue.Queue[bytes | None] = queue.Queue(maxsize=2)
    chunk_bytes = round(22050 * 0.080) * 2

    def write_block(block: bytes) -> bool:
        pending = memoryview(block)
        deadline = time.monotonic() + 1.0
        while pending:
            if cancelled.is_set() or playback.poll() is not None:
                return False
            if time.monotonic() >= deadline:
                return False
            _, writable, _ = select.select([], [playback.stdin], [], 0.01)
            if not writable:
                continue
            try:
                written = os.write(playback.stdin.fileno(), pending)
            except (BlockingIOError, BrokenPipeError):
                if playback.poll() is not None:
                    return False
                continue
            pending = pending[written:]
            deadline = time.monotonic() + 1.0
        return True

    def feed_player() -> None:
        clean_end = False
        try:
            while not cancelled.is_set():
                block = audio.get()
                if block is None:
                    clean_end = True
                    break
                if not write_block(block):
                    break
            if clean_end and not cancelled.is_set():
                playback.stdin.close()
                while playback.poll() is None and not cancelled.wait(0.01):
                    pass
        finally:
            if playback.poll() is None:
                playback.terminate()
            try:
                playback.wait(timeout=1)
            except subprocess.TimeoutExpired:
                playback.kill()
                playback.wait()
            if playback.stdin is not None and not playback.stdin.closed:
                playback.stdin.close()

    writer = threading.Thread(target=feed_player, daemon=True)
    writer.start()
    completed = False

    def finish_audio() -> None:
        while writer.is_alive() and not cancelled.is_set():
            try:
                audio.put(None, timeout=0.01)
                return
            except queue.Full:
                continue

    def send_audio(block: bytes) -> bool:
        if cancelled.is_set() or playback.poll() is not None or not _connected(connection):
            return False
        for offset in range(0, len(block), chunk_bytes):
            chunk = block[offset:offset + chunk_bytes]
            while True:
                if cancelled.is_set() or playback.poll() is not None or not _connected(connection):
                    return False
                try:
                    audio.put(chunk, timeout=0.01)
                    break
                except queue.Full:
                    continue
        return True

    try:
        stream_pcm(
            str(request["text"]),
            int(request.get("rate", 50)),
            int(request.get("pitch", 50)),
            send_audio,
            volume=int(request.get("volume", 90)),
            voice=request.get("voice"),
            cancelled=lambda: cancelled.is_set() or not _connected(connection),
        )
        finish_audio()
        completed = True
    finally:
        if not completed:
            cancelled.set()
    if completed:
        # Keep sd_generic's command alive until its audio finishes. Speech
        # Dispatcher uses that lifetime to serialize ordinary consecutive
        # chunks (for example "CPU" followed by "RAM") and closes the client
        # when it truly wants to interrupt. Returning as soon as synthesis
        # completed made every queued chunk look concurrent, so the
        # latest-request token discarded all but the last one.
        while writer.is_alive():
            if not _connected(connection):
                cancelled.set()
            writer.join(0.01)


def _serve(connection: socket.socket) -> None:
    with connection:
        try:
            length = struct.unpack("!I", _receive_exact(connection, 4))[0]
            request = json.loads(_receive_exact(connection, length))
            if request.get("play") and request["engine"] == "amiganarrator":
                _stream_amiga(connection, request, _new_amiga_request())
                wav = b""
            elif request.get("play") and request["engine"] == "leopardspeech":
                _stream_leopard(connection, request, _new_leopard_request())
                wav = b""
            elif request["engine"] in ("wintalker", "leopardspeech"):
                if request["engine"] == "wintalker":
                    from .engines.wintalker import text_to_wav
                else:
                    from .engines.leopardspeech import text_to_wav

                wav = text_to_wav(
                    str(request["text"]),
                    int(request.get("rate", 50)),
                    int(request.get("pitch", 50)),
                    **({"voice": request.get("voice"), "volume": int(request.get("volume", 90))}
                       if request["engine"] == "leopardspeech" else {}),
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
                wav = shorten_wav_pauses(wav)
            else:
                wav = _render(
                    request["engine"],
                    request["text"],
                    int(request.get("rate", 50)),
                    int(request.get("pitch", 50)),
                    request.get("voice"),
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
