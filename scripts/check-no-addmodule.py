#!/usr/bin/env python3
"""Fail if this repo instructs anyone to add a Speech Dispatcher `AddModule` line.

One explicit `AddModule` line turns module autodiscovery OFF: afterwards only the
modules named in speechd.conf load, so every other synthesizer the user has
disappears and some retro voice becomes the system default. That is the reported
"jump scare". Autodiscovery already finds these modules by directory, so an
`AddModule` line is never needed.

A bare grep does not work. Legitimate mentions exist:

  * `install.sh` explains the hazard in a comment and prints a message about
    stripping a block left by an older version
  * this file, and the workflow that runs it, are *about* AddModule
  * markdown may discuss the hazard in prose

So this checks intent, not spelling:

  * shell         -- comments stripped; `say`/`warn` message strings exempt
  * python        -- docstrings and comments skipped; only live code counts
  * speechd .conf -- always a failure, this is the file that causes the breakage
  * markdown      -- only inside fenced code blocks, since that is what a user copies
  * yaml          -- only in `run:` payloads; a job/step `name:` is just a label

Files that exist to police this rule (SELF) and the workflow that invokes them
are skipped, or the check fails on itself.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

FAILS: list[str] = []

# Files whose mention of AddModule is the point of the file.
SELF = {
    "scripts/check-no-addmodule.py",
    ".github/workflows/checks.yml",
}

SAY_WARN = re.compile(r'\b(?:say|warn)\s+"')


def tracked_files() -> list[Path]:
    try:
        out = subprocess.run(
            ["git", "ls-files"], capture_output=True, text=True, check=True
        ).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        sys.exit("this check must run from inside the git repository")
    return [Path(p) for p in out.splitlines() if p.strip()]


def strip_python_strings(text: str) -> list[str]:
    """Blank out docstrings and comments so only live code is examined.

    Line count is preserved so reported line numbers stay correct.
    """
    out: list[str] = []
    quote: str | None = None
    for line in text.splitlines():
        if quote is not None:
            if quote in line:
                quote = None
            out.append("")
            continue
        stripped = line.lstrip()
        m = re.match(r'[rbfu]*("""|\'\'\')', stripped)
        if m:
            body = stripped[m.end():]
            if m.group(1) not in body:
                quote = m.group(1)
            out.append("")
            continue
        if stripped.startswith("#"):
            out.append("")
            continue
        out.append(line.split("#", 1)[0])
    return out


def check_shell(path: Path, text: str) -> None:
    for i, line in enumerate(text.splitlines(), 1):
        if "AddModule" not in line:
            continue
        stripped = line.lstrip()
        if stripped.startswith("#"):
            continue
        if SAY_WARN.search(line):
            continue
        FAILS.append(f"{path}:{i}: live shell code registers a module: {line.strip()}")


def check_python(path: Path, text: str) -> None:
    for i, line in enumerate(strip_python_strings(text), 1):
        if "AddModule" in line:
            FAILS.append(f"{path}:{i}: live python writes an AddModule line: {line.strip()}")


def check_conf(path: Path, text: str) -> None:
    for i, line in enumerate(text.splitlines(), 1):
        if "AddModule" in line and not line.lstrip().startswith("#"):
            FAILS.append(f"{path}:{i}: config registers a module: {line.strip()}")


def check_markdown(path: Path, text: str) -> None:
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


def check_yaml(path: Path, text: str) -> None:
    """Only `run:` payloads execute; a job or step `name:` is a label."""
    for i, line in enumerate(text.splitlines(), 1):
        if "AddModule" not in line:
            continue
        if re.match(r"\s*(?:-\s+)?name\s*:", line):
            continue  # a label, not an instruction
        stripped = line.lstrip()
        if stripped.startswith("#"):
            continue
        FAILS.append(f"{path}:{i}: workflow would run an AddModule line: {line.strip()}")


def main() -> int:
    for path in tracked_files():
        key = path.as_posix()
        if key in SELF or not path.is_file():
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
        elif suffix in {".yml", ".yaml"}:
            check_yaml(path, text)
        elif suffix == ".conf" or "speech-dispatcher" in key:
            check_conf(path, text)
        else:
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
