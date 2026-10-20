# Milestone 0: bootstrap

Status: **ready for review**; all exit criteria are met.

Spec: version 15. All decisions and assumptions are in `docs/ASSUMPTIONS.md`.

## What was done

1. **Inputs.** Asked the customer for the game folder, the ScummVM source
   and the repository hosting.
   - Repository: `github.com/CityRunner/sierra-hd` (private).
   - ScummVM: cloned from GitHub and pinned at
     `ed949213a23440447ea402e263daa871394b6bf9` (version `2026.4.1git`).
   - Game files: the customer's GOG copy, the English DOS 3.5" floppy
     release 1.000.046 with the loose patch files `SCRIPT.052` and
     `TEXT.052`; SHA-256 hashes are in `docs/ASSUMPTIONS.md`.
   - Accepted releases from ScummVM's detection tables: the English DOS 3.5"
     and 5.25" floppy releases, 1.000.046.
2. **CI runners and cost.** Researched availability and prices for private
   repositories: Linux and Windows arm64 standard runners are available in
   private repositories since 2026-01-29. Prices and per-run estimates are in
   `docs/ASSUMPTIONS.md`, "CI". Decision: Linux (x86-64 and ARM64) on every
   push; macOS and Windows on pull requests to `main` (the milestone gates)
   and on manual runs, which can be limited to one platform. The account is
   on GitHub Free (2,000 included minutes a month). A full run currently uses
   about 90 included minutes, a push about 16.
3. **Toolchain container** (`docker/`). Upstream LLVM 23.1.4, verified by
   SHA-256. Ubuntu 24.04 is pinned by digest to the release-day image, with
   packages from the frozen release pocket, so amd64 and arm64 get identical
   versions. The image includes qemu-user and an arm64 sysroot, SDL3 build
   dependencies, Xvfb and SDL2 for the reference ScummVM. A one-line fix is
   applied to the pre-release libstdc++ 14 header that Clang 23 rejects.
4. **Repository skeleton.**
   - The GPL v3 `LICENSE`, and ignore rules for game resources, ROMs and
     `reports/images/`.
   - The directories `docs/`, `tools/`, `tests/`, `third_party/`, `hints/`
     and `reports/`.
   - The ten library targets of spec section 4.1, each with a README. Their
     dependency graph is acyclic by construction, checked at configure time.
   - The `sierra-hd` program stub, which prints the libraries it links.
5. **Error-handling scheme.** The project builds without exceptions.
   Input-driven failures return `sci::core::Result<T>`
   (`std::expected<T, Error>`); bugs stop the program through `SCI_ASSERT`,
   which stays enabled in release builds. sci-core has the first pieces
   built on this scheme: the bounds-checked `ByteReader` and the
   floating-point profile check `check_fp_profile()`.
6. **Build rules.**
   - Floating-point profile (`-ffp-contract=off` everywhere, including
     clang-cl).
   - `-fwrapv` in release builds only.
   - Presets for ASan+UBSan, TSan, libFuzzer, Windows ASan, and ARM64 under
     qemu.
   - Warnings are errors.
7. **clang-format and clang-tidy.**
   - All clang-tidy checks are errors, and the cognitive-complexity limit is
     25.
   - Determinism bans: libm transcendentals, unstable sorts, `rand`, the
     `<random>` distributions and `std::hash`.
   - `scripts/check-repo-policy.py` rejects game data, images, `float`,
     `long double` and missing SPDX headers.
8. **CI** (`.github/workflows/ci.yml`).
   - Lint: format, policy and tidy.
   - Linux x86-64 and ARM64 × release, ASan+UBSan and TSan, in the toolchain
     image.
   - Fuzzing for 60 s per target.
   - macOS arm64 × three presets.
   - Windows x86-64 release and ASan, Windows ARM64 release.
   - Actions are pinned by SHA; actionlint reports no problems.
