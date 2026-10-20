# sci-resource

Access to the resources of an SCI0 game folder: the resource map, the
volumes and loose patch files, combined in ScummVM's order. Includes the
LZW and Huffman decompressors. The format is described in
`docs/formats/resources.md`.

The code is ported from ScummVM's resource loader
(`engines/sci/resource/`) at the pinned revision. Each ported file names its
source.

## Invariants

- Every resource decodes byte-identically to ScummVM's resource loader. This
  is checked with the game files by `tools/resource-compare`: 1068 of 1068
  resources of the tested release match.
- Game files are untrusted input:
  - every read is bounds-checked;
  - where ScummVM would read or write out of bounds, leave memory
    uninitialised or loop forever, the result is an error (or, for an LZW
    stream ended early, zero-filled output);
  - the parsers are fuzzed (`fuzz-decompress`, `fuzz-game-folder`).
- No file system access: the caller passes the folder's files as
  `GameFile`s.
- Only SCI0 is supported. Maps, volumes and compression methods of other SCI
  versions are rejected as `Unsupported`.
- Warnings (skipped map entries, applied patches) go to the `Resources` log
  category.
