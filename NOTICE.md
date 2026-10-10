# Project and third-party notice

Retro TTS Pack combines original Linux integration code with third-party
emulators, compatibility shims, and runtime components.

License texts supplied by the corresponding upstream projects are retained
under `licenses/`. The presence of a license file for one component does not
change the license of another component.

Original commercial synthesizer DLLs, ROMs, firmware, dictionaries, Amiga
devices, and related engine data are not part of this repository or its
release archive. The one exception is the native OpenTV engine's constant
tables, vendored with that project's sources under `native/opentv/generated/`;
they are Centigram's, and `licenses/opentv/NOTICE` is the notice that comes
with them. The optional downloader retrieves checksum-pinned packages
from engine projects and their DECtalk.nu/datajake mirrors. Users remain
responsible for complying with applicable upstream terms.

WinTalker support includes only the open integration host. `WinTalker.dll` and
`English.lex` are installed from the separately published NVDA add-on when the
optional downloader is used; they are not redistributed in release archives.

Leopard Speech support includes the open integration host from tiger-speech.
The optional downloader can install separately published Tiger, Leopard, and
Lion Mac OS X engines, SpeechDictionary frameworks, and voices from
DECtalk.nu's Apple directory, but they are not redistributed in release
archives.

Native engine sources are vendored under `native/`, each under its own MIT
license in `licenses/`:

- OpenTV, Centigram TruVoice rebuilt in portable C, by RetroBunn:
  <https://github.com/RetroBunn/tv-decomp> (`licenses/opentv/`; its tables
  are Centigram's, as that folder's NOTICE explains).
- Microsoft Sam, Mike and Mary in portable C, by KamiKitsune420:
  <https://github.com/KamiKitsune420/ms-sam-mike-mary-decomp> (`licenses/mssam/`).
- Microsoft David, Zira and Mark (OneCore) in portable C, by KamiKitsune420:
  <https://github.com/KamiKitsune420/ms-david-zira-decomp> (`licenses/onecore/`).

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
