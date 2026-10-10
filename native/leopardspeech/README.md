# Leopard host

`leopard_host.exe` is built from the shared loader in
<https://github.com/tgeczy/panthera-speech> (the loader used by the official
Leopard Speech add-on).

This build adds a `TIGER_WINE_AAC` compatibility switch. When set, the AAC
media type appends the voice's AudioSpecificConfig to `MF_MT_USER_DATA` after
the 12-byte HEAACWAVEINFO extension. Native Windows requires the extension by
itself, while Wine's GStreamer Media Foundation bridge requires the appended
config to construct raw AAC `codec_data`. Without it Alex and Vicki decode to
all-zero PCM under Wine.

The Python adapter sets the switch automatically whenever it launches the
host through Wine.

The source change is retained in `wine-aac.patch`; apply it to the shared
loader checkout before building. The cross-built executable is the Wine
fallback.

The Wine-free route is upstream's own i686 Linux build of the same loader,
which the optional downloader installs as `bin/panthera_host`. It maps the
Mach-O engine directly and carries its own Glint AAC decoder, so it needs
neither Wine nor FFmpeg, only the 32-bit runtime: `libc6:i386`,
`libstdc++6:i386`, and `libsqlite3-0:i386` for Leopard's dictionary. The same
binary serves all three generations, since the engine, dictionary and voice
directory are arguments. Build it on another distribution with
`sh ./build_linux.sh i686` from <https://github.com/tgeczy/panthera-speech>.
Upstream records that script without its execute bit, so the plain `./`
form fails with "Permission denied"; run it through `sh`.
