# -*- coding: utf-8 -*-
"""Point it at a Macintosh disk image and it installs the engines.

**Extracting an engine used to need Python, a terminal and `pip install
machfs`.** For somebody who wants to hear a 1984 speech synthesiser through
their screen reader, that is not a small ask, and it is the single thing most
likely to stop them. Panthera answered the same problem for Mac OS X with a
dialog in the Tools menu; this is that shape for the classic engines.

**Deliberately a second Tools menu item, not a merged one.** Tomi's call, and
it is the right one: these are classic Mac OS engines from a different
repository with a different lineage, and one dialog trying to be both would
serve neither. What is shared is the *shape* -- open it and you see the engines
themselves, one row each, saying what is installed and what is not. Panthera
lists Tiger, Leopard and Lion that way; this lists MacinTalk 1, 2, 3 and Pro.

The engine list comes from `ospextract.ENGINES` rather than from what happens
to be installed, because a manager that shows only what you already have
cannot answer the question you opened it to ask.

## What it will read

* an **HFS disk image** -- `.hfv`, `.dsk`, `.img` -- as Basilisk II and Mini
  vMac use, memory-mapped rather than read, so a gigabyte image costs nothing
* a **single Mac file**: outSPOKEN itself, or a MacBinary `.bin`

That last one matters more than it looks. HFV images are hard to come by and
most people will never find Datajake's mirror; `outSPOKEN.bin` on Macintosh
Repository is 148 KB and one click, and it carries the whole 1984 engine.

## Threading

Everything slow happens on a worker thread and every UI touch goes back
through `wx.CallAfter`. Reading an image is file reads and resource parsing,
both of which let go of the GIL often enough that the dialog stays responsive
and NVDA keeps speaking -- which for a dialog a blind user is driving is not a
nicety.
"""
import os
import sys
import threading

import wx

#: `gui`, `ui` and `logHandler` exist only inside NVDA, and every module this
#: add-on ships has to import on its own -- `test_no_module_shadows_nvda.py`
#: walks the folder and imports each one, because a flat layout on a shared
#: `sys.path` is exactly the arrangement the `audio` collision exploited.
#:
#: `wx` is not guarded: this file defines a `wx.Dialog` subclass, so it is
#: needed at class-definition time and there is nothing to fall back to.  The
#: suite provides one.
try:
    import gui
except ImportError:                                            # tests
    gui = None
try:
    import ui
except ImportError:                                            # tests
    ui = None
try:
    from logHandler import log
except ImportError:                                            # tests
    import logging
    log = logging.getLogger(__name__)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ospextract                                              # noqa: E402

#: Announce progress every this many percent, not every file.
#:
#: A voice folder is a handful of resources and an image can hold thirty of
#: them, so announcing each one turns the extraction into a wall of speech the
#: user cannot interrupt or follow. Panthera's manager settled on the same
#: number for the same reason.
ANNOUNCE_EVERY = 25

#: Everything the file picker will offer, and the order it offers it in.
WILDCARD = ("Macintosh disk images and files"
            "|*.hfv;*.dsk;*.img;*.dmg;*.bin;*.sit;*.image"
            "|Disk images (*.hfv;*.dsk;*.img)|*.hfv;*.dsk;*.img"
            "|MacBinary and single files (*.bin)|*.bin"
            "|All files|*.*")


