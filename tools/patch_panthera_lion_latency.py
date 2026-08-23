#!/usr/bin/env python3
"""Reduce Panthera 0.95's end-of-utterance guard from 300 to 150 ms.

Lion keeps its audio graph running between utterances, unlike Tiger and
Leopard, so Panthera otherwise waits for thirty quiet 10 ms polls after every
render. Upstream measured 150 ms as sufficient for ordinary speech. The patch
is pinned to Retro's Wine-AAC-corrected Panthera host and verifies the exact
instruction before writing.
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path


SOURCE_SHA256 = "a8f0c58d56e66a69bdafcb300541fb44780bfc8fce44db8ce3a0f591ac270d31"
QUIET_LIMIT_OFFSET = 0xEB25
ORIGINAL_LIMIT = 30
FAST_LIMIT = 15


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT OUTPUT")
    source, output = map(Path, sys.argv[1:])
    data = bytearray(source.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise SystemExit(f"refusing unknown Panthera host: sha256 {digest}")
    if data[QUIET_LIMIT_OFFSET] != ORIGINAL_LIMIT:
        raise SystemExit("refusing host with unexpected quiet-limit instruction")
    data[QUIET_LIMIT_OFFSET] = FAST_LIMIT
    output.write_bytes(data)
    print(hashlib.sha256(data).hexdigest())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
