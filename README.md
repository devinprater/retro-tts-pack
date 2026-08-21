# Retro TTS Pack for Speech Dispatcher and Orca

Project repository: <https://github.com/devinprater/retro-tts-pack>

This Linux pack provides eleven retro speech synthesizer adapters on x86_64 and
nine on ARM64 (aarch64):

- SAM
- Atari ST Speech
- SmoothTalker / Dr. Sbaitso
- Monolog / ProVoice
- Prose 2000
- DoubleTalk PC
- BeSTSpeech / Keynote Gold
- SoftVoice
- Amiga Narrator
- WinTalker / MacInTalk
- Leopard Speech / Mac OS X 10.5 MacinTalk (including Alex)

The persistent renderer keeps emulators and engine state warm. Audio playback
uses a 10 ms PipeWire buffer, and Amiga Narrator streams audio while its 68000
emulator is running. Wine is required for WinTalker and is only a fallback for
Leopard Speech when a native host is not present.

## What's new in 0.4.1

- Updated Leopard Speech behavior to upstream 0.7.3, including corrected
  stress for the spoken word "colon" and per-voice volume normalization.
- Limited queued audio to 80 ms chunks for faster interruption during Orca
  navigation while retaining the stable native host lifecycle.

See [CHANGELOG.md](CHANGELOG.md) for the complete release summary.

## Quick installation

Requirements:

- x86_64 or ARM64 (aarch64) Linux
- Python 3.10 or newer
- Speech Dispatcher with the `sd_generic` module
- PipeWire's `pw-play` command
- a working per-user systemd manager, recommended but not mandatory
- Wine for WinTalker, and for Leopard Speech only when using its PE fallback

Native Leopard Speech additionally needs the distribution's 32-bit/i686
runtime libraries for glibc, FFmpeg (`libavcodec`, `libavutil`, and
`libswresample`), and SQLite.

The included native binaries target a recent glibc-based Linux distribution.
If the loader reports a missing `GLIBC` or `GLIBCXX` version, rebuild the
native hosts on the target distribution from their upstream source trees.

Run:

```sh
./install.sh
```

To download the checksum-pinned BeSTSpeech and Prose 2000 files published in
their projects' GitHub releases:

```sh
./install.sh --download-assets
```

An interactive installation also offers this choice. Noninteractive
installations do not download proprietary assets unless the option is given.

The installer places the pack in
`$XDG_DATA_HOME/retro-tts-pack` (normally
`~/.local/share/retro-tts-pack`), installs `~/.local/bin/retro-tts`, writes
per-user Speech Dispatcher modules, and enables `retro-tts.service`.

SAM and ST Speech work immediately. `--download-assets` also enables
BeSTSpeech and Prose 2000. Other engines require original, legally obtained
files that are not included.

## Required engine assets

Put files in this pack's `assets/` directories before running `install.sh`, or
put them in the installed copy and run the installer again.

| Engine | Required files |
|---|---|
| SmoothTalker | `assets/smoothtalker/engine.bin` |
| Monolog | `assets/monologue/FB_11K8.DLL`, `FB_22K16.DLL`, `FB_DEFLT.DIC`, `FB_NGN.EXE`, `FB_SPCH.DLL`, `FB_TIMER.DLL` |
| Prose 2000 | `assets/prose2000/v3.4.1__2000__2.u22`, `v3.4.1__2000__3.u45`, `v3.4.1__2000__0.u21`, `v3.4.1__2000__1.u44`, `v3.12__8-9-88__dsp_prog.u29`, `v3.12__8-9-88__dsp_data.u29` |
| DoubleTalk PC | `assets/doubletalkpc/doubletalkpc.bin` |
| BeSTSpeech | `assets/bestspeech/b32_tts.dll` |
| SoftVoice | `assets/softvoice/tibase32.dll`, `tieng32.dll` |
| Amiga Narrator | `assets/amiganarrator/narrator.device` and either `translator.library` or `cmudict.txt` |
| WinTalker (x86_64 only) | `assets/wintalker/WinTalker.dll`, `English.lex`, and Wine installed on the host |
| Leopard Speech (x86_64 only) | Extract the separately supplied archive so that `assets/leopardspeech/leopardspeech-data/Speech/Voices` exists; the installer prefers a native host and otherwise requires Wine |

These files originated in commercial products or add-ons. This distribution
does not grant permission to copy them.

The downloader intentionally does not fetch SoftVoice, WinTalker, Leopard Speech, or the remaining
commercial firmware from generic DLL sites or archival disk images. Public
availability alone does not establish redistribution permission, and those
sources cannot be authenticated as project-published releases.

## Selecting a voice in Orca

After installation and a Speech Dispatcher restart, open Orca Preferences,
choose Speech, and select one of the installed synthesizers. The installer
only registers engines whose required assets were found.

If voices do not appear, restart Orca. On systems where Speech Dispatcher is
not managed by a systemd user service, log out and back in or restart the
user's Speech Dispatcher process using the method recommended by the Linux
distribution.

## Without a systemd user manager

Start this command from the desktop session before Orca:

```sh
~/.local/share/retro-tts-pack/bin/retro-tts-server
```

Configure the desktop's autostart facility to run it at login. It requires
`XDG_RUNTIME_DIR` and access to the user's PipeWire session.

## Diagnostics

Check the renderer:

```sh
systemctl --user status retro-tts.service
journalctl --user -u retro-tts.service -n 100
```

Test a module:

```sh
spd-say -w -o sam "SAM is working."
spd-say -w -o bestspeech "BeSTSpeech is working."
spd-say -w -o leopardspeech "Alex is working."
```

List configured modules:

```sh
spd-say -O
```

Render directly to a WAV file:

```sh
retro-tts --persistent --engine stspeech --output test.wav --text "Test."
```

If a proprietary engine reports a missing path, verify spelling and filename
case under `~/.local/share/retro-tts-pack/assets`, then rerun `install.sh`.

WinTalker requires a working x86_64 Wine installation. Leopard Speech prefers
the native i386 Linux host when one is packaged and otherwise uses Wine.
Neither engine runs on ARM64. WinTalker's small x86_64 host can be rebuilt with
`make -C native/wintalker` when `winegcc` is installed.

## Raspberry Pi notes

The ARM64 archive targets 64-bit Raspberry Pi OS and comparable glibc-based
aarch64 distributions. A Compute Module 5 has substantially more memory than
these engines need; 16 GB is ample. Its four Cortex-A76 cores should handle the
native emulators and Unicorn-backed engines well, although final Orca latency
depends on cooling, storage, PipeWire configuration, and the desktop workload.
The release is cross-compiled with Arm GNU Toolchain and smoke-tested under
QEMU aarch64; it has not yet been timed on physical Compute Module hardware.

## Manual Speech Dispatcher setup

If the installer cannot find a base `speechd.conf`, copy the system's
`speechd.conf` into `~/.config/speech-dispatcher/speechd.conf`. For each
available engine, add:

```text
AddModule "sam" "sd_generic" "sam-generic.conf"
```

Replace `sam` with the module name. Module configurations are installed under
`~/.config/speech-dispatcher/modules/`.

## Contents and licensing

The pack contains open-source adapters, emulators, native compatibility shims,
and the Unicorn runtime. Corresponding license texts are under `licenses/`.
Original synthesizer firmware, dictionaries, ROMs, devices, and engine DLLs
are intentionally excluded.
