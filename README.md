# Retro TTS Pack for Speech Dispatcher and Orca

Project repository: <https://github.com/devinprater/retro-tts-pack>

This Linux pack provides nineteen retro speech synthesizer adapters on x86_64
and fourteen on ARM64 (aarch64):

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
- EchoTalk / Echo II Textalker 1.3 and 3.1.3
- OutSpoken: MacinTalk 1, 2, 3, and Pro
- Centigram TruVoice 5.10
- Microsoft Sam, Mike and Mary (SAPI 5)
- Microsoft David, Zira and Mark (Windows OneCore)
- L&H TTS3000 6.x (x86_64)
- Panthera Speech generations: Tiger 10.4, Leopard 10.5, and Lion 10.7
  MacinTalk (including Vicki and Alex)

The persistent renderer keeps emulators and engine state warm. Audio playback
uses a 10 ms PipeWire buffer, and Amiga Narrator streams audio while its 68000
emulator is running. Wine is required for WinTalker and is only a fallback for
Leopard Speech when a native host is not present.

## What's new in 1.1.0

- Added native Centigram TruVoice 5.10 and L&H TTS3000 6.x support on x86_64.
- Recreated the symbol-rich `cgrm_spk.cpp` utility as a native Linux host.
- Locally imports the newly supplied installers without redistributing their
  proprietary DLLs.

See [CHANGELOG.md](CHANGELOG.md) for the complete release summary.

## Quick installation

Requirements:

- x86_64 or ARM64 (aarch64) Linux
- Python 3.10 or newer
- a C compiler (`cc`) for the engines built from source at install time:
  Centigram TruVoice, Microsoft Sam/Mike/Mary, and David/Zira/Mark
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

### NixOS

A `flake.nix` is included (x86_64-linux). It patches the prebuilt binaries
for the Nix store and runs the normal installer:

```sh
nix run github:devinprater/retro-tts-pack#install
```

or install it into your profile and run `retro-tts-pack-install`. The flake
honours the same `install.sh` options, e.g. `--download-assets`.

To download the checksum-pinned BeSTSpeech and Prose 2000 files published in
their projects' GitHub releases:

```sh
./install.sh --download-assets
```

An interactive installation also offers this choice. Noninteractive
installations do not download proprietary assets unless the option is given.
The opt-in downloader also retrieves checksum-pinned SmoothTalker, Monologue,
DoubleTalk PC, WinTalker, SoftVoice, Amiga Narrator, and
all L&H TTS3000 language packages from DECtalk.nu, retaining
datajake as a fallback for packages mirrored identically there. DECtalk.nu also
provides the checksum-pinned Tiger and Leopard trees, EchoTalk Textalker images,
the Lion tree, and the OutSpoken MacinTalk ROM collection from its dedicated
Apple directory. Snow Leopard and Sequoia data are not used by this pack.

The installer places the pack in
`$XDG_DATA_HOME/retro-tts-pack` (normally
`~/.local/share/retro-tts-pack`), installs `~/.local/bin/retro-tts`, writes
per-user Speech Dispatcher modules, and enables `retro-tts.service`.

SAM and ST Speech work immediately. `--download-assets` also enables
BeSTSpeech and Prose 2000. Other engines require original, legally obtained
files that are not included.

### Run it as your own user, never with `sudo`

**This is a per-user install. There is no system-wide mode and no `--system`
flag.** Do not run `sudo ./install.sh`.

Every path the installer uses derives from `$HOME`, so under `sudo` the entire
pack — binaries, Speech Dispatcher modules, and the systemd user unit — lands in
`/root/.local/share/retro-tts-pack` and nothing appears in your own account. Your
screen reader runs as you, never reads root's home directory, and so never sees
the modules. You also get `WARNING: no usable systemd user manager`, because under
`sudo` there is no user session for the renderer service to attach to. The install
reports success while being invisible to the very thing that would use it.

```sh
./install.sh            # correct: installs for you
sudo ./install.sh       # wrong: installs for root, unreachable
```

If you already installed with `sudo`, remove the root copy and reinstall as
yourself:

```sh
sudo rm -rf /root/.local/share/retro-tts-pack
sudo rm -f  /root/.config/systemd/user/retro-tts.service
./install.sh
```

This applies to console screen readers too. Fenrir, like Orca, runs as your user
and reads your `~/.config/speech-dispatcher/`, so a per-user install is the one it
will find.


## Required engine assets

Put files in this pack's `assets/` directories before running `install.sh`, or
put them in the installed copy and run the installer again.

