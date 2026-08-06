# Project and third-party notice

Retro TTS Pack combines original Linux integration code with third-party
emulators, compatibility shims, and runtime components.

License texts supplied by the corresponding upstream projects are retained
under `licenses/`. The presence of a license file for one component does not
change the license of another component.

Original commercial synthesizer DLLs, ROMs, firmware, dictionaries, Amiga
devices, and related engine data are not part of this repository or its
release archive. The optional downloader only retrieves checksum-pinned files
that the BeSTSpeech wrapper and Prose 2000 projects publish in their own GitHub
releases. Users remain responsible for complying with applicable upstream
terms.

WinTalker support includes only the open integration host. `WinTalker.dll`,
`English.lex`, and the MacInTalk voice data must be supplied separately by the
user and are not downloaded or redistributed by this project.

Important upstream projects include:

- <https://github.com/samtupy/b32tts_wrapper>
- <https://github.com/OnjLouis/prose2000>
- <https://github.com/daiverd/doubletalk-pc>
- <https://github.com/nicodex/AmigaNarrator>
- <https://github.com/daiverd/rusty_tts>
- <https://github.com/unicorn-engine/unicorn>
