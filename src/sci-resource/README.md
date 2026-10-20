# sci-resource

RESOURCE.MAP and the RESOURCE.xxx volume files: lookup, loading and decompression of SCI0 resources.

Milestone 0 contains only the library skeleton; the content arrives with the
milestones named below.

## Invariants

- Game files are untrusted input: every read is bounds-checked through `sci::core::ByteReader`, and the parsers have libFuzzer targets.
- Decoded resources are byte-identical to ScummVM's resource loader at the pinned revision (milestone 1).
- No file system access of its own: `app` supplies the bytes of the game files.
