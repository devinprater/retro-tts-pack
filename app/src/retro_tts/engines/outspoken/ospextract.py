# -*- coding: utf-8 -*-
"""Pull the speech engines out of your own Macintosh disk image into `rom/`.

This project ships no part of any engine, so the add-on starts with an empty
`rom/` folder. This tool fills it from a disk image you already own -- the same
arrangement Apple-Eloquence-ELF uses: publish the recipe and the extractor,
never the bits.

Sources it understands:

  * an HFS disk image (`.hfv`, `.dsk`, `.img`) as used by Basilisk II / Mini vMac
  * a single file: outSPOKEN itself, a MacBinary `.bin`, or an already-extracted
    resource fork

What it looks for:

  * **outSPOKEN** (`cdev`, creator `BSDo`) -> `DRVR 1030` the 1984 MacinTalk
    engine, `TALK 1001`, `RULZ 1129`
  * **MacinTalk 2** (`Extensions/MacinTalk 2`) -> its front end, back end,
    rules, dictionary and phoneme tables
  * **MacinTalk 2 voices** (`Extensions/Voices/*`) -> one folder each
  * **MacinTalk Pro** (`Extensions/MacinTalk Pro`) -> its engine, its tables
    and its 573 KB data-fork lexicon
  * **MacinTalk Pro voices** -> one folder each, including the `gtss` unit
    database and the per-voice code

  * **MacinTalk 3** (`Extensions/MacinTalk 3`) -> the 1994 68k engine, whose
    code hides in `ttvi` resources named after composers
  * **MacinTalk 3 voices** -> one folder each; most are a parameter set and
    nothing else, and nine carry wave data as well

MacinTalk 3 was refused by name until 2026-08-21, on the grounds that a native
add-on already builds that engine and emulating it would be strictly worse.
That changed when the 68k build spoke: running the real 1994 code says how the
engine behaved *then*, which no native port can, and it brings the nineteen
voices -- Bells, Boing, Cellos, Pipe Organ, Zarvox and the rest -- that the
formant voices alone do not.

MacinTalk Pro is the same kind of component as MacinTalk 2 and MacinTalk 3 --
same 'ttsc' type, same standard component entry -- so the host glue is shared.

    py -3 tools/extract_rom.py "C:/path/to/MacOS7.hfv"
    py -3 tools/extract_rom.py "C:/path/to/outSPOKEN" --out rom
    py -3 tools/extract_rom.py <image> --list
"""
import mmap
import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsrc                                                   # noqa: E402
import voices as voicelib                                     # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _print(msg):
    """The default voice of this module: standard output, for the CLI.

    Everything that used to `print` now takes a `say`, so one copy of the logic
    serves the command line and the dialog.  A callback rather than a captured
    stream, because the dialog wants each line as it happens -- reading a
    gigabyte image with nineteen voices in it is not something to report only
    once it is over.
    """
    print(msg)


def nvda_roms():
    """-> `<NVDA user config>/macintalk/outspoken`, or None if NVDA is absent.

    **This is where the add-on actually reads the engine from**, never its own
    folder: updating an add-on deletes and recreates that directory, so a ROM
    kept inside it would be destroyed on every upgrade.

    Extracting into `./rom` and expecting NVDA to notice is the mistake this
    exists to prevent, and it is not hypothetical -- it is what left three
    MacinTalk Pro voice folders of one file each on the author's own machine
    while a complete extraction sat in the repository.

    Refuses to guess. If NVDA's configuration directory is not where NVDA puts
    it, the user is told rather than handed a folder nothing will read.
    """
    base = os.environ.get("APPDATA")
    cands = []
    if base:
        cands.append(os.path.join(base, "nvda"))
    cands.append(os.path.join(os.path.expanduser("~"), ".nvda"))
    for c in cands:
        if os.path.isdir(c):
            return os.path.join(c, "macintalk", "outspoken")
    return None

# (folder, [(type, id-or-None, description)]).  id None means "every one".
#: Resources to leave behind even though the spec asks for their whole type,
#: keyed by output folder. Only MacinTalk 3 needs one, and it needs it badly:
#: `ttvi 11` is the PowerPC build of the engine living beside the 68k build in
#: the same file.
SKIP_RESOURCES = {"macintalk3": (("ttvi", 11),)}

