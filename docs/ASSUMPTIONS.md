# Assumptions and decisions

Every assumption and decision made while implementing the task spec
(version 15), with the date and the milestone it belongs to. Decisions that
change section 2 of the spec ("Fixed decisions") are only made with the
customer's approval and say so.

Status markers: **decided** (final unless the customer objects),
**assumed** (to be confirmed), **pending** (waiting for input).

## Inputs and hosting (milestone 0, 2026-10-20)

- **Spec source.** The spec was read from the Claude Docs document
  "Task Spec: HD Colonel's Bequest (SCI0)", version 15. The attached file
  `colonels-bequest-hd-task-spec.md` was not present in the session. The
  document's tab title still says "Custom Rust SCI0 Interpreter"; the
  customer confirmed this is legacy naming and that C++23 (spec section 2)
  applies. **Decided.**
- **Repository.** `github.com/CityRunner/sierra-hd`, private. **Decided** by
  the customer.
- **ScummVM source.** Cloned from `https://github.com/scummvm/scummvm`, as
  agreed with the customer. Kept outside the repository. **Decided.**
- **Pinned ScummVM revision.** `ed949213a23440447ea402e263daa871394b6bf9`
  (master, committed 2026-10-20, version string `2026.4.1git`). This was the
  head of master when milestone 0 started. The same value is in
  `tools/reference-scummvm/REVISION`, which the build script checks.
  **Decided.** A later change of the pin is recorded here, with the reason.
- **Game folder.** **Pending:** the customer will provide the game files. Until
  then the tested release cannot be identified, and the reference ScummVM is
  only checked without a game (it starts the launcher).
- **Environment.** The agent works in a Linux x86-64 cloud container, not on a
  local machine (spec section 3.1). Docker is available (its daemon is started
  by hand in the session), so podman is not used; the customer allowed
  installing podman if needed, and it was not. Outbound HTTPS goes through a
  TLS-intercepting proxy; `docker/build-image.sh` supports that through
  `SIERRA_HD_EXTRA_CA` without affecting the image (see "Toolchain").

## Accepted game releases (milestone 0)

From the detection tables of the pinned ScummVM revision
(`engines/sci/detection_tables.h`, entries `laurabow`), the English DOS
releases are (spec section 10.2):

| Release | Game version | `resource.map` MD5, size |
|---|---|---|
| English DOS 3.5" floppy | 1.000.046 | `4e511f47d9893fa529d6621a93fa0030`, 8478 |
| English DOS 5.25" floppy | 1.000.046 | `b1905f6aa68ff65a057b080b1eae954c`, 12030 |

- The English DOS non-interactive demo, the Amiga and Atari ST releases, the
  Hebrew release and the fan translations are not accepted. **Decided** (spec
  sections 1.2 and 1.3).
- The tested release is the customer's copy; the other row is accepted with a
  warning. ScummVM's table identifies releases by the MD5 of the first 5000
  bytes of each file (the advanced detector's default) and the file size; the program will identify them by SHA-256 of whole files, with
  the hashes taken from the customer's copy (and, for the other release,
  recorded when a copy is available). **Assumed.** Until the other release
  can be hashed it is recognised by the ScummVM MD5 values above.

## Toolchain (milestone 0)

- **Compiler.** Upstream LLVM 23.1.4 (the latest release on 2026-10-20) on
  every platform, from the official release archives, verified by SHA-256:

  | Archive | SHA-256 |
  |---|---|
  | `LLVM-23.1.4-Linux-X64.tar.xz` | `25d814fda50d5fb0084b42be09605d8b1595d49d5e2e03a3345d7e112ff6f9f3` |
  | `LLVM-23.1.4-Linux-ARM64.tar.xz` | `9b27cf263d711aa6ebfd4045ab241d947bd09b3cd19ffa5728f48a80c9eb3a91` |
  | `LLVM-23.1.4-macOS-ARM64.tar.xz` | `5d3179db39adeca5b17a34d82708fe976bc60e055b05fe557878fb3fb64d39ef` |
  | `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz` | `854bffcfc84528e0ee92490ff5c178920abd358a79c3e98f053e41e3e8e4c6d2` |
  | `clang+llvm-23.1.4-aarch64-pc-windows-msvc.tar.xz` | `f0d2e32424850ffb9b6aef190587a8298353d858a8a7260f490c173fe98d6811` |

  **Decided.**
