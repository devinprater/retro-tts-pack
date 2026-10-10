# SAPI 4

`sapi4_speak` is built from Quinton Williams's sapi4-decomp (MIT), a
decompilation of Microsoft's 1999 SAPI 4 engine `msttssyn.dll` into portable C,
in the manner of OpenTV. It carries the engine's nineteen voice modes: Sam,
Mike and Mary with their in Hall, in Space, in Stadium and for Telephone
variants, the two whispers, and RoboSoft One through Six.

Microsoft's `msttssyn.dll` and the voice files (`.vce` and `.cfg`) are not here
and never may be; the engine reads its tables from that DLL at run time. The
optional downloader installs them from Quinton Williams's Sapple build, pinned
by their hashes, and the DLL's own hash is the one sapi4-decomp names in its
README. Nothing is read from the DLL at build time, so this tree builds without
any of it.

Build with `OUT=$(mktemp -d) sh build.sh`, then:

```sh
bin/sapi4_speak -d DIR -l                    # the modes the engine reports
bin/sapi4_speak -d DIR -m "Mike in Hall" -o out.wav "Hello there."
```

`DIR` holds `msttssyn.dll` and the voice files. `-o` takes a path, not `-` for
stdout, so the module here writes a temporary file and reads it back. `-s` sets
speed, `-p` pitch, and
`-t` treats the text as carrying SAPI 4 inline tags. It is a pure build: the
whole engine is decompiled C with hooks, and no emulated DLL is loaded.
