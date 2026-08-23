# Third-party components

## Musashi — `third_party/musashi/`

A portable Motorola M680x0 emulation engine by Karl Stenerud, version 4.60.
This is the 68000 core MAME uses. Vendored from
<https://github.com/kstenerud/Musashi>.

MIT licence — the notice below must travel with any distribution of this
project, source or binary.

```
Copyright Karl Stenerud.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### Local modification

`m68kconf.h` — `M68K_INSTRUCTION_HOOK` changed from `M68K_OPT_OFF` to
`M68K_OPT_ON`. The host needs a per-instruction hook to catch A-line traps at
their vector and to enforce a counted instruction budget. No other change.

---

# What is **not** here, and never will be

**MacinTalk and outSPOKEN are not in this repository and are not distributed
with it.**

`DRVR 1030` (`.sp`) is MacinTalk, © 1984 Joseph Katz and Mark Barton.
outSPOKEN is © 1989 Berkeley Systems, Inc., later ALVA BV. We have no
permission to redistribute either, and how the code is executed has no bearing
on that — running it under an emulator is not a licence.

This project ships **only the emulator and host**. The engine comes from the
user's own copy of outSPOKEN, placed in the add-on's `rom/` folder by hand.
That arrangement is the entire reason this repository is publishable, and it is
the same one EchoTalk uses for its Textalker images.