9. **Reference ScummVM** (`tools/reference-scummvm/`). Built in the toolchain
   container from the pinned revision, with patches from
   `tools/scummvm-patches/` (none yet) applied to an exported copy. Only the
   SCI engine is built, with no scalers and no aspect correction. `run.sh`
   writes an isolated 1x configuration without undithering.

## Test results

### CI on GitHub

Manual runs of the full workflow on `milestone-0`. The pull request runs it
once more.

| Platform | Jobs | Result |
|---|---|---|
| Linux x86-64 | lint (format, policy, clang-tidy), release, ASan+UBSan, TSan, fuzz (60 s) | pass |
| Linux ARM64 | release, ASan+UBSan, TSan | pass |
| macOS 15 (Apple silicon) | release, ASan+UBSan, TSan | pass |
| Windows x86-64 | release, ASan | pass, after one fix |
| Windows ARM64 | release | pass |

The first Windows ASan run failed at link time. CMake runs lld-link
directly, so the ASan runtime that clang-cl adds when it drives the link was
missing. It is now named explicitly (`cmake/SciCompilerFlags.cmake`).

Two CI fixes came out of this:

- Manual runs had cancelled push runs on the same branch, because they
  shared a concurrency group. The event name is now part of the group.
- Failing steps now publish the tail of their log as an error annotation.

### Local, in the toolchain container

From clean build directories:

| Check | Result |
|---|---|
| clang-format (`scripts/check-format.sh`) | pass |
| Repository policy (`scripts/check-repo-policy.py`) | pass |
| clang-tidy on all sources | pass, no findings |
| Linux x86-64 debug, release, ASan+UBSan, TSan | 2/2 tests pass in each |
| Linux ARM64 (cross build, run under qemu-user) | 2/2 tests pass |
| libFuzzer `fuzz-byte-reader`, 30 s | 1,115,048 runs, no crash |
| Reference ScummVM build | builds with no warnings; `--version` and `--list-engines` (SCI only) work; the launcher runs under Xvfb |

The two tests are `sci-core-tests` (8 test cases) and the corpus replay of
`fuzz-byte-reader`.

Negative checks, to confirm the guards work:

- UBSan stops on a signed overflow.
- ASan reports a heap-buffer-overflow.
- The policy check rejects a tracked `RESOURCE.001`.
- CMake rejects a dependency cycle (sci-pic on sci-vm).
- clang-tidy reports each banned function in a probe file.
- `check_fp_profile()` reports FMA contraction when built with
  `-ffp-contract=fast` on aarch64.

## Exit criteria

| Criterion | Status |
|---|---|
| The empty libraries build and pass the style checks and sanitizer jobs on every CI platform | Met (see the table above). |
| The reference ScummVM runs | Met. It builds with the pinned toolchain, detects the customer's GOG copy as `sci:laurabow` (English DOS 3.5" floppy, 1.000.046), and runs the game headless under Xvfb up to the opening copy-protection screen at 1x. The screen capture stays in the git-ignored `reports/images/`. |
| The decisions are recorded in docs/ASSUMPTIONS.md | Done. |

## Deviations from the spec

- None from section 2.
- SDL3, Nuked OPL3 and mt32emu are not vendored yet. They will be vendored
  when first needed (SDL3 at milestone 3a, the audio libraries at milestone
  7), with the pins recorded then (`docs/ASSUMPTIONS.md`, "Third-party
  components").
- Docker instead of podman (spec section 3.1 allows either).
- macOS and Windows CI run at the milestone gates only, as section 3.2
  provides.

## Open questions

1. **Copy protection in recordings.** The game opens with the fingerprint
   identification screen. The plan is for scenario recordings and the
   playthrough to answer it like a player would, in both engines. Please
   confirm, or say if you prefer another approach (to be settled before
   milestone 4).
2. **CI budget.** At the current size the Free plan's 2,000 minutes are
   enough. If the account has no payment method, CI stops when they run out.
   Each milestone report will give the measured use.