- **Container.** `docker/Dockerfile`, multi-architecture (amd64 and arm64).
  The base image is pinned by digest to `ubuntu:noble-20240423` (the 24.04
  release image). Packages come from the frozen `noble` release pocket only,
  which matches that image and gives identical package versions on both
  architectures. The Ubuntu snapshot service was considered, but it does not
  cover `ubuntu-ports` (arm64). The build container gets no security updates
  this way. That is acceptable for a build-only image that is not shipped.
  **Decided.**
- **Standard libraries.** Linux: libstdc++ from GCC 14 (`14-20240412`,
  release pocket). macOS: the SDK's libc++. Windows: the MSVC STL through
  clang-cl. All three implementations are in use, which is what spec
  section 5.2 guards against: output-affecting code uses sci-core's own
  facilities instead of those that differ. **Decided.**
- **libstdc++ fix.** The release-pocket libstdc++ has a typo in
  `<bits/unicode.h>` (`++this;` instead of `++*this;`) that Clang 19 and later
  reject whenever `<format>` is included. GCC 14.1 fixed it upstream. The
  Dockerfile applies the same one-line fix to the native and arm64 cross
  headers. **Decided.**
- **Other tools.** CMake 3.28.3 and Ninja 1.11.1 in the container. On macOS
  and Windows CI, Ninja 1.13.2 from the official release, verified by SHA-256,
  and the CMake preinstalled on the runner (not pinned; the project requires
  3.28 or later). Python 3.12 in the container. **Decided.**
- **ARM64 locally.** The amd64 image includes qemu-user and the Ubuntu arm64
  cross libraries. The `cross-aarch64` preset builds with
  `--target=aarch64-linux-gnu` and runs the tests under qemu. **Decided.**
- **Proxy support.** `docker/build-image.sh` passes an extra CA bundle as a
  BuildKit secret, so it never reaches an image layer, and switches the
  Ubuntu archive to HTTPS. Without `SIERRA_HD_EXTRA_CA` the image builds over
  plain HTTP. Integrity is still guaranteed by the archive's GPG signatures
  and the LLVM SHA-256. **Decided.**

## Build flags (milestone 0)

- C++23 without extensions. Clang only; configuring with another compiler
  fails. **Decided** (spec section 2).
- Floating-point profile (spec section 5.3): `-ffp-contract=off` on every
  target, including clang-cl, where `/fp:precise` would otherwise enable
  contraction, and no fast-math. On x86-64, SSE2 is the baseline; x87 is
  excluded by `static_assert(FLT_EVAL_METHOD == 0)`. A run-time check,
  `sci::core::check_fp_profile()`, covers rounding mode, FTZ, DAZ and FMA
  contraction. A deliberate `-ffp-contract=fast` build on aarch64 makes the
  check fail as intended. **Decided.**
- `-fwrapv` in Release and RelWithDebInfo builds without sanitizers, and
  never in sanitizer builds (spec section 5.2). **Decided.**
- Sanitizers: ASan+UBSan and TSan on Linux and macOS, ASan on Windows x86-64,
  all with `-fno-sanitize-recover=all`. The LLVM release ships no ASan runtime
  for Windows on ARM64, so that job runs the release build only. **Decided.**
- Standard-library assertions (`_GLIBCXX_ASSERTIONS`, libc++ extensive
  hardening) in Debug and sanitizer builds. **Decided.**
- Linux uses lld; macOS uses the Xcode linker; Windows uses lld-link.
  **Decided.**
