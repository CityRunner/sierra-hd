# Third-party components

Every third-party component with its version and licence (spec section 14).
Vendored code lives in `third_party/` at a pinned commit or checksum,
together with its licence file.

## Vendored

None at milestone 0. Planned, with the pin recorded when each is added
(docs/ASSUMPTIONS.md, "Third-party components"):

| Component | Licence | Needed from |
|---|---|---|
| SDL3 | zlib | milestone 3a (window layer) |
| Nuked OPL3 | LGPL 2.1 or later | milestone 7 |
| mt32emu (Munt) | LGPL 2.1 or later | milestone 7 |

## Ported code

None yet. Code ported from ScummVM (GPL v3 or later) keeps its original
licence header and names its source file and the pinned revision.

## Tools (not distributed)

| Tool | Version | Licence | Use |
|---|---|---|---|
| LLVM (Clang, lld, clang-format, clang-tidy, libFuzzer, sanitizers) | 23.1.4 | Apache 2.0 with LLVM exception | compiler and analysis |
| ScummVM | pinned revision, see docs/ASSUMPTIONS.md | GPL v3 or later | behavioural reference, built under tools/ |
| Ubuntu 24.04 packages | release pocket | various | toolchain container |
