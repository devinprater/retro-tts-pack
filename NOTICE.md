# Project and third-party notice

Retro TTS Pack combines original Linux integration code with third-party
emulators, compatibility shims, and runtime components.

License texts supplied by the corresponding upstream projects are retained
under `licenses/`. The presence of a license file for one component does not
change the license of another component.

Original commercial synthesizer DLLs, ROMs, firmware, dictionaries, Amiga
devices, and related engine data are not part of this repository or its
release archive. The optional downloader retrieves checksum-pinned packages
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