- C++ module scanning is off: the project does not use modules. **Decided.**

## Error handling (milestone 0)

**Decided** (spec section 5.1 asks for one scheme, chosen at milestone 0):

- The project is built without exceptions (`-fno-exceptions`, or `/EHs-c-`
  with `_HAS_EXCEPTIONS=0` for clang-cl), in every library, tool and test.
  This follows ScummVM, which also has no exceptions, so ported code keeps
  its shape.
- Operations that can fail on their input (parsers, loaders, file access in
  app) return `sci::core::Result<T>`, which is `std::expected<T, Error>`.
  `Error` holds an `ErrorCode` (`InvalidData`, `Unsupported`, `NotFound`,
  `Io`) and a message.
- Broken invariants (bugs) are `SCI_ASSERT`. It stays enabled in release
  builds, calls a handler installed by app for logging, and aborts.
- Allocation failure aborts, as it does in any build without exceptions.
- Where ScummVM calls `error()` for bad game data, the port returns an
  `Error`. Where it calls `error()` for an internal inconsistency, the port
  uses `SCI_ASSERT`. Each such call is classified when it is ported.

Details: docs/ARCHITECTURE.md, "Error handling".

## Style and static analysis (milestone 0)

- clang-format: LLVM-based style, 4-space indentation, 120 columns
  (`.clang-format`). **Decided.**
- clang-tidy: the bugprone, cert, clang-analyzer, concurrency,
  cppcoreguidelines, misc, modernize, performance, portability and
  readability groups, with every finding treated as an error. Disabled checks
  are listed with their reasons in `.clang-tidy`. The cognitive-complexity
  limit is 25 per function. **Decided.**
- Determinism bans through `bugprone-unsafe-functions`: libm transcendental
  functions, unstable sorts (`std::sort`, `std::ranges::sort` and related),
  `rand`, the `<random>` distributions and `std::hash`. A probe file confirmed
  that each one is reported. `float` and `long double` are rejected by
  `scripts/check-repo-policy.py`, because clang-tidy has no check for types.
  **Decided.**
- Naming: types `PascalCase`; functions and variables `snake_case`; private
  members with a trailing `_`; macros `SCI_UPPER_CASE`. **Decided.**
- Every source file starts with an SPDX identifier (`GPL-3.0-or-later` for
  project code). Ported ScummVM files will keep their original header, plus
  the source path and the pinned revision (spec section 5.1). **Decided.**

## Testing (milestone 0)

- Unit tests use a minimal in-house harness (`tests/support`, about 80
  lines) rather than a vendored framework. Spec section 2 prefers fewer
  external dependencies, and the tests need only registration and checks.
  **Decided.** If the tests outgrow it, a vendored framework will be
  proposed.
- Fuzz targets are libFuzzer entry points. In fuzz builds (Linux x86-64 CI)
  they run under libFuzzer. In every other build they are linked to a small
  replay driver and run their seed corpus as a CTest test, so all platforms
  and sanitizers exercise them. **Decided.**

## CI (milestone 0)

- **Availability.** GitHub announced on 2026-01-29 that standard arm64
  runners (`ubuntu-24.04-arm`, `windows-11-arm`) are available in private
  repositories and count against the plan's minutes. macOS on Apple silicon
  (`macos-15`) and x86-64 Linux and Windows were already available.
  **Assumed:** this could not be checked against the account, because the
  session has no API access to the repository's settings or billing.
- **Prices** (per minute for private repositories, after GitHub's price cut of
  2026-01-01, from public pricing summaries; to be confirmed on the account's
  billing page): Linux x64 2-core $0.006, Linux arm64 2-core $0.005, Windows
  2-core $0.010, macOS (M1, 3-core) $0.062. GitHub Free includes 2,000
  minutes a month for private repositories, Pro and Team 3,000. Historically,
  included minutes were consumed at multipliers (Windows 2x, macOS 10x).