| Engine | Required files |
|---|---|
| SmoothTalker | `assets/smoothtalker/engine.bin` |
| Monolog | `assets/monologue/FB_11K8.DLL`, `FB_22K16.DLL`, `FB_DEFLT.DIC`, `FB_NGN.EXE`, `FB_SPCH.DLL`, `FB_TIMER.DLL` |
| Prose 2000 | `assets/prose2000/v3.4.1__2000__2.u22`, `v3.4.1__2000__3.u45`, `v3.4.1__2000__0.u21`, `v3.4.1__2000__1.u44`, `v3.12__8-9-88__dsp_prog.u29`, `v3.12__8-9-88__dsp_data.u29` |
| DoubleTalk PC | `assets/doubletalkpc/doubletalkpc.bin` |
| BeSTSpeech | `assets/bestspeech/b32_tts.dll`; the add-on's `dll_*.dll` files enable English, Dutch, French, German, Greek, Hebrew, Italian, Japanese, Polish, Portuguese, Russian, and Spanish |
| SoftVoice | `assets/softvoice/tibase32.dll`, `tieng32.dll`; `TISPAN32.DLL` enables Spanish |
| Amiga Narrator | `assets/amiganarrator/narrator.device` and either `translator.library` or `cmudict.txt` |
| EchoTalk | `assets/echotalk/textalker.ram.bin`, `textalker.obj.bin`, `textalker_v13.ram.bin`, and `textalker_v13.obj.bin` |
| OutSpoken | `assets/outspoken/outspoken-roms`; individual MacinTalk 1, 2, 3, or Pro generations are discovered from its subdirectories |
| Centigram TruVoice | none: the engine is native OpenTV, built from source when `install.sh` runs, with its tables compiled in |
| Microsoft Sam, Mike and Mary | `assets/mssam/Sam.spd`, `Sam.sdf`, `Mike.spd`, `Mike.sdf`, `Mary.spd`, `Mary.sdf`, `LTTS1033.LXA`, and `r1033tts.LXA`; the installer imports them by name from anywhere near the source tree |
| Microsoft David, Zira and Mark | `assets/onecore/MSTTSLocEnUS.dat`, and `M1033David.{APM,BEP,INI}`, `M1033Zira.{APM,BEP,INI}`, `M1033Mark.{APM,BEP,INI}` |
| L&H TTS3000 (x86_64 only) | the DLLs extracted from the supplied `lhtts*.exe` language installers; the installer extracts these automatically with 7-Zip or `cabextract` |
| WinTalker (x86_64 only) | `assets/wintalker/WinTalker.dll`, `English.lex`, and Wine installed on the host |
| Tiger Speech (x86_64 only) | `assets/tigerspeech/tigerspeech-data/Speech/Voices`; Wine is required by this pack |
| Leopard Speech (x86_64 only) | `assets/leopardspeech/leopardspeech-data/Speech/Voices`; the installer prefers its Wine-compatible native host where available |
| Lion Speech (x86_64 only) | `assets/lionspeech/lionspeech-data/Speech/Voices`, including `libstdc++.6.0.9.dylib` and `libc++abi.dylib`; Wine is required |

These files originated in commercial products or add-ons. This distribution
does not grant permission to copy them.

The downloader intentionally does not fetch the remaining
commercial firmware from generic DLL sites or archival disk images. Public
availability alone does not establish redistribution permission, and those
sources cannot be authenticated as project-published releases.

## Selecting a voice in Orca

After installation and a Speech Dispatcher restart, open Orca Preferences,
choose Speech, and select one of the installed synthesizers. The installer
only registers engines whose required assets were found.

Speech Dispatcher has no separate NVDA-style variant setting, so SoftVoice
personalities and Amiga Narrator sex/mode variants appear as selectable people.
BeSTSpeech exposes its classic personalities and language-specific people such
as `French - Fred`, `Japanese - Fred`, and `Spanish - Fred`.

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
spd-say -w -o tigerspeech "Vicki is working."
spd-say -w -o echotalk "Textalker is working."
spd-say -w -o outspoken "The original MacinTalk is working."
spd-say -w -o truevoice "Centigram TrueVoice is working."
spd-say -w -o mssam "Microsoft Sam is working."
spd-say -w -o onecore "Microsoft David is working."
spd-say -w -o lhtts "L and H TTS is working."
spd-say -w -o lionspeech "Lion Alex is working."
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
Centigram TruVoice, Microsoft Sam/Mike/Mary and the OneCore voices are built
from source during installation, so those three need a C compiler (`cc`) on
ARM64 rather than a shipped binary.

## Manual Speech Dispatcher setup

If the installer cannot find a base `speechd.conf`, copy the system's
`speechd.conf` into `~/.config/speech-dispatcher/speechd.conf`.

**Do not add `AddModule` lines to register these engines.** Speech Dispatcher
finds this pack's modules by directory, which is why the installer only places
files. A single explicit `AddModule` line turns that autodiscovery off: after it,
only the listed modules are loaded, every other synthesizer you have stops
appearing, and the alphabetically-first retro voice becomes your system default.
That is the failure people report as a jump scare. If an older version of this
installer left a `RETRO-TTS-PACK` block in your `speechd.conf`, the current
installer removes it — re-run `./install.sh` on an affected machine.

To confirm the engines were found, ask Speech Dispatcher what it has:

```sh
spd-say -O
```

Module configurations live in `~/.config/speech-dispatcher/modules/`. A config
there is picked up automatically when it is named `*-generic.conf` and the
command in its `GenericCmdDependency` line exists on your `PATH`.

## Pending work

The ordered backlog, including what is deliberately *not* being worked on and
why, is in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Contents and licensing

The pack contains open-source adapters, emulators, native compatibility shims,
vendored C engines, and the Unicorn runtime. Corresponding license texts are
under `licenses/`. Original synthesizer firmware, dictionaries, ROMs, devices,
and engine DLLs are intentionally excluded. The one exception is the native
OpenTV engine's constant tables, vendored with its sources under
`native/opentv/generated/`: those bytes are Centigram's, not OpenTV's, and
`licenses/opentv/NOTICE` is the notice that comes with them.
