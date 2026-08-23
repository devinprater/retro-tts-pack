from __future__ import annotations

import json
import struct
import sys

from .engines.softvoice import text_to_wav


def _read_exact(source, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        block = source.read(length - len(result))
        if not block:
            raise EOFError("SoftVoice worker input closed")
        result.extend(block)
    return bytes(result)


def main() -> int:
    source = sys.stdin.buffer
    target = sys.stdout.buffer
    while True:
        header = source.read(4)
        if not header:
            return 0
        if len(header) != 4:
            return 1
        try:
            request = json.loads(_read_exact(source, struct.unpack("!I", header)[0]))
            wav = text_to_wav(
                str(request["text"]), voice=request.get("voice"),
                rate=int(request.get("rate", 50)),
            )
            response = struct.pack("!BI", 0, len(wav)) + wav
        except Exception as error:
            message = str(error).encode("utf-8", "replace")
            response = struct.pack("!BI", 1, len(message)) + message
        target.write(response)
        target.flush()


if __name__ == "__main__":
    raise SystemExit(main())
