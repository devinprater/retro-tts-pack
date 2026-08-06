#!/usr/bin/env python3
"""Download assets deliberately published in upstream GitHub releases."""

from __future__ import annotations

import hashlib
import io
import os
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path


DOWNLOADS = (
    {
        "name": "BeSTSpeech 2025.5 binary release",
        "url": (
            "https://github.com/samtupy/b32tts_wrapper/releases/download/"
            "2025.5/b32_bin_2025.5.zip"
        ),
        "archive_sha256": (
            "3658c82f4f861010b865196ab066d466ad3af9b07b7116a06d4da688b142b52f"
        ),
        "files": {
            "b32_tts.dll": (
                "bestspeech/b32_tts.dll",
                "36e02d71c1c964662f1ae7689f0237fd5328f537d645e6ede3f4bcd1f316efcc",
            ),
        },
    },
    {
        "name": "Prose 2000 v1.1.0 NVDA add-on release",
        "url": (
            "https://github.com/OnjLouis/prose2000/releases/download/"
            "v1.1.0/prose2000-1.1.0.nvda-addon"
        ),
        "archive_sha256": (
            "fe2d441d07338277dc1bd6df6e6116af552de2f32df806eabb684f1608661156"
        ),
        "files": {
            "synthDrivers/_prose2000/roms/v3.12__8-9-88__dsp_data.u29": (
                "prose2000/v3.12__8-9-88__dsp_data.u29",
                "96f73b10205fe3754dbe88bd624caabc30e267b8ee85d1cc4ac4753315be9eef",
            ),
            "synthDrivers/_prose2000/roms/v3.12__8-9-88__dsp_prog.u29": (
                "prose2000/v3.12__8-9-88__dsp_prog.u29",
                "e275fac12ea6afc0140ec32e17c7108f27c7ecaf4695ce342d02bbb84f1a330f",
            ),
            "synthDrivers/_prose2000/roms/v3.4.1__2000__0.u21": (
                "prose2000/v3.4.1__2000__0.u21",
                "0606668a1a873a99e525681ab7eb437b7b35e79fd38a28967114e7ac9de5b657",
            ),
            "synthDrivers/_prose2000/roms/v3.4.1__2000__1.u44": (
                "prose2000/v3.4.1__2000__1.u44",
                "652b866d235f7c91e8ff90e8782d2793d442308f1b2f7b09e54db8de0b38116c",
            ),
            "synthDrivers/_prose2000/roms/v3.4.1__2000__2.u22": (
                "prose2000/v3.4.1__2000__2.u22",
                "86186e0200c994d88ae5fe53a5b4b804746df59e6599ffc40abe70065ee30d03",
            ),
            "synthDrivers/_prose2000/roms/v3.4.1__2000__3.u45": (
                "prose2000/v3.4.1__2000__3.u45",
                "fe091c4cad8fab452a24d07f596517805494932e5f0e28d9cc782700d9b41d1b",
            ),
        },
    },
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(url: str) -> bytes:
    request = urllib.request.Request(
        url, headers={"User-Agent": "retro-tts-pack/0.2.1"}
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def install_file(target: Path, data: bytes, expected: str) -> None:
    actual = digest(data)
    if actual != expected:
        raise RuntimeError(
            f"checksum mismatch for {target.name}: expected {expected}, got {actual}"
        )
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if digest(target.read_bytes()) == expected:
            print(f"Already present: {target}")
            return
        raise RuntimeError(
            f"refusing to overwrite an unrecognized existing file: {target}"
        )
    fd, temporary = tempfile.mkstemp(prefix=f".{target.name}.", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
        os.replace(temporary, target)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
    print(f"Installed: {target}")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} ASSET_DIRECTORY", file=sys.stderr)
        return 2
    destination = Path(sys.argv[1]).expanduser().resolve()
    for item in DOWNLOADS:
        print(f"Downloading {item['name']}...")
        archive = fetch(item["url"])
        actual = digest(archive)
        if actual != item["archive_sha256"]:
            raise RuntimeError(
                f"archive checksum mismatch for {item['name']}: {actual}"
            )
        with zipfile.ZipFile(io.BytesIO(archive)) as package:
            for member, (relative, expected) in item["files"].items():
                install_file(destination / relative, package.read(member), expected)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
