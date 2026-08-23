# -*- coding: utf-8 -*-
"""Trimming silence, and handing audio over while it is still being made.

**Named `ospaudio` and not `audio`, which is not a style choice.** NVDA ships
its own `source/audio` package and imports it long before this one exists, so
`import audio` from here returns NVDA's -- whatever `sys.path` says, because
`sys.modules` is consulted first. It shipped that way for one deploy and every
MacinTalk 3 and Pro utterance died with `module 'audio' has no attribute
'Stream'`: listed voices, selectable, silent. `tests/test_no_module_shadows_nvda.py`
now checks every module here against NVDA's own.

Shared by the engines that render a buffer at a time. The engine modules are
otherwise deliberately independent -- one per engine generation, so one
engine's quirk can never become another's problem -- but this is neither
engine-specific nor obvious, and it was about to be written a third time.

**Why streaming exists.** Measured on the same long text at rate 232:

    MacinTalk 1     219 ms for 21.26 s of audio    97x realtime
    MacinTalk 2     105 ms for 20.43 s           194x
    MacinTalk 3    1073 ms for 24.94 s            23x
    MacinTalk Pro  1532 ms for 26.45 s            17x

The last two spend the better part of a second, sometimes more, before a
single sample exists. Played only when complete, that is a second of silence
before a long sentence -- which is exactly what it sounded like. Handing each
piece over as it is rendered puts the first sound about 30 ms in instead.

MacinTalk 1 is not in this file's care: it renders a whole utterance inside
one CPU call with no callback loop, so there is no point at which to yield.
At 97x it is also the least in need.
"""

#: 8-bit unsigned silence, which every one of these engines clears its
#: buffers to.
SILENT = 0x80

#: How much silence to leave at the end of an utterance, and at the start.
#: About 54 ms and 10 ms at the engines' shared 22254 Hz. Cutting hard on a
#: sample clicks, which is the whole reason either is not zero.
KEEP, LEAD = 1200, 220


def tail_silence(pcm):
    """-> how many bytes at the end of `pcm` are silent."""
    n = i = len(pcm)
    while i > 0 and pcm[i - 1] == SILENT:
        i -= 1
    return n - i


def trim_head(pcm, lead=LEAD):
    """Drop the leading silence, leaving `lead` bytes of it."""
    n = len(pcm)
    start = 0
    while start < n and pcm[start] == SILENT:
        start += 1
    if start >= n:
        return b""
    return pcm[max(0, start - lead):]


def trim_tail(pcm, keep=KEEP):
    """Drop the trailing silence, leaving `keep` bytes of it."""
    n = len(pcm)
    end = n
    while end > 0 and pcm[end - 1] == SILENT:
        end -= 1
    if end == 0:
        return b""
    return pcm[:min(n, end + keep)]


def trim(pcm, keep=KEEP, lead=LEAD):
    """Drop the silence at both ends, leaving a little at each.

    **The leading silence is the one that is felt**, but the trailing one
    costs just as much in practice: it keeps the player busy after the words
    have stopped, which is long enough for a fast keystroke to land inside it
    and hear the previous utterance still going. MacinTalk 3 shipped without
    this for a day and left 390 ms of nothing on every single utterance.
    """
    if not pcm:
        return pcm
    out = trim_head(pcm, lead)
    return trim_tail(out, keep) if out else out


class Stream(object):
    """Hands rendered audio to a sink as it arrives, one piece behind.

    **One piece is always held back**, because trailing silence can only be
    recognised once it has stopped growing: a buffer that ends quiet may be
    the end of the utterance or a pause with more speech behind it, and
    nothing but time tells them apart. The held piece keeps *absorbing* while
    it is entirely silent, so a tail spanning several buffers is not shipped
    a piece at a time.

    Only the first piece out is head-trimmed and only the last is
    tail-trimmed, which is what makes the streamed audio identical to the
    whole-utterance render -- asserted per engine, because "nearly identical"
    would mean a click or a clipped word that only some phrases produce.

    The sink returns False to say it has lost interest -- a cancel, or NVDA
    shutting the driver down. `feed` then returns False and the caller should
    stop rendering rather than finish into a queue nobody will read.
    """

    def __init__(self, sink):
        self.sink = sink
        self.aborted = False
        self.fed = 0
        self._held = b""
        self._first = True

    def feed(self, piece):
        """Take one rendered piece. -> False if the sink has given up."""
        if self.aborted:
            return False
        self._held += piece
        keep = tail_silence(self._held)
        if keep >= len(self._held):
            return True                     # all quiet so far: hold it all
        out = self._held[:len(self._held) - keep]
        self._held = self._held[len(self._held) - keep:]
        if self._first:
            out = trim_head(out)
            self._first = False
        return self._emit(out)

    def finish(self, last=b""):
        """Emit what is held, trimmed as the end of an utterance.

        **Call this before draining the engine, never after.** What a drain
        produces is the engine settling once the words are done, and it must
        reach nobody: discarding it is what stops the next utterance
        inheriting this one's ending.
        """
        if self.aborted:
            return False
        self._held += last
        if self._first:
            # Nothing went out, so this is the whole utterance and the
            # ordinary two-ended trim applies exactly as it always did.
            held = trim(self._held)
        elif tail_silence(self._held) >= len(self._held):
            # Only the silence deliberately held back. Keep the usual pad
            # rather than trimming it away: `trim_tail` has no last sample to
            # count from here, and dropping it outright leaves the streamed
            # audio exactly KEEP bytes shorter than the whole.
            held = self._held[:KEEP]
        else:
            held = trim_tail(self._held)
        self._held = b""
        return self._emit(held)

    def _emit(self, out):
        if not out:
            return True
        if self.sink(out) is False:
            self.aborted = True
            return False
        self.fed += len(out)
        return True
