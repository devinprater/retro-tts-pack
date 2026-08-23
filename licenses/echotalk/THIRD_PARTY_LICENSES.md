# Third-party licenses

EchoTalk incorporates third-party source code. This file reproduces the
required notices. It must be distributed with any binary or source
release of EchoTalk.

---

## Fake6502 — public domain

**Files:** `third_party/fake6502/fake6502.c`

**Author:** Mike Chambers (miker00lz@gmail.com), 2011, version 1.1

**Upstream source:** <http://rubbermallet.org/fake6502.c>

**License statement, verbatim from the source file:**

> LICENSE: This source code is released into the public domain, but if
> you use it please do give credit. I put a lot of effort into writing
> this!

Being public domain, Fake6502 imposes no conditions on redistribution.
The credit above is given at the author's request.

**Local modifications:** EchoTalk's copy was reduced back to the plain
public-domain core from a third-party fork that had added debugger hooks
and external dependencies. The CPU emulation itself is unmodified.

---

## MAME — TMS5220 speech synthesizer emulation — BSD-3-Clause

**Files:**

- `third_party/tms5220/tms5220.cpp`, `third_party/tms5220/tms5220.h`
  — unmodified MAME source, kept as ground-truth reference. Not compiled
  into any EchoTalk binary.
- `third_party/tms5220/tms5110r.hxx` and
  `third_party/tms5220_core/tms5110r.hxx` — TMS51xx/TMS52xx coefficient
  and chirp ROM tables, used unmodified.
- `third_party/tms5220_core/tms5220_core.c`,
  `third_party/tms5220_core/tms5220_core.h`,
  `third_party/tms5220_core/tms5220_reset.c` — **derived from**
  `tms5220.cpp`/`.h`. This is a port of MAME's implementation out of its
  `device_t` class framework into standalone C. It is the code EchoTalk
  actually compiles and ships, and it remains subject to this license.

**Copyright holders:** Frank Palazzolo, Aaron Giles, Jonathan Gevaryahu,
Raphael Nabet, Couriersud, Michael Zapf
(`tms5110r.hxx`: Frank Palazzolo, Couriersud, Jonathan Gevaryahu)

**Local modifications to the ported files:** MAME's `device_t`,
save-state, timer, and callback infrastructure removed; the external
VSM/TMS6100 speech-ROM interface stubbed out; class methods converted to
plain C functions taking an explicit state pointer. The LPC synthesis
core (lattice filter, chirp table, interpolation, parameter decode) is a
faithful port of MAME's logic.

---

## MAME — Apple II Echo II card emulation — BSD-3-Clause

**Files:** `third_party/tms5220/a2echoii.cpp`, `third_party/tms5220/a2echoii.h`

**Copyright holder:** R. Belmont
(ready logic traced by Lord Nightmare and Tony Diaz, per the file header)

Kept as reference documentation of the card's bus protocol and `/READY`
handshake. Unmodified, and not compiled into any EchoTalk binary.

---

## BSD-3-Clause license text

Applies to all MAME-derived files listed above.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

---

## Not third-party code, but note on the contents of `roms/`

`roms/` holds Textalker binaries (`textalker.obj.bin`,
`textalker.ram.bin`, `textalker_v13.obj.bin`, `textalker_v13.ram.bin`)
extracted from user-supplied disk images. These are proprietary software
published by Street Electronics Corporation, with the v3.1.3 release
also carrying a 1986 American Printing House for the Blind copyright.

They are **not** licensed under any of the terms above and are present
here only as working material for development. EchoTalk executes them
under 6502 emulation rather than reimplementing them, so any distributed
build needs the question of how the end user supplies these files
settled separately from this document.
