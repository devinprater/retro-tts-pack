# Open questions

Things worth a decision rather than a guess. None of them block the pack as it
stands.

## The Apple generations on ARM64

The three Apple generations now run natively on x86_64 through Panthera's i686
host, which needs only the 32-bit runtime. On aarch64 there is no host in the
pack at all, and those engines are the slowest thing in it for the Pi as a
result. Upstream builds one: `./build_linux.sh aarch64`, which runs Apple's
i386 code through Box64 with the same Glint decoder, all MIT, and publishes it
as `panthera-linux-aarch64-<version>.tar.gz`, about 34 MB. Upstream says it is
built and exercised in CI, and that the same translator and host were validated
by ear on ARM64 phones, but that nobody has yet listened to it on an ARM Linux
machine. Worth deciding whether to install it for Pi users, and whether the
34 MB belongs in the optional downloader or somewhere a user opts into.

## Preloading the Apple host when the daemon starts

Measured: the first utterance after the daemon starts waits about 1.5 to 2
seconds while Leopard's host maps the engine and opens its sample bank, and
every utterance after that is 11 to 40 ms to the first audio. The engine module
has a `preload()` for exactly this, and the daemon does not appear to call it
for these generations. Worth deciding whether to preload at daemon start, since
a screen reader's first utterance of a session is often the one that matters
most, and the cost would be paid once in the background instead.

## Whether to keep the Wine fallback for the Apple generations

Wine is now only a fallback, for machines without the 32-bit runtime. On such a
machine the pack can still speak through the PE host, which needs Wine, an
emulated Windows layer, and the AAC workaround in `native/leopardspeech/`. The
fallback is what the pack had before, so nothing is lost by keeping it; the
question is whether it earns its complexity now that the native route is the
default and the check that chose it actually works.