class SpeechDataDialog(wx.Dialog):
    """One at a time, and it comes forward if it is already open."""

    _instance = None

    @classmethod
    def show(cls, parent):
        if cls._instance is not None:
            try:
                cls._instance.Raise()
                return
            except RuntimeError:
                cls._instance = None
        dialog = cls(parent)
        cls._instance = dialog
        dialog.Show()

    def __init__(self, parent):
        # Translators: the title of the classic Macintosh speech data dialog.
        super(SpeechDataDialog, self).__init__(parent,
                                               title=_("Macintosh speech data"))
        self._busy = False
        self._lastAnnounced = -1

        main = wx.BoxSizer(wx.VERTICAL)
        pad = wx.Panel(self)
        inner = wx.BoxSizer(wx.VERTICAL)

        # Translators: labels the list of speech engines.
        inner.Add(wx.StaticText(pad, label=_("&Engines:")),
                  flag=wx.BOTTOM, border=4)
        #: A read-only list rather than a grid: every row is one engine and one
        #: sentence about it, which is what a screen reader can say in one go.
        self._list = wx.ListBox(pad, style=wx.LB_SINGLE,
                                size=(560, 110))
        self._list.Bind(wx.EVT_LISTBOX, self.onSelect)
        inner.Add(self._list, flag=wx.EXPAND | wx.BOTTOM, border=8)

        #: **A read-only box saying what is where, per engine.**
        #:
        #: Panthera's manager has one and this did not, which is a worse gap
        #: than it sounds: a row reading "not installed" is half an answer
        #: without "and here is what to point me at".  It also has room for
        #: the case a one-line row cannot show at all -- a voice folder that
        #: is present and still not offered, which looks installed from the
        #: outside and cannot speak.
        #:
        #: Read-only rather than a label, so a screen reader can be arrowed
        #: through it line by line and the text can be selected and copied
        #: into a bug report.
        # Translators: label for the box describing the selected engine.
        inner.Add(wx.StaticText(pad, label=_("&Details:")),
                  flag=wx.BOTTOM, border=4)
        self._details = wx.TextCtrl(
            pad, size=(560, 150),
            style=wx.TE_MULTILINE | wx.TE_READONLY | wx.TE_DONTWRAP)
        inner.Add(self._details, flag=wx.EXPAND | wx.BOTTOM, border=8)

        self._where = wx.StaticText(pad, label="")
        inner.Add(self._where, flag=wx.BOTTOM, border=8)

        row = wx.BoxSizer(wx.HORIZONTAL)
        # Translators: a button that opens a file picker for a disk image.
        self._pick = wx.Button(pad, label=_("&Install from a disk image or "
                                            "file..."))
        self._pick.Bind(wx.EVT_BUTTON, self.onPick)
        row.Add(self._pick, flag=wx.RIGHT, border=8)
        # Translators: a button that opens the folder the engines live in.
        self._open = wx.Button(pad, label=_("Open the &folder"))
        self._open.Bind(wx.EVT_BUTTON, self.onOpenFolder)
        row.Add(self._open, flag=wx.RIGHT, border=8)
        # Translators: closes the dialog.
        close = wx.Button(pad, wx.ID_CLOSE, label=_("&Close"))
        close.Bind(wx.EVT_BUTTON, lambda evt: self.Close())
        row.Add(close)
        inner.Add(row)

        pad.SetSizer(inner)
        main.Add(pad, flag=wx.ALL | wx.EXPAND, border=12)
        self.SetSizerAndFit(main)
        self.Bind(wx.EVT_CLOSE, self.onClose)

        self.refresh()
        self._pick.SetFocus()

    # -- what is installed ------------------------------------------------

    def folder(self):
        """-> where the add-on actually reads engines from, or None."""
        return ospextract.nvda_roms()

    def refresh(self):
        """Rebuild the list from what is on disk right now."""
        where = self.folder()
        try:
            have = ospextract.installed_engines(where)[0] if where else {}
        except Exception:
            log.error("outSPOKEN: cannot read the engine folder", exc_info=True)
            have = {}
        sel = max(self._list.GetSelection(), 0)
        self._list.Clear()
        for name, about, _sub, _source in ospextract.ENGINES:
            voices = sorted(have.get(name, []))
            if voices:
                # Translators: {n} voices are installed for an engine.
                state = _("installed, %d voice%s") % (
                    len(voices), "" if len(voices) == 1 else "s")
            else:
                # Translators: an engine with no speech data yet.
                state = _("not installed")
            self._list.Append("%s -- %s -- %s" % (name, state, about))
        if self._list.GetCount():
            self._list.SetSelection(min(sel, self._list.GetCount() - 1))
        self._where.SetLabel(
            # Translators: names the folder the engines are read from.
            _("They are read from: %s") % (where or _("NVDA's folder was not "
                                                      "found")))
        self.showDetails()

    def onSelect(self, evt):
        self.showDetails()

    def showDetails(self):
        """Fill the details box for whichever engine is selected."""
        index = self._list.GetSelection()
        if index < 0 or index >= len(ospextract.ENGINES):
            self._details.SetValue("")
            return
        try:
            lines = ospextract.engine_details(self.folder(),
                                              ospextract.ENGINES[index][0])
        except Exception:
            log.error("outSPOKEN: could not describe the engine",
                      exc_info=True)
            lines = []
        self._details.SetValue("\n".join(lines))

    # -- installing -------------------------------------------------------

    def onPick(self, evt):
        if self._busy:
            return
        where = self.folder()
        if not where:
            gui.messageBox(
                # Translators: shown when NVDA's own config folder is missing.
                _("NVDA's configuration folder could not be found, so there is "
                  "nowhere to install to."),
                _("Macintosh speech data"), wx.OK | wx.ICON_ERROR)
            return
        with wx.FileDialog(
                self,
                # Translators: the file picker's title.
                _("Choose a Macintosh disk image, or outSPOKEN itself"),
                wildcard=WILDCARD,
                style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as picker:
            if picker.ShowModal() != wx.ID_OK:
                return
            source = picker.GetPath()

        #: Ask before writing over data that is already there.  The extractor
        #: overwrites file by file, so a half-finished second run leaves a
        #: folder holding pieces of two images -- which `installed_engines`
        #: would then report as an engine that cannot actually speak.
        try:
            existing = bool(ospextract.installed_engines(where)[0])
        except Exception:
            existing = False
        if existing:
            answer = gui.messageBox(
                # Translators: asked before overwriting installed speech data.
                _("There is already speech data installed.\n\n"
                  "Installing from this image will write over it. Continue?"),
                _("Macintosh speech data"),
                wx.YES | wx.NO | wx.NO_DEFAULT | wx.ICON_WARNING)
            if answer != wx.YES:
                return

        self._busy = True
        self._pick.Disable()
        self._lastAnnounced = -1
        self._say(_("Reading %s.") % os.path.basename(source))
        threading.Thread(target=self._work, args=(source, where),
                         name="outspoken-extract", daemon=True).start()

    def _work(self, source, where):
        """The slow half. Never touches the dialog except through CallAfter."""
        lines = []
        try:
            got = ospextract.extract(
                source, where,
                say=lines.append,
                progress=lambda done, total: wx.CallAfter(
                    self._progress, done, total))
        except Exception as error:
            log.error("outSPOKEN: extraction failed", exc_info=True)
            wx.CallAfter(self._failed, str(error))
            return
        wx.CallAfter(self._finished, got, lines)

    def _progress(self, done, total):
        if not total:
            return
        percent = int(done * 100 / total)
        step = percent // ANNOUNCE_EVERY
        if step != self._lastAnnounced:
            self._lastAnnounced = step
            # Translators: progress while reading a disk image.
            self._say(_("%d percent.") % percent)

    def _failed(self, why):
        self._busy = False
        self._pick.Enable()
        gui.messageBox(
            # Translators: shown when reading a disk image failed.
            _("That image could not be read.\n\n%s") % why,
            _("Macintosh speech data"), wx.OK | wx.ICON_ERROR)
        self._pick.SetFocus()

    def _finished(self, got, lines):
        self._busy = False
        self._pick.Enable()
        self.refresh()
        if got is None:
            gui.messageBox(
                # Translators: the image held nothing this add-on can use.
                _("Nothing in that image was recognised.\n\n"
                  "It should hold outSPOKEN itself, or an Extensions folder "
                  "with MacinTalk 2, MacinTalk 3 or MacinTalk Pro in it."),
                _("Macintosh speech data"), wx.OK | wx.ICON_INFORMATION)
            self._pick.SetFocus()
            return
        written, _skipped = got
        have = ospextract.installed_engines(self.folder())[0]
        voices = sum(len(v) for v in have.values())
        # Translators: the result of installing from a disk image.
        message = _("%d resources installed.\n\n"
                    "NVDA will offer %d voice%s, from %d engine%s.\n\n"
                    "Restart NVDA to use them.") % (
            written, voices, "" if voices == 1 else "s",
            len(have), "" if len(have) == 1 else "s")
        self._say(_("Finished. %d voices.") % voices)
        gui.messageBox(message, _("Macintosh speech data"),
                       wx.OK | wx.ICON_INFORMATION)
        self._pick.SetFocus()

    # -- the rest ---------------------------------------------------------

    def onOpenFolder(self, evt):
        where = self.folder()
        if not where:
            return
        try:
            os.makedirs(where, exist_ok=True)
            os.startfile(where)
        except Exception:
            log.error("outSPOKEN: cannot open the engine folder", exc_info=True)

    def _say(self, message):
        if ui is not None:
            try:
                ui.message(message)
            except Exception:
                pass

    def onClose(self, evt):
        if self._busy:
            #: Closing mid-extraction would leave the worker writing into a
            #: folder nothing is watching, and the next run would find pieces
            #: of two images in it.
            self._say(_("Still reading the image."))
            return
        SpeechDataDialog._instance = None
        self.Destroy()
