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
- **Game folder.** The customer's copy is the GOG release, uploaded to the
  session on 2026-10-20. Only the game files were extracted (the GOG
  installer's `tmp/` folder was left out), to a read-only directory outside
  the repository: `/home/user/game/colonels-bequest-gog` in the agent's
  container. It is mounted read-only into the toolchain container, for
  example `SIERRA_HD_MOUNTS=/home/user/game/colonels-bequest-gog:/game`.
  **Decided.**
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
- **Tested release** (the customer's GOG copy, 2026-10-20): the English DOS
  3.5" floppy release, game version 1.000.046. All five ScummVM detection
  MD5s and sizes match, and the reference ScummVM identifies it as
  `sci:laurabow`, "Laura Bow I: The Colonel's Bequest (DOS/English)".
  `RESOURCE.CFG` selects `EGA320.DRV` and `MT32.DRV`. **Decided.**
- **Patch files.** The GOG folder also contains the loose patch files
  `SCRIPT.052` (header `0x82`) and `TEXT.052` (header `0x83`). They are not
  part of ScummVM's detection entry. ScummVM loads them over the volume
  copies ("Patching SCRIPT.052 - OK", "Patching TEXT.052 - OK"), so they are
  part of the tested input. This project's resource loader must apply patch
  files exactly as ScummVM does (milestone 1). **Decided.** Whether other
  copies of the 3.5" release carry the same patches is unknown; a copy
  without them is accepted, with a warning, as the same release.
- **SHA-256 of the tested release.** The program identifies the game by these
  hashes (spec section 10.2); hashes are not game data:

  | File | Size | SHA-256 |
  |---|---|---|
  | `RESOURCE.MAP` | 8478 | `47c3463102b15034a0ba7d561c8c1c2de9838f05b8ecbc286961bdea862ba13b` |
  | `RESOURCE.001` | 515788 | `898b89ad5ca9474cab37a8bf4fe8f22016700870fcc3eceb6953ae9e39cf4eb8` |
  | `RESOURCE.002` | 721381 | `db1fa10ce5e438774fb44aef9c69eaa22ba82a403b05052533b9b5b11863a819` |
  | `RESOURCE.003` | 667468 | `e114602aeb7e6b25ba4471bcbc1d15778841b4e4cf0c15d11fc4f58b491551f3` |
  | `RESOURCE.004` | 683807 | `fcf2d7252dfd06b8ed8ca701128023fe404ad52c066b7548eec0eda3840cf762` |
  | `SCRIPT.052` | 6962 | `4f8a42dbba46fe3b7156b90fdca8f0c526d3509fed623425983c072fda5254c7` |
  | `TEXT.052` | 2734 | `7b8e4af2d9101764dbdd790988b892a0477297578cf02dd9c3d1100a3cc4bd73` |
  | `SCIV.EXE` | 76419 | `cbbb9035ac64a8e24ba5cb6a334d0e7208164aa43f359fd7b244b87d87e9fa98` |
  | `ADL.DRV` | 8997 | `522259dcd9026919698fd2a40e65e9676147382e445e9955128606bc691e011b` |
  | `MT32.DRV` | 3162 | `15b2a5208c430f43b2583e642c5c286ea6f09acc920848eaf71776547f0352d1` |
  | `EGA320.DRV` | 1952 | `1c134ec896509f243a5dcc85e8f51389649a0001f27d841a015aa26016c7993e` |

- **Other accepted release.** The 5.25" floppy release in the table above is
  accepted with a warning. No copy is available, so it is recognised by the
  ScummVM MD5 values (first 5000 bytes and size) until one can be hashed.
  **Assumed.**
- **Copy protection.** The game opens with the fingerprint identification
  screen; the reference ScummVM shows it unscaled at 1x after start-up. Both
  engines run it as game logic. Scenario recordings and the playthrough
  answer it like a player would. **Decided** (approved by the customer on
  2026-10-20).

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

- **Plan.** The repository belongs to a GitHub Free personal account
  (confirmed by the customer, 2026-10-20). For private repositories, that
  means 2,000 included minutes a month. Once they are used up, jobs stop
  unless a payment method and budget are set.
- **Availability.** GitHub has offered standard arm64 runners
  (`ubuntu-24.04-arm`, `windows-11-arm`) in private repositories since
  2026-01-29. All of them ran on this repository: Linux x86-64 and ARM64,
  macOS 15 on Apple silicon, and Windows x86-64 and ARM64.
- **Prices.** Per-minute list prices for private repositories after GitHub's
  price cut of 2026-01-01, from public pricing summaries (the account's
  billing page is not reachable from the session): Linux x64 2-core $0.006,
  Linux arm64 2-core $0.005, Windows 2-core $0.010, macOS (M1, 3-core)
  $0.062. Included minutes have historically been consumed at multipliers:
  Windows 2x, macOS 10x.
- **Measured use** (milestone 0; it grows with the code). Job time is rounded
  up to whole minutes.
  - A push runs 8 Linux jobs of 1 to 2.5 minutes each, with the image cached:
    about 16 included minutes.
  - A full run (gate) adds 3 macOS jobs and 3 Windows jobs of about 2
    minutes each: about 16 + 60 + 12 = 90 included minutes, or about $0.50
    at list prices.
  - The first run on each architecture builds the toolchain image (4 to 9
    minutes per job).
  - At this size the 2,000 minutes cover roughly 100 pushes plus a few
    gates a month. The numbers in each milestone report show how this
    develops.
- **Decision.** Linux x86-64 and ARM64 run on every push. Windows runs on
  pull requests to `main` (the milestone gates) and on manual runs.
  **Decided.**
- **macOS is optional** (customer decision, 2026-10-20, at the milestone 0
  approval: "make macOS optional and concentrate on Windows and Linux for
  now"). The macOS jobs run only when a manual run selects `macos`. They are
  no longer part of the milestone gates. This saves about 60 of the roughly
  90 included minutes a gate costs, and avoids the macOS runner queues seen
  on 2026-10-20. macOS stays a supported platform (spec section 1.2). The
  customer can ask for a macOS run at any gate. **Decided**, approved by the
  customer as a deviation from spec section 3.2.
- Push, pull request and manual runs are in separate concurrency groups, so
  they do not cancel each other.
- The tail of a failed step's log is published as an error annotation. It is
  readable in the run summary and through the checks API. The session cannot
  download full job logs, because they are served from a host the session
  proxy blocks.
- clang-cl ASan builds link the ASan runtime explicitly
  (`cmake/SciCompilerFlags.cmake`). CMake runs lld-link itself, so the
  libraries that clang-cl would add were missing; this was found by the
  first Windows run.
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

## Milestone 0 approval (2026-10-20)

Milestone 0 was approved and merged
([CityRunner/sierra-hd#1](https://github.com/CityRunner/sierra-hd/pull/1)),
together with the copy-protection approach and the change to make macOS CI
optional (both above).

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
