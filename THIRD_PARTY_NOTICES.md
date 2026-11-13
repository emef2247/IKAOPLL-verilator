# Third-party attribution and notices

The project source is licensed under the BSD 2-Clause License:
Copyright (c) 2025 emef2247 (see `LICENSE`).
This file distinguishes the upstream RTL the renderer drives, build-time
tools and development-only comparison references. Their own notices and
conditions remain applicable. The upstream RTL is not vendored by this
project; a source release of this project is not a distribution of the RTL.

## IKAOPLL

Project: [ika-musume/IKAOPLL](https://github.com/ika-musume/IKAOPLL)

Use: IKAOPLL is the YM2413 (OPLL) RTL reimplementation that generates all
sound in this project. The harness only decodes the VGM, drives clocks and
bus writes, and captures the RTL's digital output. The RTL sources are not
included in this repository: they are fetched locally into `rtl/` (see
`setup_env.sh` / `tools/create_rtl_dir.sh`), and the fetched upstream
license text is kept at `rtl/IKAOPLL_LICENSE`. The vendor RTL is used
unchanged; harness parameter choices are recorded in the documentation.
A working sample archive supplied by Hiroyuki (held locally under
`fromHiroyuki-san/`, not committed or redistributed) pinned the RTL
parameter configuration used by the build.

Upstream license (as saved in `rtl/IKAOPLL_LICENSE` at fetch time):

```text
BSD 2-Clause License

Copyright (c) 2023, Raki

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Verilator

Project: [verilator/verilator](https://github.com/verilator/verilator)

Use: build-time tool that compiles the IKAOPLL RTL into the simulator
executable. It is invoked during the build and is not distributed with this
project. Verilator itself is dual-licensed under the LGPL-3.0-or-later and
the Artistic License 2.0; see the upstream project for its terms. The
renderer was verified with Verilator 4.038.

## zlib

Use: the simulator links zlib (`-lz`) to read gzip-compressed VGM/VGZ
input. zlib is a system library dependency and is not distributed with this
project. It is provided under the zlib license; copyright (C) 1995-2022
Jean-loup Gailly and Mark Adler.

## EMU2413 / libvgm (comparison only)

Projects: [libvgm](https://github.com/ValleyBell/libvgm),
EMU2413 by Mitsutaka Okazaki.

Use: development-only comparison. An independently rendered reference WAV
(EMU2413 core from a pinned libvgm checkout, commit
`bad50c571db45b872304bdcd0603dd2214f0663e`) is compared against the RTL
output by the optional `tools/compare_opll_wavs.py`. The emulator is never
part of the production renderer, and no upstream core or reference binary
is redistributed in this repository. EMU2413 is distributed by its author
under the MIT License; preserve the upstream notices from the pinned
checkout if the optional reference installation is redistributed.

## Distribution scope

Include this notice and the project `LICENSE` with source distributions.
If the fetched RTL is redistributed, the upstream BSD 2-Clause notice above
must accompany it. Source fixture and music rights are separate from
software licenses; this file provides no permission to redistribute private
or third-party song data.
