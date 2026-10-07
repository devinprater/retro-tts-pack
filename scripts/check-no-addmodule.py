#!/usr/bin/env python3
"""Fail if this repo instructs anyone to add a Speech Dispatcher `AddModule` line.

One explicit `AddModule` line turns module autodiscovery OFF: afterwards only the
modules named in speechd.conf load, so every other synthesizer the user has
disappears and some retro voice becomes the system default. That is the reported
"jump scare". Autodiscovery already finds these modules by directory, so an
`AddModule` line is never needed.

A bare grep is not enough -- the installer legitimately MENTIONS AddModule in a
comment explaining why not to do this, and in a user-facing message about
stripping a block left by an older version. So this checks intent, not spelling:

  * shell: comments are stripped, and message strings (`say "..."`/`warn "..."`)
    are exempt; any remaining AddModule is a failure
  * python: any AddModule -- it would be written into a config
  * speechd .conf: always a failure, that is the file that causes the breakage
  * markdown: flagged only inside fenced code blocks, since that is what a user
    copies; prose explaining the hazard is fine
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

FAILS: list[str] = []


def tracked_files() -> list[Path]:
    try:
        out = subprocess.run(
            ["git", "ls-files"], capture_output=True, text=True, check=True
        ).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        sys.exit("this check must run from inside the git repository")
    return [Path(p) for p in out.splitlines() if p.strip()]


def check_shell(path: Path, text: str) -> None:
    for i, line in enumerate(text.splitlines(), 1):
        if "AddModule" not in line:
            continue
        stripped = line.lstrip()
        if stripped.startswith("#"):            # explanatory comment: intended
            continue
        if re.search(r'\b(?:say|warn)\s+"', line):  # user-facing message: intended
            continue
        FAILS.append(f"{path}:{i}: shell code would register a module: {line.strip()}")


def check_python(path: Path, text: str) -> None:
    for i, line in enumerate(text.splitlines(), 1):
        if "AddModule" in line and not line.lstrip().startswith("#"):
            FAILS.append(f"{path}:{i}: python would write an AddModule line: {line.strip()}")


def check_conf(path: Path, text: str) -> None:
    for i, line in enumerate(text.splitlines(), 1):
        if "AddModule" in line and not line.lstrip().startswith("#"):
            FAILS.append(f"{path}:{i}: config registers a module: {line.strip()}")


def check_markdown(path: Path, text: str) -> None:
    """Only fenced code blocks matter: that is what a user copies and runs."""
    in_fence = False
    fence = ""
    for i, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            marker = stripped[:3]
            if not in_fence:
                in_fence, fence = True, marker
            elif stripped.startswith(fence):
                in_fence = False
            continue
        if in_fence and "AddModule" in line:
            FAILS.append(
                f"{path}:{i}: markdown tells the user to add an AddModule line: "
                f"{line.strip()}"
            )


def main() -> int:
    for path in tracked_files():
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if "AddModule" not in text:
            continue
        suffix = path.suffix.lower()
        if suffix == ".sh":
            check_shell(path, text)
        elif suffix == ".py":
            check_python(path, text)
        elif suffix == ".md":
            check_markdown(path, text)
        elif suffix == ".conf" or "speech-dispatcher" in str(path):
            check_conf(path, text)
        else:
            # binaries and anything else: any mention outside a comment is suspect
            for i, line in enumerate(text.splitlines(), 1):
                if "AddModule" in line and not line.lstrip().startswith("#"):
                    FAILS.append(f"{path}:{i}: {line.strip()}")

    if FAILS:
        print("AddModule lines that would break autodiscovery:\n")
        for f in FAILS:
            print("  " + f)
        print(
            "\nSpeech Dispatcher discovers this pack's modules by directory. An\n"
            "explicit AddModule line disables that discovery and hides every other\n"
            "synthesizer the user has. Remove the line; do not reword it."
        )
        return 1

    print("no AddModule instructions: autodiscovery is not being disabled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
