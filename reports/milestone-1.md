# Milestone 1: resources

Status: **ready for review**. All exit criteria are met.

Exit criterion (spec section 12): every resource decodes byte-identically to
ScummVM's resource loader. The check goes through the reference tooling, and
the reason for the tooling chosen is recorded in docs/.

## What was done

1. **sci-resource**, ported from ScummVM's resource loader
   (`engines/sci/resource/`) at the pinned revision. Every ported file names
   its source.
   - Resource types and ids, with ScummVM's SCI0 type table, names and
     patch-file suffixes.
   - The resource map: ScummVM's SCI0 format checks and parsing. The first
     entry for an id wins, and entries beyond their volume are skipped.
   - Volumes: the SCI0 format check, entry headers and compression methods.
   - The SCI0 LZW and Huffman decompressors. They keep ScummVM's read-ahead,
     so that every decode, successful or not, has the same outcome.
   - Patch files, with ScummVM's naming rules, quirks and order.
   - `ResourceManager`: combines the map, volumes and patch files in
     ScummVM's order. It does no file I/O; the caller passes the files.
2. **Logging facility** in sci-core (spec section 5.1): levels, categories
   and a sink installed by app or a tool.
3. **Reference tooling:**
   - `tools/scummvm-patches/0001-dump-resources.patch` makes the reference
     ScummVM dump every resource through its own resource manager.
   - `tools/resource-dump` writes the same layout from sci-resource.
   - `tools/resource-compare` compares the two dumps.

   The choice of a patched ScummVM over a harness, and the reason, are in
   `docs/ASSUMPTIONS.md`, "Milestone 1".
4. **Tests:**
   - 6 sci-resource test files with 28 test cases on synthetic data. They
     include LZW round trips through every code width and a full table,
     using a reference encoder written for the tests.
   - A test for the logging facility.
   - Two new fuzz targets, `fuzz-decompress` and `fuzz-game-folder`, with
     generated seed corpora.
5. **Docs:**
   - `docs/formats/resources.md` describes the file formats.
   - The sci-resource README, `docs/ARCHITECTURE.md` ("Resources",
     "Logging"), `docs/ASSUMPTIONS.md`, THIRD_PARTY.md (ported files) and
     the tool READMEs are updated.

## Test results

### Comparison with ScummVM (local, game files of the tested release)

| Build of `sci-resource-dump` | Result |
|---|---|
| Linux x86-64 release | 1068 of 1068 resources identical, manifests identical |
| Linux x86-64 ASan+UBSan | 1068 of 1068 identical, no sanitizer reports |
| Linux ARM64 (cross build under qemu-user) | 1068 of 1068 identical |

- The resources split into 364 views, 88 pics, 244 scripts, 227 texts, 119
  sounds, 9 vocabs, 8 fonts, 3 cursors and 6 patch resources.
- Two of them come from the patch files `SCRIPT.052` and `TEXT.052`, the
  other 1066 from the volumes.
- ScummVM fails on none of them.
- The map has 1412 entries, duplicates included. They use all three SCI0
  methods: 1270 LZW, 96 Huffman, 46 stored.

As a negative check, one byte changed in one dump file is reported as a
difference.

### Unit tests, fuzzing and style (container; Linux CI on every push)

| Check | Result |
|---|---|
| clang-format, repository policy, clang-tidy | pass, no findings |
| Linux x86-64 debug, release, ASan+UBSan, TSan | 5/5 tests pass in each |
| Linux ARM64 (qemu) | 5/5 tests pass |
| libFuzzer, 60 s per target | `fuzz-byte-reader` 2.47 M runs, `fuzz-decompress` 0.29 M, `fuzz-game-folder` 1.07 M; no crashes, hangs or leaks |

The 5 tests are `sci-core-tests`, `sci-resource-tests` and the corpus
replays of the three fuzz targets. The CI results for this pull request are
on the pull request: Linux on every push, Windows on the pull request, macOS
only on request.

Fuzzing found one hang before the fix: a Huffman node linking to itself,
on which ScummVM also loops forever. Such trees are now rejected (see
below).

## Deviations from the spec

- None from section 2.
- Only SCI0 is supported; other formats are rejected as unsupported. Spec
  section 1.3 puts other SCI versions out of scope.
- Where ScummVM's behaviour is undefined or non-deterministic, the port
  returns an error, or zero-fills an LZW stream that ends early. Such data
  does not occur in the tested release. The full list is in
  `docs/ASSUMPTIONS.md`, "Milestone 1".

## Corrections to milestone 0

- 113 fuzzer-generated inputs had been committed to
  `tests/fuzz/corpus/byte_reader` by a manual libFuzzer run. They were
  synthetic data, not game data, and are removed.
- The check scripts now also cover new files and skip deleted ones.
- The fuzz runner reports any input that runs longer than 10 s.

## Open questions

None. Milestone 2 (path 1 rasteriser, views and fonts at 1x, room viewer)
needs a second reference patch for buffer dumps; it will follow the same
pattern.
