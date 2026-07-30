from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

def _percent(value: int) -> int:
    return max(0, min(100, value))


def _render(engine: str, text: str, rate: int, pitch: int) -> bytes:
    rate = _percent(rate)
    pitch = _percent(pitch)
    if engine == "sam":
        from .engines.sam import text_to_wav as sam_to_wav
        # SAM speed is inverse: smaller values speak faster.
        speed = round(180 - rate * 1.6)
        sam_pitch = round(20 + pitch * 1.8)
        wav = sam_to_wav(text, pitch=sam_pitch, speed=speed)
    elif engine == "stspeech":
        from .engines.stspeech import text_to_wav as stspeech_to_wav
        # STSpeech's rate parameter is a frame delay: larger is slower.
        if rate <= 50:
            st_rate = 200 - round(rate * 121 / 50)
        else:
            st_rate = 79 - round((rate - 50) * 59 / 50)
        st_pitch = round(20 + pitch * 1.8)
        wav = stspeech_to_wav(text, rate=st_rate, pitch=st_pitch)
    elif engine == "smoothtalker":
        from .engines.smoothtalker import text_to_wav as smoothtalker_to_wav
        smooth_rate = round(rate * 0.09)
        smooth_pitch = round(pitch * 0.09)
        wav = smoothtalker_to_wav(text, rate=smooth_rate, pitch=smooth_pitch)
    elif engine == "monologue":
        from .engines.monologue import text_to_wav as monologue_to_wav
        engine_rate = round(rate * 0.09)
        engine_pitch = round(pitch * 0.09)
        wav = monologue_to_wav(text, rate=engine_rate, pitch=engine_pitch)
    elif engine == "prose2000":
        from .engines.prose2000 import text_to_wav as prose2000_to_wav
        wav = prose2000_to_wav(text, rate=rate, pitch=pitch)
    elif engine == "doubletalkpc":
        from .engines.doubletalkpc import text_to_wav as doubletalkpc_to_wav
        wav = doubletalkpc_to_wav(text, rate=rate, pitch=pitch)
    elif engine == "bestspeech":
        from .engines.bestspeech import text_to_wav as bestspeech_to_wav
        wav = bestspeech_to_wav(text, rate=rate, pitch=pitch)
    elif engine == "amiganarrator":
        from .engines.amiganarrator import text_to_wav as amiganarrator_to_wav
        wav = amiganarrator_to_wav(text, rate=rate, pitch=pitch)
    elif engine == "softvoice":
        from .engines.softvoice import text_to_wav as softvoice_to_wav
        wav = softvoice_to_wav(text)
    else:
        raise ValueError(f"unknown engine: {engine}")
    if not wav:
        raise RuntimeError(f"{engine} produced no audio")
    return wav


def _play(wav: bytes) -> int:
    player = shutil.which("pw-play") or shutil.which("paplay") or shutil.which("aplay")
    if not player:
        raise RuntimeError("no supported audio player found (pw-play, paplay, or aplay)")

    suffix = ".wav"
    path = ""
    process: subprocess.Popen[bytes] | None = None
    try:
        with tempfile.NamedTemporaryFile(prefix="retro-tts-", suffix=suffix, delete=False) as output:
            output.write(wav)
            path = output.name

        command = [player, path]
        if Path(player).name == "pw-play":
            # pw-play otherwise requests 100 ms of buffering, which dominates
            # the onset latency of short screen-reader utterances.
            command = [player, "--latency", "10ms", path]
        elif Path(player).name == "paplay":
            command = [player, "--latency-msec=10", path]
        process = subprocess.Popen(command)

        def stop(_signum: int, _frame: object) -> None:
            if process and process.poll() is None:
                process.terminate()

        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        return process.wait()
    finally:
        if path:
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass


def _persistent_render(
    engine: str, text: str, rate: int, pitch: int, *, play: bool = False
) -> bytes:
    socket_path = os.environ.get(
        "RETRO_TTS_SOCKET",
        str(Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp")) / "retro-tts.sock"),
    )
    request = json.dumps(
        {"engine": engine, "text": text, "rate": rate, "pitch": pitch, "play": play}
    ).encode("utf-8")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(60)
        client.connect(socket_path)
        client.sendall(struct.pack("!I", len(request)) + request)
        header = _receive_exact(client, 5)
        status, length = struct.unpack("!BI", header)
        payload = _receive_exact(client, length)
    if status:
        raise RuntimeError(payload.decode("utf-8", "replace"))
    return payload


def _receive_exact(connection: socket.socket, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        block = connection.recv(length - len(result))
        if not block:
            raise RuntimeError("persistent renderer disconnected")
        result.extend(block)
    return bytes(result)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="retro-tts")
    parser.add_argument(
        "--engine",
        choices=(
            "sam", "stspeech", "smoothtalker", "monologue", "prose2000",
            "doubletalkpc",
            "bestspeech", "softvoice",
            "amiganarrator",
        ),
        required=True,
    )
    parser.add_argument("--rate", type=int, default=50, help="rate from 0 through 100")
    parser.add_argument("--pitch", type=int, default=50, help="pitch from 0 through 100")
    parser.add_argument("--output", type=Path, help="write a WAV file instead of playing")
    parser.add_argument(
        "--persistent", action="store_true",
        help="render through the persistent retro-tts service",
    )
    parser.add_argument("--text", help="text to speak; stdin is used when omitted")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    text = args.text if args.text is not None else sys.stdin.read()
    if not text.strip():
        return 0
    if args.persistent:
        daemon_plays = args.engine == "amiganarrator" and args.output is None
        wav = _persistent_render(
            args.engine, text, args.rate, args.pitch, play=daemon_plays
        )
        if daemon_plays:
            return 0
    else:
        wav = _render(args.engine, text, args.rate, args.pitch)
    if args.output:
        args.output.write_bytes(wav)
        return 0
    return _play(wav)


if __name__ == "__main__":
    raise SystemExit(main())