WANTED = {
    "macintalk1": [("DRVR", 1030, "the 1984 engine, named '.sp'"),
                   ("TALK", 1001, "MacinTalk Rules"),
                   ("RULZ", 1129, "letter-to-sound rules"),
                   ("DICT", None, "Berkeley's exception dictionaries"),
                   ("PHNM", None, "phoneme table")],
    "macintalk2": [("Cecy", 1, "back end -- the synthesiser"),
                   ("Cecy", 3, "front end -- text to phonemes"),
                   ("ttsr", None, "pronunciation rules"),
                   ("ttsd", None, "dictionary"),
                   ("ttss", None, "phoneme symbols"),
                   ("ttph", None, "magic char map"),
                   ("ttop", None, "magic opcode map"),
                   ("thng", None, "component descriptors")],
    "voice":      [("ttvi", None, "voice info"),
                   ("ttvd", None, "voice description"),
                   ("ttvw", None, "voice data")],
    # MacinTalk 3 keeps its CODE in a `ttvi`, because Apple named these
    # resources after composers: `ttvi 10` is "Bach" and is the component
    # entry, `ttvi 8` and `9` are more of the engine, and `ttvi 3-7` are its
    # data. The type that means "voice info" for every other engine here means
    # "the engine" for this one.
    #
    # `ttvi 11` is "Mozart", the **PowerPC** build of the same engine, and is
    # excluded: 102 KB of code this host cannot run.
    "macintalk3": [("ttvi", None, "the engine (Bach is ttvi 10) and its data"),
                   ("ttss", None, "phoneme symbols"),
                   ("ttsp", None, "parameters"),
                   ("STR ", None, "strings"),
                   ("thng", None, "component descriptor"),
                   ("vers", None, "version")],
    # A MacinTalk 3 voice is usually a parameter set and nothing else -- Fred
    # is a 714-byte ttvd -- because the engine is formant and the formant data
    # lives in the engine. The nine singing and novelty voices also carry a
    # `ttvw`, and the engine refuses them with -192 without it.
    "mt3voice":   [("ttvd", None, "voice description and parameters"),
                   ("ttvw", None, "wave data, only the novelty voices"),
                   ("vers", None, "version")],
    # MacinTalk Pro is a 'ttsc' component like MacinTalk 2, so the same host
    # glue serves it; see docs/macintalk2-components.md.  Its code is gtse 1
    # and everything else here is table data.
    "macintalkpro": [("gtse", None, "the engine (gtse 1) and its tables"),
                     ("gtst", None, "text tables"),
                     ("gtsa", None, "phoneme alphabets"),
                     ("gtsp", None, "phoneme sets"),
                     ("gtsg", None, "configuration"),
                     ("gtsm", None, "module map"),
                     ("gtsi", None, "index"),
                     ("gala", None, "copyright"),
                     ("thng", None, "component descriptor"),
                     # Not decoration.  Pro's Open asks for its own `vers 1`
                     # (gtse 1 +$6E8) and returns synthOpenFailed without it.
                     ("vers", None, "version")],
    # A Pro voice is nothing like a MacinTalk 2 one.  Alongside the standard
    # ttvd it carries a 128-byte gtsv record, and `gtss` holds both the
    # concatenative unit database (789-922 KB) and about 11 KB of per-voice
    # *code*.  That is why asking only for ttvi/ttvd/ttvw returned 362 bytes
    # out of an 870 KB file.
    "provoice":   [("ttvd", None, "voice description"),
                   ("gtsv", None, "voice record"),
                   ("gtss", None, "unit database and per-voice code"),
                   ("gtsg", None, "configuration"),
                   ("gtsi", None, "index"),
                   ("gtsm", None, "wave map"),
                   ("vers", None, "version")],
}


def _safe(s):
    return "".join(c if c.isalnum() or c in " ._-" else "_" for c in s).strip()


def open_image(path):
    """-> {posix path: (data, rsrc, creator)} for an HFS image, else None.

    **Memory-mapped, not read into memory.**  `machfs` wants the whole volume
    as one buffer, and Tomi's own Basilisk II image is a gigabyte: pulling that
    into NVDA's process to fetch a 40 KB resource would be indefensible, and
    the slice `machfs` takes when a volume does not begin at offset 0 would
    make it two gigabytes.  A memory map satisfies every access it makes.
    Measured: that gigabyte mounts in 0.07 seconds.

    `_machfs` is vendored rather than depended on, and that is the whole reason
    this can be a dialog at all -- an NVDA add-on cannot run `pip install`, and
    needing one is what kept extraction on the command line.  MIT, 1236 lines,
    identical to upstream but for one import line in each of two files; see
    `_machfs/_macresources.py`.
    """
    import _machfs as machfs
    try:
        fh = open(path, "rb")
    except OSError:
        return None
    try:
        with fh:
            mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
            try:
                v = machfs.Volume()
                v.read(mm)
                out = {}
                for parts, o in v.iter_paths():
                    if isinstance(o, machfs.Folder):
                        continue
                    out["/".join(parts)] = (
                        o.data, o.rsrc,
                        getattr(o, "creator", b"").decode("mac-roman",
                                                          "replace"))
                return out
            finally:
                mm.close()
    except Exception:
        return None