- **Estimated use per run** (milestone 0 size; it grows with the code):

  | Workflow part | Jobs | Minutes each (cached) | Cost if billed |
  |---|---|---|---|
  | Linux x64: lint, 3 presets, fuzz | 5 | about 5 | about $0.15 |
  | Linux arm64: 3 presets | 3 | about 5 | about $0.08 |
  | macOS: 3 presets | 3 | about 6 to 8 | about $1.10 to $1.50 |
  | Windows x64: 2 presets, arm64: 1 | 3 | about 8 to 10 | about $0.25 to $0.30 |

  The first run on each architecture builds the toolchain image (about 10
  minutes) or downloads LLVM (about 5 minutes); later runs use the cache.
- **Decision.** Linux x86-64 and ARM64 run on every push. macOS and Windows
  run on pull requests to `main` (the milestone gates) and when started by
  hand. This is the option spec section 3.2 provides for. **Decided**, unless
  the customer prefers every push.
- Actions are pinned by commit SHA. The workflow is checked with actionlint
  1.7.12.
- Linux jobs run inside the toolchain image. It is built per runner
  architecture, with its layers in the GitHub Actions cache.
- TSan jobs set `vm.mmap_rnd_bits=28` on the runner. TSan's memory layout does
  not fit the default ASLR entropy of the current runner kernels.

## Third-party components (milestone 0)

- SDL3, Nuked OPL3 and mt32emu are vendored when they are first needed:
  SDL3 at the end of milestone 3a (the window layer), Nuked OPL3 and mt32emu
  at milestone 7. Their pins are recorded here and in THIRD_PARTY.md at that
  point. Milestone 0 has no third-party code: its exit criterion is about
  empty libraries, and vendoring early would only add unused code.
  **Assumed:** this does not change the fixed decision itself (spec section 2).
- `LICENSE` is the GPL v3 text from ScummVM's `COPYING` at the pinned
  revision. Its SHA-256 `3972dc97…986` is that of the canonical
  `gpl-3.0.txt`. gnu.org is not reachable from the session. **Decided.**

## Reference ScummVM (milestone 0)

- Built by `tools/reference-scummvm/build.sh` inside the toolchain container,
  with the same compiler as the project. The pinned revision is exported to
  the build directory and patches are applied there, so the checkout stays
  read-only. Configure options: `--disable-all-engines --enable-engine=sci`,
  no scalers (`--disable-scalers`, `--disable-hq-scalers`,
  `--disable-edge-scalers`), `--disable-aspect`, no cloud, network, speech or
  FluidSynth; debug symbols with optimisations. MT-32 emulation and Nuked OPL
  stay enabled for milestone 7. **Decided.**
- The reference uses SDL 2.30.0 from Ubuntu. This is independent of the
  project's SDL3 and only affects presentation, which is never compared.
  **Decided.**
- Run-time settings (`tools/reference-scummvm/run.sh`): `gfx_mode=normal`,
  `scale_factor=1`, `aspect_ratio=false`, `filtering=false`,
  `disable_dithering=false` (no EGA undithering), `music_driver=null` (until
  milestone 7), and an isolated configuration and save directory.
  **Decided.**
- Comparisons go through buffer dumps and traces (patches in
  `tools/scummvm-patches/`), never through screenshots of the scaled window
  (spec section 6.2).

## Spec items carried forward

- Edge coverage is blended in the same sRGB-encoded space as the pairs, with
  no linear-light conversion (spec section 6.5). **Assumed**; to be
  confirmed after seeing results (spec section 15).
- ScummVM's Event Recorder is evaluated as the basis of the replay format
  before that format is designed (spec section 7, needed from milestone 4a).
  **Pending.**

## Milestone 0 placeholders

- Each library except sci-core contains only a `component_name()` function,
  which the program's `--version` output uses to show that everything links.
  These functions are removed as soon as the library has real content.
