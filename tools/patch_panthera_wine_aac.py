#!/usr/bin/env python3
"""Patch Panthera 0.95's host to pass AAC AudioSpecificConfig to Wine.

The upstream host supplies the 12-byte HEAACWAVEINFO extension Windows wants.
Wine's GStreamer Media Foundation bridge additionally requires the two-byte
AudioSpecificConfig (13 88 for AAC-LC, 22050 Hz, mono) as codec_data.

This patch is deliberately pinned to the exact 0.95.0 executable and verifies
both modified regions before writing anything.
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path


SOURCE_SHA256 = "c930f975a1f58616fedda7f5079d52892a494ae80579a388f7240df4807050ab"
SITE = 0x7ABC
CAVE = 0x3603E
ORIGINAL_SITE = bytes.fromhex(
    "8b4424148d54241883c40cc644240efe8b086a0c5268b0fa4300508b4168ffd0"
)
SITE_PATCH = bytes.fromhex("e97de50200") + b"\x90" * (len(ORIGINAL_SITE) - 5)
CAVE_PATCH = bytes.fromhex(
    "8b442414" "8d542418" "83c40c" "c644240efe" "66c74424181388"
    "8b08" "6a0e" "52" "68b0fa4300" "50" "8b4168" "ffd0" "e9721afdff"
)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT OUTPUT")
    source, output = map(Path, sys.argv[1:])
    data = bytearray(source.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise SystemExit(f"refusing unknown Panthera host: sha256 {digest}")
    if data[SITE:SITE + len(ORIGINAL_SITE)] != ORIGINAL_SITE:
        raise SystemExit("refusing host with unexpected AAC call-site bytes")
    if any(data[CAVE:CAVE + len(CAVE_PATCH)]):
        raise SystemExit("refusing host whose selected code cave is not empty")
    data[SITE:SITE + len(SITE_PATCH)] = SITE_PATCH
    data[CAVE:CAVE + len(CAVE_PATCH)] = CAVE_PATCH
    output.write_bytes(data)
    print(hashlib.sha256(data).hexdigest())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