#: Where a folder's resource names and map offsets are recorded.  Written
#: beside the `.bin`s rather than encoded into their file names, because a Mac
#: resource name is not a file name: MacinTalk Pro's modules are called `*TTS`,
#: `*Wave`, `*Lex` and so on, and `*` is illegal in a Windows path.
#: Sanitising would silently lose the thing the engine looks resources up BY.
#:
#: Four columns: type, id, map entry, name.  The map entry is what
#: `RsrcMapEntry` answers -- see rsrc.Resource -- and the name comes last
#: because it is the only field that could contain anything surprising.
INDEX_FILE = "resources.tsv"

#: What that file used to be called, when it held names alone.  Removed on
#: sight so a half-updated `rom/` cannot leave a reader matching on the older
#: three-column shape.
OLD_INDEX_FILE = "names.tsv"

#: The whole resource fork, kept for the engines that read their own file
#: rather than asking the Resource Manager for handles.  See rsrc.fork_bytes.
FORK_FILE = "rsrcfork.bin"


def take(fork, spec, outdir, label, skip=(), optional=()):
    """Write the resources `spec` asks for. -> (written, missing).

    `skip` is (type, id) pairs to leave behind even when the spec asks for the
    whole type. It exists for one case: MacinTalk 3's `ttvi 11` is the PowerPC
    build of the same engine sitting beside the 68k one, and asking for every
    `ttvi` would otherwise drag 102 KB of code this host cannot run.

    `optional` is types whose absence is not worth reporting. Ten of MacinTalk
    3's nineteen voices have no `ttvw` and are complete without one, so saying
    they are missing it would train the reader to ignore the word.
    """
    try:
        rs = rsrc.parse(fork)
    except Exception as e:
        print("  ! %s: cannot read resource fork (%s)" % (label, e))
        return 0, [d for _, _, d in spec]
    by = {}
    for r in rs:
        by.setdefault(r.type, []).append(r)
    os.makedirs(outdir, exist_ok=True)
    n, missing, names = 0, [], []
    for rtype, rid, desc in spec:
        got = [r for r in by.get(rtype, [])
               if (rid is None or r.id == rid) and (r.type, r.id) not in skip]
        if not got:
            if rtype not in optional:
                missing.append("%s %s (%s)" % (rtype, rid if rid else "*", desc))
            continue
        for r in got:
            name = "%s_%d.bin" % (_safe(r.type).strip() or "res", r.id)
            open(os.path.join(outdir, name), "wb").write(r.data)
            names.append((r.type, r.id, r.map_entry, r.name))
            n += 1
    # **MacinTalk Pro looks its pieces up by name, not by id**, so throwing
    # these away made the engine unusable however complete the extraction was.
    # Its own code is `gtse 1` named `*TTS`, and a voice's 789 KB unit database
    # is `EnglMBruceData`; `Get1NamedResource` is how it finds either.
    #
    # The map entry goes with it because Pro also asks `RsrcMapEntry` where a
    # resource sits in the map, and then reads the data out of the file itself.
    stale = os.path.join(outdir, OLD_INDEX_FILE)
    if os.path.exists(stale):
        os.remove(stale)
    if names:
        with open(os.path.join(outdir, INDEX_FILE), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write("# type\tid\tmapentry\tname\n")
            for rtype, rid, entry, nm in sorted(names,
                                                key=lambda x: (x[0], x[1])):
                fh.write("%s\t%d\t%d\t%s\n" % (rtype, rid, entry, nm))
    return n, missing


#: MacinTalk 1 is not a voice-folder engine -- its two voices are a pitch
#: setting, not files -- so it is checked by its own resources rather than
#: through `voices.installed`.
#:
#: **This must match what the driver requires**, which is `rom.REQUIRED` plus
#: `RULZ_1129.bin`: without the letter-to-sound rules the engine takes phonemes
#: only, and no screen reader sends those. `_catalogue` in outspoken.py is the
#: authority and a test holds the two together.
MT1_REQUIRED = ("DRVR_1030.bin", "TALK_1001.bin", "RULZ_1129.bin")


def installed_engines(out):
    """What this folder can speak with. -> ({engine: [voices]}, [(folder, why)])

    **Split out of `report_ready` so the dialog and the terminal answer the
    same question with the same code.**  Writing files is not the same as being
    able to speak, and the difference used to surface only after restarting
    NVDA: an engine without its voices, voices without their engine, or voice
    folders extracted before this project could drive the engine at all, which
    hold a `ttvd` and nothing else.
    """
    ok, bad = voicelib.installed(roots=[out], speakable=True)
    have = {}
    for v in ok:
        have.setdefault(v.engine, []).append(v.name)

    # Per directory, not accumulated across the tree. Names repeat by design --
    # every MacinTalk Pro voice folder has its own `rsrcfork.bin` -- so summing
    # them up declares an engine present that was assembled out of pieces of
    # two. That is a bug this project has already had once, in
    # `voices.engine_installed`.
    want = set(MT1_REQUIRED)
    if any(want <= set(names) for _dp, _dirs, names in os.walk(out)):
        have["MacinTalk 1"] = ["Male", "Female"]
    return have, bad


#: Every engine this add-on can drive, in the order the manager lists them.
#:
#: Named here rather than derived from what happens to be installed, because a
#: manager that lists only what you already have cannot tell you what you are
#: missing -- which is the question somebody opens it to ask.  Panthera's lists
#: Tiger, Leopard and Lion the same way and for the same reason.
#: name, one line about it, the folder it is written to, and where it comes
#: from.  The last is the one a user actually needs: "not installed" is only
#: half an answer without "and here is what to point me at".
ENGINES = (
    ("MacinTalk 1", "1984, the original -- Male and Female", "macintalk1",
     "outSPOKEN itself -- the control panel, creator 'BSDo'.  Also the "
     "outSPOKEN.bin on Macintosh Repository, which is 148 KB and carries the "
     "whole engine."),
    ("MacinTalk 2", "1993 -- ten voices", "macintalk2",
     "Extensions/MacinTalk 2, on a System 7 disk or disk image, with its "
     "voices in Extensions/Voices."),
    ("MacinTalk 3", "1994 -- nineteen, the singing ones among them",
     "macintalk3",
     "Extensions/MacinTalk 3, and Extensions/Voices for the nineteen.  The "
     "engine's own code hides in resources named after composers."),
    ("MacinTalk Pro", "1994 -- Agnes, Bruce and Victoria", "macintalkpro",
     "Extensions/MacinTalk Pro, and Extensions/Voices.  Each Pro voice is "
     "most of a megabyte of recorded units."),
)


def engine_details(out, name):
    """Everything the manager can say about one engine. -> [line]

    The same judgement `installed_engines` makes, spelled out for a reader:
    where it goes, where it comes from, what is there and what is present but
    not offered.  That last case is the one worth having -- a folder holding a
    `ttvd` and nothing else looks installed from the outside and cannot speak.
    """
    row = next((e for e in ENGINES if e[0] == name), None)
    if row is None:
        return []
    _n, about, sub, source = row
    have, bad = installed_engines(out) if out else ({}, [])
    voices = sorted(have.get(name, []))

    lines = [about, ""]
    if voices:
        lines.append("Installed, %d voice%s:"
                     % (len(voices), "" if len(voices) == 1 else "s"))
        lines.append("    " + ", ".join(voices))
    else:
        lines.append("Not installed yet.")
    lines += ["", "It goes in:", "    %s" % os.path.join(out or "?", sub),
              "", "It comes from:"]
    #: Wrapped here rather than by the control.  The box is TE_DONTWRAP, to
    #: match Panthera's and because a folder path folded in the middle is
    #: unreadable -- but that leaves a sentence scrolling sideways, which for
    #: somebody arrowing through it line by line is worse.
    lines += ["    " + ln for ln in textwrap.wrap(source, 62)]

    #: Only this engine's rejects.  `voices.installed` reports them by folder,
    #: and a folder under `voices/` does not say which engine it belongs to --
    #: so match on the reason naming the engine, and fall back to showing them
    #: all rather than silently dropping a warning the user needs.
    mine = [(f, why) for f, why in bad if name.lower() in why.lower()]
    for folder, why in (mine or ([] if voices else bad)):
        if not lines[-1] == "":
            lines.append("")
        lines.append("Present but not offered:")
        lines.append("    %s -- %s" % (os.path.basename(folder), why))
    return lines


def report_ready(out):
    """Say what the add-on will actually OFFER from this folder.

    Writing files is not the same as being able to speak, and until now the
    difference only showed up after restarting NVDA. Two ordinary states get
    it wrong:

    * an engine extracted without its voices, or voices without their engine
      -- the extractor pulls MacinTalk Pro's Agnes, Bruce and Victoria out of
      any image that carries them, whether or not it also had Pro itself;
    * **voices extracted before this project could drive the engine**, leaving
      a folder holding a `ttvd` and nothing else. That is what was on the
      author's own machine on 2026-08-20, and adding the engine afterwards
      made those stubs look ready. See `voices.VOICE_PARTS`.

    So this reports the same judgement the add-on will make, from the same
    function, rather than a count of files written.
    """
    have, bad = installed_engines(out)
    print("\n  NVDA will offer, from %s:" % out)
    if not have:
        print("    nothing yet -- see below")
    for engine in sorted(have):
        names = sorted(have[engine])
        print("    %-14s %2d voice%s  %s"
              % (engine, len(names), " " if len(names) == 1 else "s",
                 ", ".join(names)))

    if bad:
        # Group by reason: twenty MacinTalk 3 voices with the same explanation
        # is one line, not twenty.
        why = {}
        for folder, reason in bad:
            why.setdefault(reason, []).append(folder)
        print("\n  Present but NOT offered, and why:")
        for reason in sorted(why):
            who = sorted(why[reason])
            shown = ", ".join(who[:3])
            if len(who) > 3:
                shown += " and %d more" % (len(who) - 3)
            print("    %-34s %s" % (shown[:34], reason))
        if any("incomplete" in r for r in why):
            print("\n    An incomplete voice is one extracted before this "
                  "project could drive its\n    engine. Re-run this tool "
                  "against your image to complete it -- nothing is\n"
                  "    lost, the missing pieces are simply added.")




# ---- the same work, without a command line ------------------------------
#
# **All of this used to be a script and nothing else**, so extracting an
# engine needed Python, a terminal and `pip install machfs`.  For somebody who
# wants to hear a 1984 speech synthesiser through their screen reader that is
# not a small ask, and Panthera answered the same problem for Mac OS X with a
# dialog.  This is that shape for the classic engines.
#
# The logic below is the script's own, moved rather than rewritten -- `git log
# --follow` on this file reaches the command-line tool -- with `print` replaced
# by a `say` callback so one copy serves the terminal and the dialog.


def plan(files, say=_print):
    """Work out what is in the image. -> (jobs, skipped)

    A job is (path, fork, spec, subfolder, datafork).  Which engine a file
    belongs to is decided by its name and, for voices, by the resource types it
    carries -- never by a list of expected contents, so an image nobody has
    seen before is still read on its own terms.
    """
    jobs, skipped = [], []
    for path, (data, res, creator) in sorted(files.items()):
        base = path.split("/")[-1]
        low = base.lower()
        fork = res or data
        if not fork:
            continue
        if low.startswith("outspoken") and "prefs" not in low:
            # outSPOKEN 8 (creator 'oSM ') dropped the bundled 1984 engine and
            # speaks through the Speech Manager instead, so it has nothing for
            # us. Only the earlier product (creator 'BSDo') carries '.sp'.
            if creator.strip() and creator.strip() != "BSDo":
                skipped.append((path, "outSPOKEN %s -- no bundled engine; "
                                      "it uses the Speech Manager" % creator))
                continue
            jobs.append((path, fork, WANTED["macintalk1"], "macintalk1", b""))
        elif base == "MacinTalk 2" or low.startswith("macintalk 2."):
            jobs.append((path, fork, WANTED["macintalk2"], "macintalk2", b""))
        elif base == "MacinTalk 3" or low.startswith("macintalk 3."):
            jobs.append((path, fork, WANTED["macintalk3"], "macintalk3", b""))
        elif base == "MacinTalk Pro" or low.startswith("macintalk pro"):
            # Pro is the only one of these with a data fork that matters: 573 KB
            # of lexicon, which no resource type covers.  Both halves are needed
            # and only one of them is a resource fork, so carry it separately.
            jobs.append((path, res or data, WANTED["macintalkpro"],
                         "macintalkpro", data if res else b""))
        elif "/Voices/" in path or path.startswith("Voices/"):
            # Which engine a voice belongs to is written in its resource types,
            # not its name. A MacinTalk 2 voice carries ttvi + ttvd + ttvw; the
            # MacinTalk 3 voices are parameter sets with no ttvi, and the Pro
            # voices carry only a ttvd beside a very large data fork.
            try:
                kinds = {r.type for r in rsrc.parse(fork)}
            except Exception:
                kinds = set()
            nm = base.replace(".rsrc", "")
            if {"ttvi", "ttvd", "ttvw"} <= kinds:
                jobs.append((path, fork, WANTED["voice"],
                             "voices/" + _safe(nm), b""))
            elif "gtss" in kinds:
                jobs.append((path, fork, WANTED["provoice"],
                             "voices/" + _safe(nm), b""))
            elif "ttvd" in kinds:
                # MacinTalk 3. Refused by name until 2026-08-21, when the 68k
                # engine spoke under the emulator and Tomi heard it. Ten of
                # its nineteen voices are a `ttvd` and nothing else; the nine
                # singing and novelty ones carry a `ttvw` too.
                jobs.append((path, fork, WANTED["mt3voice"],
                             "voices/" + _safe(nm), b""))

    return jobs, skipped


def run(jobs, out, say=_print, listing=False, progress=None):
    """Write what every job asks for, under `out`. -> resources written

    `progress` is called with (done, total).  An image with nineteen voices in
    it takes long enough that a dialog needs to say where it has got to.
    """
    total = 0
    for done, (path, fork, spec, sub, datafork) in enumerate(jobs, 1):
        outdir = os.path.join(out, sub)
        if listing:
            try:
                rs = rsrc.parse(fork)
                kinds = sorted({r.type for r in rs})
            except Exception:
                kinds = ["<unreadable>"]
            say("  %-52s %s" % (path[:52], " ".join(kinds)))
            continue
        # Ten of MacinTalk 3's nineteen voices have no wave data and are
        # complete without it, so its absence is not a shortfall to report.
        optional = ("ttvw",) if spec is WANTED["mt3voice"] else ()
        n, missing = take(fork, spec, outdir, path,
                          skip=SKIP_RESOURCES.get(sub, ()),
                          optional=optional)
        # MacinTalk Pro walks its own resource map and seeks to byte offsets in
        # the file, so parsing the resources out is not enough -- it needs the
        # fork itself. Only Pro does this; the older engines ask the Resource
        # Manager for handles and never see a file.
        if spec is WANTED["macintalkpro"] or spec is WANTED["provoice"]:
            try:
                os.makedirs(outdir, exist_ok=True)
                with open(os.path.join(outdir, FORK_FILE), "wb") as fh:
                    fh.write(rsrc.fork_bytes(fork))
                n += 1
            except Exception as e:
                say("  ! %s: cannot save the resource fork (%s)" % (path, e))
        if datafork:
            os.makedirs(outdir, exist_ok=True)
            with open(os.path.join(outdir, "datafork.bin"), "wb") as fh:
                fh.write(datafork)
            n += 1
        total += n
        flag = "" if not missing else "   MISSING: " + "; ".join(missing)
        say("  %-52s %2d resources -> %s%s"
              % (path[:52], n, os.path.relpath(outdir, ROOT), flag))

        if progress:
            progress(done, len(jobs))
    return total


def extract(source, out, say=_print, listing=False, progress=None):
    """Read `source` and fill `out`. -> (written, skipped), or None if nothing
    in it was recognised.

    The one entry point worth calling from anywhere else.  `source` is an HFS
    image or a single Mac file -- outSPOKEN itself, a MacBinary `.bin` off
    Macintosh Repository, or a bare resource fork -- and which it is is settled
    by trying to mount it rather than by looking at the extension.
    """
    files = open_image(source)
    if files is None:
        raw = open(source, "rb").read()
        files = {os.path.basename(source): (b"", raw, "")}
        say("reading a single file: %s\n" % os.path.basename(source))
    else:
        say("mounted %s -- %d files\n"
            % (os.path.basename(source), len(files)))
    jobs, skipped = plan(files, say)
    if not jobs:
        return None
    return run(jobs, out, say, listing, progress), skipped
