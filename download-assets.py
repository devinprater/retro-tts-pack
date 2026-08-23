#!/usr/bin/env python3
"""Download checksum-pinned assets from known project and archive sources."""

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
    {
        "name": "SmoothTalker 1.0 NVDA add-on (DECtalk.nu/datajake mirrors)",
        "urls": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/"
            "smoothtalker-1.0.nvda-addon",
            "https://datajake.braillescreen.net/TTS/SynthesizersForNVDA/"
            "AI_Generated/smoothtalker-1.0.nvda-addon",
        ),
        "archive_sha256": (
            "cc540cab71101de39109a194336d6ab29b18f68cc47e04e9bd40cc01f9677cfe"
        ),
        "files": {
            "synthDrivers\\_smoothtalker_engine\\engine.bin": (
                "smoothtalker/engine.bin",
                "d664381812f242b6b5771e805805c68ef9df968d8673252132570236495ab038",
            ),
        },
    },
    {
        "name": "Monologue 1.1 NVDA add-on (DECtalk.nu/datajake mirrors)",
        "urls": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/"
            "monologue-1.1.nvda-addon",
            "https://datajake.braillescreen.net/TTS/SynthesizersForNVDA/"
            "AI_Generated/monologue-1.1.nvda-addon",
        ),
        "archive_sha256": (
            "30af5e6b69053959e6ac2ca64fed849462a636abc182ae62300f2b68d75ab574"
        ),
        "files": {
            "synthDrivers/_monologue_engine/bin/FB_11K8.DLL": (
                "monologue/FB_11K8.DLL",
                "fee96c7041be6fcb62bd4894da869c8d1ce544b913a04d116e5407f3c5852ec6",
            ),
            "synthDrivers/_monologue_engine/bin/FB_22K16.DLL": (
                "monologue/FB_22K16.DLL",
                "33c3a69ab5af26f11384c41eae9ee93ff6f2ea83a9a5a86714c8dd3f0d3bbf7a",
            ),
            "synthDrivers/_monologue_engine/bin/FB_DEFLT.DIC": (
                "monologue/FB_DEFLT.DIC",
                "7d95937acfee7b44b7d8bf9bb3d2263ad1425d3baf6e41c423fe51a418d7134b",
            ),
            "synthDrivers/_monologue_engine/bin/FB_NGN.EXE": (
                "monologue/FB_NGN.EXE",
                "7284aa93a11204089e0bd9f8af3fa35022ca79dcce7ec07f7b8e6d1c655f736f",
            ),
            "synthDrivers/_monologue_engine/bin/FB_SPCH.DLL": (
                "monologue/FB_SPCH.DLL",
                "85b4c71fae2205e0b15d675aefb61058d6fce8828464290d2e3631276a519ed1",
            ),
            "synthDrivers/_monologue_engine/bin/FB_TIMER.DLL": (
                "monologue/FB_TIMER.DLL",
                "1581d55ddddb66cd36a7d1eeaf8cea98c289c2de0d92a2e5ca701b971b2a42fa",
            ),
        },
    },
    {
        "name": "DoubleTalk PC 0.1.12 NVDA add-on (DECtalk.nu/datajake mirrors)",
        "urls": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/"
            "doubletalkpc.nvda-addon",
            "https://datajake.braillescreen.net/TTS/SynthesizersForNVDA/"
            "AI_Generated/doubletalkpc.nvda-addon",
        ),
        "archive_sha256": (
            "cea64e4aa79c79f240d617b7578b308cd7a75d66385b865a4553bdb25f88b19f"
        ),
        "files": {
            "synthDrivers/doubletalkpc/doubletalkpc.bin": (
                "doubletalkpc/doubletalkpc.bin",
                "7629885bd2ea5a9eb533a8f229240c23560d7226f1f38d990804e53fee860b39",
            ),
        },
    },
    {
        "name": "WinTalker 1.0 NVDA add-on (DECtalk.nu/datajake mirrors)",
        "urls": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/WinTalker.nvda-addon",
            "https://datajake.braillescreen.net/TTS/SynthesizersForNVDA/"
            "WinTalker.nvda-addon",
        ),
        "archive_sha256": (
            "0a0447bafa465a39e889a862fd721abc411bb439ee8b3e8ae135e23c64aceff5"
        ),
        "files": {
            "synthDrivers/wintalker_data/x64/WinTalker.dll": (
                "wintalker/WinTalker.dll",
                "4e16e70550a579e620c8890daf741709dc54af37cc144a28e0e5021e1d8e1901",
            ),
            "synthDrivers/wintalker_data/English.lex": (
                "wintalker/English.lex",
                "31243fc7dd9e5dfb15973491c59f30ef39d42b8317c16c3521e14554e93f93de",
            ),
        },
    },
    {
        "name": "SoftVoice 2025.3.8 NVDA add-on (DECtalk.nu mirror)",
        "url": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/softvoice-2026.nvda-addon"
        ),
        "archive_sha256": (
            "46221c597db800d266efcc08f48028e6463115896a29d2b5c765bf6465f97ce0"
        ),
        "files": {
            "synthDrivers/tibase32.dll": (
                "softvoice/tibase32.dll",
                "9297f69236b296238096baa1e9d00567fc74409b5a7ebe2565da71b27fcdc5cb",
            ),
            "synthDrivers/tieng32.dll": (
                "softvoice/tieng32.dll",
                "4e261dcdf4eca118cf75c39b2f52d5b00888de820df9e4e868183a039f25e98b",
            ),
        },
    },
    {
        "name": "Amiga Narrator 1.0.0 NVDA add-on (DECtalk.nu mirror)",
        "url": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/"
            "amigaNarrator-1.0.0.nvda-addon"
        ),
        "archive_sha256": (
            "6bc250aca4c0039f70c367452367b43598ca4b781824c1ffde1fec7b3f0cc891"
        ),
        "files": {
            "synthDrivers/_amigaNarrator/narrator.device": (
                "amiganarrator/narrator.device",
                "f1fcd86749abed4aec158dc1e6e7ae297500fe6fbe3e3ccc4fcc02bbf62217d7",
            ),
            "synthDrivers/_amigaNarrator/translator.library": (
                "amiganarrator/translator.library",
                "874f705749bed5fdd7d9080c631c9e2e05b5d08743175166a2d1722f6ed55dfa",
            ),
            "synthDrivers/_amigaNarrator/cmudict.txt": (
                "amiganarrator/cmudict.txt",
                "d56f4043f5bf3cea38c7b8e495128338a13df07914c543bdb957183daa7347e5",
            ),
        },
    },
    {
        "name": "EchoTalk 0.1.0 NVDA add-on (DECtalk.nu mirror)",
        "url": (
            "https://dectalk.nu/Software%20and%20Manuals/Software/"
            "NVDA%20Add-ons/NVDA%202019.3%20and%20Beyond/echotalk.nvda-addon"
        ),
        "archive_sha256": "601a13965e8894ce99a810f8dd68b6ca74008423fe1d6088c68afd0586dd9ba4",
        "files": {
            "synthDrivers/echotalk/textalker.obj.bin": ("echotalk/textalker.obj.bin", "9bf638e0b2cdabc12cb681d8e4fb15e03fdf85aae494b1d47cd959415ce96579"),
            "synthDrivers/echotalk/textalker.ram.bin": ("echotalk/textalker.ram.bin", "080106d5584138d86357f308e63aa5016adcdfcbf7130d629bb3f50078e8911b"),
            "synthDrivers/echotalk/textalker_v13.obj.bin": ("echotalk/textalker_v13.obj.bin", "b787adef5008dc6a971239bcfcf85fccf4f730da7e59232d3ef5929c7bcbe65a"),
            "synthDrivers/echotalk/textalker_v13.ram.bin": ("echotalk/textalker_v13.ram.bin", "4f0e39718a91105e1acf48054774c47145bc124bac34cc6ae7d3149d5db07e91"),
        },
    },
    {
        "name": "OutSpoken MacinTalk ROM collection (DECtalk.nu mirror)",
        "url": "https://dectalk.nu/Software%20and%20Manuals/Software/Miscellaneous/outspoken-roms.zip",
        "archive_sha256": "f4573637b651f7fc2f0201b02c685894f62ad41bb2badd718f94bf474ed7275e",
        "tree": ("outspoken-roms/", "outspoken/outspoken-roms"),
    },
    {
        "name": "Leopard MacinTalk data (DECtalk.nu package)",
        "url": "https://dectalk.nu/Software%20and%20Manuals/Software/Miscellaneous/leopardspeech-data.zip",
        "archive_sha256": (
            "ad678e5fb09ed1082a6977fc88d1fac2dd43212d5eabceb114ecbd8d6ea01233"
        ),
        "tree": (
            "leopardspeech-data/",
            "leopardspeech/leopardspeech-data",
        ),
    },
    {
        "name": "Tiger MacinTalk data (DECtalk.nu package)",
        "url": "https://dectalk.nu/Software%20and%20Manuals/Software/Miscellaneous/tigerspeech-data.zip",
        "archive_sha256": (
            "887ce60fc83d968b1d1de580ec0a83855b087b985b2125cb1fa6fc07d2602fbc"
        ),
        "tree": ("tigerspeech-data/", "tigerspeech/tigerspeech-data"),
    },
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(urls: str | tuple[str, ...]) -> bytes:
    if isinstance(urls, str):
        urls = (urls,)
    failures = []
    for url in urls:
        try:
            request = urllib.request.Request(
                url, headers={"User-Agent": "retro-tts-pack/1.0.0"}
            )
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except Exception as error:
            failures.append(f"{url}: {error}")
    raise RuntimeError("all download sources failed:\n" + "\n".join(failures))


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


