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
  BeSTspeech, Centigram TruVoice, Microsoft Sam/Mike/Mary, and David/Zira/Mark
- Speech Dispatcher with the `sd_generic` module
- a command to play audio: PipeWire's `pw-play`, PulseAudio's `paplay`, or
  ALSA's `aplay`, and the native client tries them in that order. The engines
  that stream their audio (L&H, TrueVoice and Amiga Narrator) play through
  `pw-play` themselves and do need it; the rest work with whichever is present
- a working per-user systemd manager, recommended but not mandatory
- for the Apple generations, Tiger, Leopard and Lion: on x86_64 the 32-bit
  runtime, `libc6:i386` and `libstdc++6:i386`; on aarch64 the 64-bit one,
  `libc6`, `libstdc++6` and `libgcc-s1`. Either way `libsqlite3` (`:i386` on
  x86_64) for Leopard's phrasing dictionary. They need no Wine
- Wine only as a fallback: for WinTalker, and for the Apple generations on an
  x86_64 machine without the 32-bit runtime

Those three run through Panthera's own host, which maps the Mach-O engine
directly on x86_64 with no Wine and no emulation, and runs it through Box64 on
aarch64. Both builds carry their own AAC decoder, so neither needs Wine or
FFmpeg, and `install.sh` builds neither: the optional downloader installs the one
for this machine from that project's pinned release.

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

To download the checksum-pinned Prose 2000 files published in that project's
GitHub release:

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

SAM and ST Speech work immediately, as do the engines built from the vendored
sources when a C compiler is present: BeSTspeech, Centigram TruVoice,
Microsoft Sam/Mike/Mary, and David/Zira/Mark. `--download-assets` also fetches
the Prose 2000 package. Other engines require original, legally obtained files
that are not included.

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
| BeSTSpeech | none: the engine is native openbst, built from source when `install.sh` runs, with its tables compiled in. Its 1995 build and the twelve 2006 language builds are offered; the six 1998 builds are not |
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

See which command the modules call, which is where roughly half the wait before
a word is heard lives:

```sh
grep -A1 GenericExecuteSynth ~/.config/speech-dispatcher/modules/bestspeech-generic.conf
```

If it names `retro-tts-client`, the native client is in use and costs about two
milliseconds. If it names `retro-tts`, no compiler was found at install time and
every utterance pays a Python interpreter's start-up instead, roughly 45 ms
measured, on every key press. Reinstalling with `cc` present fixes it.

Render directly to a WAV file:

```sh
retro-tts --persistent --engine stspeech --output test.wav --text "Test."
```

If a proprietary engine reports a missing path, verify spelling and filename
case under `~/.local/share/retro-tts-pack/assets`, then rerun `install.sh`.

### If you hear broken eSpeak, or vowels that stutter

That is not one of this pack's engines. Speech Dispatcher still ships a stock
module, `espeak-ng-mbrola`, whose synthesis line pushes eSpeak's phonemes into
MBROLA:

```sh
printf %s '$DATA' | espeak-ng -v mb-$VOICE -s $RATE -p $PITCH $PUNCT \
  -q --stdin --pho | mbrola -v $VOLUME -e /usr/share/mbrola/$VOICE/$VOICE \
  - -.au | $PLAY_COMMAND
```

MBROLA is usually not installed, so the last stage of that pipeline receives
nothing while eSpeak still runs. The module ships anyway, and Speech Dispatcher
chooses it whenever the output module that was asked for is not available.
Because this pack sets neither `DefaultModule` nor `LanguageDefaultModule`, a
pack engine that could not be loaded is exactly the case that lands there.

So when a voice that is not one of the pack's speaks, check which module is
really in use, and name one by hand if you want:

```sh
spd-say -O                                  # the modules Speech Dispatcher knows
spd-say -w -o bestspeech "bestspeech is speaking"
```

`install.sh` prints which engines it could not load and what each one needs, so
an engine missing its assets shows up there rather than only here, by ear.

WinTalker requires a working x86_64 Wine installation. The Apple generations
prefer the native host and use Wine only where the 32-bit runtime is missing,
which is x86_64 alone. On aarch64 the host runs the engine through Box64, and
Tiger's formant voices come out; Leopard and Lion are left off there, because
upstream's ARM64 build does not implement the CoreFoundation shim
`_CFPropertyListCreateFromXMLData` and returns silence for them. WinTalker's
small x86_64 host can be rebuilt with `make -C native/wintalker` when `winegcc`
is installed.

## Raspberry Pi notes

The ARM64 archive targets 64-bit Raspberry Pi OS and comparable glibc-based
aarch64 distributions. A Compute Module 5 has substantially more memory than
these engines need; 16 GB is ample. Its four Cortex-A76 cores should handle the
native emulators and Unicorn-backed engines well, although final Orca latency
depends on cooling, storage, PipeWire configuration, and the desktop workload.
The release is cross-compiled with Arm GNU Toolchain and smoke-tested under
QEMU aarch64; it has not yet been timed on physical Compute Module hardware.
BeSTspeech, Centigram TruVoice, Microsoft Sam/Mike/Mary and the OneCore voices
are built from source during installation, so those need a C compiler (`cc`)
on ARM64 rather than a shipped binary.

The Apple generations have an ARM64 host of their own, which the optional
downloader installs. It runs Apple's i386 engine through Box64, linked into it,
with the same Glint decoder, so there is no Wine and no translator to install,
and it needs only the aarch64 runtime listed above. **Tiger speaks there, and
Leopard and Lion are deliberately left off:** upstream's ARM64 build does not
implement the CoreFoundation shim `_CFPropertyListCreateFromXMLData`, so those
two come out silent, and a synthesizer that says nothing is worse than one that
is not offered, because Speech Dispatcher falls back to eSpeak and the user
hears the wrong voice. The installer renders a voice and requires samples
before it enables a generation, so this shows up as a skip with a reason rather
than as silence.

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
and engine DLLs are intentionally excluded. Two exceptions are the constant
tables of the native OpenTV engine, under `native/opentv/generated/`, and the
tables of the native openbst engine, under `native/openbst/src/data/`. Those
bytes are Centigram's and Berkeley Speech Technologies' respectively, not
OpenTV's or openbst's, and the notices under `licenses/` say so.
