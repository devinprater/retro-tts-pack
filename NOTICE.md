# Project and third-party notice

Retro TTS Pack combines original Linux integration code with third-party
emulators, compatibility shims, and runtime components.

License texts supplied by the corresponding upstream projects are retained
under `licenses/`. The presence of a license file for one component does not
change the license of another component.

Original commercial synthesizer DLLs, ROMs, firmware, dictionaries, Amiga
devices, and related engine data are not part of this repository or its
release archive. The exceptions are the constant tables of the native OpenTV
engine, under `native/opentv/generated/`, and of the native openbst engine,
under `native/openbst/src/data/`. Those bytes are Centigram's and Berkeley
Speech Technologies' respectively, and the notices under `licenses/opentv/`
and `licenses/openbst/` say so. The optional downloader retrieves
checksum-pinned packages
from engine projects and their DECtalk.nu/datajake mirrors. Users remain
responsible for complying with applicable upstream terms.

WinTalker support includes only the open integration host. `WinTalker.dll` and
`English.lex` are installed from the separately published NVDA add-on when the
optional downloader is used; they are not redistributed in release archives.

The SAPI 5 voices (Sam, Mike and Mary) and the OneCore voices (David, Zira and
Mark) are run by portable C reconstructions, but they cannot speak without their
voice data, which is Microsoft's. The optional downloader takes it from Quinton
Williams's Sapple build (<https://quintonwilliams.me/sapple/>), which publishes
that archive with the data inside for personal use, under Sapple's own terms and
not under a licence, and which removes it if a rights holder asks. Sapple also
publishes the SHA-256 of the archive and of the OneCore files, and the
downloader checks both. None of this data is redistributed in this pack's
release archives, and the engines here are the open reconstructions named in
`licenses/mssam/` and `licenses/onecore/`, not Microsoft's own.

Tiger, Leopard and Lion MacinTalk support includes the open integration host
from <https://github.com/tgeczy/panthera-speech>, which the optional downloader
installs from that project's pinned Linux releases: the i686 one on x86_64, and
the aarch64 one on ARM, which links Box64 (MIT, notices retained as
`licenses/tiger-speech/box64-LICENSE.txt` and `Box-component-notices.txt`). The host carries the
Glint AAC decoder, by CrispStrobe, whose MIT notice is retained in
`licenses/tiger-speech/` beside Panthera's. The optional downloader can install
separately published Tiger, Leopard, and Lion Mac OS X engines,
SpeechDictionary frameworks, and voices from DECtalk.nu's Apple directory, but
they are not redistributed in release archives.

Native engine sources are vendored under `native/`, each under its own MIT
license in `licenses/`:

- OpenTV, Centigram TruVoice rebuilt in portable C, by RetroBunn:
  <https://github.com/RetroBunn/tv-decomp> (`licenses/opentv/`; its tables
  are Centigram's, as that folder's NOTICE explains).
- Microsoft Sam, Mike and Mary in portable C, by KamiKitsune420:
  <https://github.com/KamiKitsune420/ms-sam-mike-mary-decomp> (`licenses/mssam/`).
- Microsoft David, Zira and Mark (OneCore) in portable C, by KamiKitsune420:
  <https://github.com/KamiKitsune420/ms-david-zira-decomp> (`licenses/onecore/`).
- openbst, BeSTspeech / Keynote Gold in portable C, by Mudb0y:
  <https://github.com/Mudb0y/openbst> (`licenses/openbst/`; its tables are
  Berkeley Speech Technologies' and HumanWare's, as `native/openbst/README.md`
  explains).

Important upstream projects include:

- <https://github.com/samtupy/b32tts_wrapper>
- <https://github.com/OnjLouis/prose2000>
- <https://github.com/daiverd/doubletalk-pc>
- <https://github.com/nicodex/AmigaNarrator>
- rusty_tts by daiverd (MIT; license retained in `licenses/rusty_tts/`).
  The upstream repository is no longer published, so no link is given.
- <https://github.com/unicorn-engine/unicorn>
- <https://github.com/tgeczy/panthera-speech>
- <https://github.com/jaybird110127/echotalk>
- <https://github.com/tgeczy/outspoken-nvda>
- <https://github.com/kstenerud/Musashi>