def install_tree(
    package: zipfile.ZipFile, source_prefix: str, destination: Path
) -> None:
    """Install one archive subtree after the archive itself was verified."""
    prefix = Path(source_prefix)
    installed = 0
    for info in package.infolist():
        member = Path(info.filename)
        if info.is_dir() or not member.is_relative_to(prefix):
            continue
        relative = member.relative_to(prefix)
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError(f"unsafe archive member: {info.filename}")
        target = destination / relative
        data = package.read(info)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            if target.is_file() and target.read_bytes() == data:
                continue
            raise RuntimeError(
                f"refusing to overwrite an existing file: {target}"
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
        installed += 1
    if not installed and not destination.exists():
        raise RuntimeError(f"archive subtree is missing: {source_prefix}")
    print(f"Installed tree: {destination} ({installed} new files)")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} ASSET_DIRECTORY", file=sys.stderr)
        return 2
    destination = Path(sys.argv[1]).expanduser().resolve()
    for item in DOWNLOADS:
        print(f"Downloading {item['name']}...")
        archive = fetch(item.get("urls", item.get("url")))
        actual = digest(archive)
        if actual != item["archive_sha256"]:
            raise RuntimeError(
                f"archive checksum mismatch for {item['name']}: {actual}"
            )
        with zipfile.ZipFile(io.BytesIO(archive)) as package:
            if "files" in item:
                for member, (relative, expected) in item["files"].items():
                    install_file(destination / relative, package.read(member), expected)
            else:
                source_prefix, relative = item["tree"]
                install_tree(package, source_prefix, destination / relative)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
