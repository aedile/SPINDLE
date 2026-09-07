# Third-party code

## MAME (BSD-3-Clause)

Three parts of this project are written from MAME and carry its copyright:

- `core/tempest.c` — the memory map, the pot-wired input ports, the periodic
  interrupt and the EAROM control lines, from `src/mame/atari/tempest.cpp`.
- `core/avg.c`, `core/avg.h` — the Analog Vector Generator, from
  `src/devices/video/avgdvg.cpp`, with the Tempest variant's colour and
  intensity handling and its endless-loop frame boundary.
- `core/mathbox.c` — the math box, from `src/mame/atari/mathbox.cpp`, copyright
  Eric Smith. The real board is a bit-slice ALU running microcode out of six
  PROMs; MAME simulates the arithmetic rather than the microcode, and so does
  this, which is why those PROMs are not needed here.

Copyright Mathis Rosenhauer, Eric Smith, Brad Oliver and the MAME team; used
under the BSD-3-Clause license:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

## The 6502 core

`core/m6502fast.h` was written for this project and is covered by the project
license. It is an instruction-stepped NMOS 6502 that passes Klaus Dormann's
functional test suite, decimal mode included.

## The POKEY

`core/pokey.c` was written for this project and is covered by the project
license. The audio side is in the spirit of Ron Fries' pokeysnd; the pot inputs
are modelled from the POKEY datasheet, because on this board they are not
paddles at all — they are how the game reads its buttons and its spinner.

## ROMs

No game ROMs are included in this repository, and none ever will be. Supplying
them is up to whoever builds the thing.
