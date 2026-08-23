# -*- coding: utf-8 -*-
"""Find the engine the user supplied.

This add-on ships no part of MacinTalk or outSPOKEN. The user extracts three
resources from their own copy -- `tools/extract_rom.py` does it from a disk
image -- and drops them somewhere we look.

**The ROM lives in NVDA's configuration folder, not the add-on folder.**
Updating an add-on deletes and recreates its directory, so a ROM kept inside it
would be silently destroyed on every upgrade. The add-on folder is still
searched, because someone will put it there anyway and it should work.

**Every Macintosh engine shares one folder**, `macintalk`, with a subfolder
per generation. The sibling panthera-speech add-ons keep Tiger's and Leopard's
trees beside these, so somebody running three of them has one place to look
rather than three loose folders with three naming conventions:

    macintalk/
        outspoken/      <- here
            macintalk1/     DRVR_1030.bin, TALK_1001.bin, RULZ_1129.bin
            macintalk2/     macintalk3/     macintalkpro/
            voices/<name>/
        tiger/
        leopard/

`migrate` moves `outspoken-roms` across, once, and the old location is
searched for good afterwards. Subfolders are searched recursively, so the
layout `tools/extract_rom.py` produces works unchanged, and a flat folder with
the files loose in it works just as well.
"""
import os

FILES = {
    "DRVR_1030.bin": "the 1984 MacinTalk engine, named '.sp'",
    "TALK_1001.bin": "MacinTalk Rules",
    "RULZ_1129.bin": "letter-to-sound rules (needed for English text)",
    "DICT_-4048.bin": "Berkeley's exception dictionary (optional)",
}

#: `RULZ` only drives the English front end. Without it the synthesiser still
#: speaks, but only if it is handed phonemes, which no screen reader does.
REQUIRED = ("DRVR_1030.bin", "TALK_1001.bin")

#: Every engine that lives in its own module, and the name a person sees.
#: MacinTalk 1 is not here because it is the `.sp` driver and has no module of
#: its own -- it is the three files in `REQUIRED` plus the rules.
#:
#: **Add to this when an engine is added, or the add-on will tell people they
#: have no engine while speaking to them in nineteen voices.**
ENGINE_MODULES = (
    ("macintalk2", "MacinTalk 2"),
    ("macintalk3", "MacinTalk 3"),
    ("macintalkpro", "MacinTalk Pro"),
)

CONFIG_DIRNAME = os.path.join("macintalk", "outspoken")

#: Where every release up to 0.9.0 kept it.
LEGACY_DIRNAME = "outspoken-roms"


def config_dir():
    """`<nvda user config>/macintalk/outspoken`.

    `globalVars.appArgs.configPath` is the only correct source. NVDA's own
    `NVDAState.WritePaths.configDir` is a property wrapping exactly this value,
    so it already accounts for a portable copy and for a config directory given
    on the command line with `-c`. Expanding `%APPDATA%` ourselves would be
    right on one machine and wrong on every portable one.

    The fallback exists for running outside NVDA -- the tests, and anything
    driven from a command line -- and for nothing else.
    """
    base = None
    try:
        import globalVars
        base = globalVars.appArgs.configPath
    except Exception:
        base = None
    if not base:
        base = os.path.join(os.path.expanduser("~"), ".nvda")
    return os.path.join(str(base), CONFIG_DIRNAME)


def config_base():
    """The directory the shared `macintalk` folder sits inside."""
    return os.path.dirname(os.path.dirname(config_dir()))


def legacy_dir():
    return os.path.join(config_base(), LEGACY_DIRNAME)


def pointer_file():
    """A text file naming the folder, for anyone keeping it elsewhere.

    The sibling panthera-speech add-ons have had this from the start,
    because a Leopard tree is 717 MB and people keep those on another
    drive. outSPOKEN's engine is 7 MB and nobody needed it -- until the
    folder moved, and a way to say "it is over there" became the thing
    that makes moving it safe.
    """
    return os.path.join(config_base(), LEGACY_DIRNAME + ".txt")


