# Open questions

Things worth a decision rather than a guess. None of them block the pack as it
stands.

## Upstream's ARM64 host returns silence for Leopard and Lion

The pack now installs Panthera's pinned aarch64 release, so the Apple
generations no longer need x86_64. Tiger speaks through it. Leopard and Lion do
not, and the cause is in that build rather than here: it is missing the
CoreFoundation shim `_CFPropertyListCreateFromXMLData`, which returns null, so
the speech dictionary never loads. The host's own diagnostics say it plainly.
On x86_64 the same call is present:

    aarch64  [uc] missing shim called: _CFPropertyListCreateFromXMLData -> 0
    x86_64   [shim] first call: _CFPropertyListCreateFromXMLData

Verified under qemu-aarch64 with an aarch64 Debian rootfs, not on Pi hardware,
so the diagnosis is from the host's own log rather than from listening. The
silence follows from a missing function, which emulation cannot invent, but a
listener on real hardware would settle it in a minute.

Two things to decide:

- **Whether to report it upstream.** Their release notes say the aarch64 build
  is exercised in CI and was validated by ear on ARM64 phones, and that nobody
  has yet listened to it on an ARM Linux machine, so this is likely to be news
  to them. It is their repository, so the report waits for a yes.
- **Whether to attempt a fix here.** The release ships its own sources
  (`sources/panthera.tar.gz`, `sources/box64.tar.gz`, `sources/glint.tar.gz`),
  so a cross-build with the shim filled in is possible. It is real work in
  someone else's loader, and it would need an aarch64 toolchain or the rootfs
  that already exists.

Until then the installer leaves those two generations off on aarch64, on
purpose, and says why: silence is worse than absence for a screen reader,
because Speech Dispatcher falls back to eSpeak and the reader hears the wrong
voice.

## Preloading the Apple host when the daemon starts

Measured: the first utterance after the daemon starts waits about 1.5 to 2
seconds while Leopard's host maps the engine and opens its sample bank, and
every utterance after that is 11 to 40 ms to the first audio. The engine module
has a `preload()` for exactly this, and the daemon does not appear to call it
for these generations. Worth deciding whether to preload at daemon start, since
a screen reader's first utterance of a session is often the one that matters
most, and the cost would be paid once in the background instead.

## Whether to keep the Wine fallback for the Apple generations

Wine is now only a fallback, for x86_64 machines without the 32-bit runtime. On
such a machine the pack can still speak through the PE host, which needs Wine, an
emulated Windows layer, and the AAC workaround in `native/leopardspeech/`. The
fallback is what the pack had before, so nothing is lost by keeping it; the
question is whether it earns its complexity now that the native route is the
default and the check that chose it actually works.
