# third_party

Vendored dependencies, each in its own directory at a pinned commit or
checksum, with its licence file and a note on where it came from. See
THIRD_PARTY.md for the list. They are built by `cmake/SciThirdParty.cmake`.

- `SDL3/`: SDL 3.4.18 (zlib), from milestone 3a; see `SDL3/VENDORED.md`.
- `nuked-opl3/`: Nuked OPL3 1.8 at commit `765ec96` (LGPL 2.1 or later),
  from milestone 7; see `nuked-opl3/VENDORED.md`.
- `mt32emu/`: libmt32emu 2.8.3 from the Munt project, tag
  `libmt32emu_2_8_3` (LGPL 2.1 or later), the synthesiser core only, from
  milestone 7; see `mt32emu/VENDORED.md`.