def migrate():
    """Move `outspoken-roms` under `macintalk`, once. -> path or None

    **A rename, never a copy.** Old and new both sit inside NVDA's
    configuration directory, so this is one volume and one metadata
    operation. It matters less here than for the sibling add-ons -- this
    folder is about 7 MB against Leopard's 717 -- but the rule is the
    same, and a rename cannot half-succeed.

    Lazy, called from `search_roots` rather than at import, so nothing
    happens while NVDA is starting. If the rename fails -- a file open, a
    permission -- nothing has changed and the old folder is still
    searched, which is why it stays in `search_roots` for good rather
    than for one release.
    """
    new = config_dir()
    old = legacy_dir()
    if os.path.isdir(new) or not os.path.isdir(old):
        return None
    try:
        os.makedirs(os.path.dirname(new), exist_ok=True)
        os.rename(old, new)
    except OSError:
        return None
    # **The breadcrumb is load-bearing, not a note to a human.** An
    # earlier release of this add-on looks only in `outspoken-roms`, so
    # somebody who rolls back would find nothing where they left it. The
    # pointer file is read by `search_roots` below, which means a rollback
    # to any release from this one onward still works -- and anyone who
    # opens the configuration folder wondering where it went can read it.
    try:
        if not os.path.exists(pointer_file()):
            with open(pointer_file(), "w", encoding="utf-8") as f:
                f.write(new)
    except OSError:
        pass
    return new


def search_roots():
    configured = os.environ.get("RETRO_TTS_OUTSPOKEN_ROMS")
    if configured:
        return [configured]
    migrate()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    addon = os.path.dirname(here)                    # ...\addon
    roots = [config_dir(), legacy_dir()]
    try:
        if os.path.isfile(pointer_file()):
            with open(pointer_file(), encoding="utf-8") as f:
                named = f.read().strip()
            if named:
                roots.append(named)
    except OSError:
        pass
    roots.append(os.path.join(addon, "rom"))
    # **Deduplicated, because this list is shown to people.** The pointer file
    # usually names the folder we would have looked in anyway, so the report in
    # the Tools menu listed `macintalk\outspoken` twice and read like a bug.
    # Order is preserved: the first mention of a folder is the one that decides
    # which copy of a file wins.
    seen, unique = set(), []
    for r in roots:
        key = os.path.normcase(os.path.abspath(r))
        if key not in seen:
            seen.add(key)
            unique.append(r)
    return unique


def find():
    """-> ({name: path}, [missing names]).

    Searches each root recursively, so the layout `tools/extract_rom.py`
    produces (`rom/macintalk1/DRVR_1030.bin`) works as well as a flat folder.
    """
    found = {}
    for root in search_roots():
        if not os.path.isdir(root):
            continue
        for dirpath, _dirs, names in os.walk(root):
            for n in names:
                if n in FILES and n not in found:
                    found[n] = os.path.join(dirpath, n)
    return found, [n for n in FILES if n not in found]


#: The dictionary only improves pronunciation; the engine speaks without it.
OPTIONAL = ("DICT_-4048.bin",)


def usable():
    """True when there is enough to speak English."""
    found, _ = find()
    return all(n in found for n in REQUIRED) and "RULZ_1129.bin" in found


def engines_present():
    """-> names of the engines there is enough on disk to run.

    The one place that answers "is there anything here at all". The global
    plugin used to answer it for itself, with a narrower test than the
    synthesizer's: it asked only for REQUIRED, while MacinTalk 1 also needs
    RULZ_1129.bin to turn text into phonemes. A user with the first two files
    and not the third therefore got no dialog *and* no synthesizer -- the two
    halves disagreeing produced silence from both.
    """
    out = []
    found, _missing = find()
    if all(n in found for n in REQUIRED) and "RULZ_1129.bin" in found:
        out.append("MacinTalk 1")
    # **MacinTalk 3 was missing from here for the whole of 0.8.0 and 0.9.0**,
    # having been added to the driver's catalogue and not to this list. Anyone
    # with only MacinTalk 3 extracted was told on every start-up that they had
    # no engine, while its nineteen voices worked perfectly -- which is the
    # failure the docstring above warns about, recurring in the same function
    # that warns about it. `ENGINE_MODULES` is now the one list, and a test
    # holds it against what the driver actually offers.
    for mod, label in ENGINE_MODULES:
        try:
            m = __import__(mod)
            if m.usable(search_roots()):
                out.append(label)
        except Exception:
            pass
    return out


def explain():
    """-> (anything_runnable, [lines]) -- describe(), plus the verdict.

    Written for a log someone else will read: every root that was searched,
    every file that was looked for, and what it concluded.
    """
    engines = engines_present()
    lines = ["searched:"]
    for r in search_roots():
        lines.append("  %s %s" % (r, "exists" if os.path.isdir(r)
                                  else "MISSING"))
    found, _missing = find()
    for n, what in FILES.items():
        lines.append("  %-16s %-8s (%s)"
                     % (n, "found" if n in found else "MISSING", what))
    lines.append("runnable engines: %s" % (", ".join(engines) or "none"))
    return bool(engines), lines


def describe():
    found, missing = find()
    lines = ["ROM folder: %s" % config_dir()]
    for n, what in FILES.items():
        lines.append("  %-16s %s   (%s)"
                     % (n, "found" if n in found else "MISSING", what))
    return "\n".join(lines)
