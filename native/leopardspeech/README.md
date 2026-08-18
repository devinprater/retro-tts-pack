# Leopard host

`leopard_host.exe` is built from the shared loader in
<https://github.com/tgeczy/tiger-speech> (the loader used by the official
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
loader checkout before building. The checked-in executable was cross-built
from that source with Clang and Fedora's MinGW32 headers and libraries.
