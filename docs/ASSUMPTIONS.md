# Assumptions and decisions

Every assumption and decision made while implementing the task spec
(version 15), with the date and the milestone it belongs to. Decisions that
change section 2 of the spec ("Fixed decisions") are only made with the
customer's approval and say so.

Changes the customer made to the spec itself are recorded in
`docs/SPEC_ADDENDA.md`. **A1** (2026-10-21) adds milestone 3b and section
6.10 (drawing order, occlusion and stroke integrity), amends sections 6.4,
6.7 and 12, and moves release v0.1 to after milestone 3b; see "Addendum A1"
below. **A2** (2026-10-21) adds section 6.11 (sprite undithering) and
milestone SU, a separate gate between milestones 6 and 7, and amends
sections 1.1, 2 (the output decision, approved by the customer), 6.5 and
12; it is implemented at milestone SU, not before.

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
  **Superseded** on 2026-10-21 by the CI cost policy below.
- **macOS is optional** (customer decision, 2026-10-20, at the milestone 0
  approval: "make macOS optional and concentrate on Windows and Linux for
  now"). The macOS jobs run only when a manual run selects `macos`. They are
  no longer part of the milestone gates. This saves about 60 of the roughly
  90 included minutes a gate costs, and avoids the macOS runner queues seen
  on 2026-10-20. macOS stays a supported platform (spec section 1.2). The
  customer can ask for a macOS run at any gate. On 2026-10-21 the CI cost
  policy below put macOS back into every gate and release; on 2026-10-22
  the customer made it optional again (see "Platform priorities" under the
  CI cost policy).
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

## CI cost policy (2026-10-21)

The customer's policy, verbatim in intent: minimise CI minutes without
weakening any check the spec requires. CI is for what cannot be checked
on the local machine. Everything that can run locally runs locally first;
CI confirms, it does not discover. **Decided** (customer instruction).

### The rules

1. **Local first.**
   - Before every push, `scripts/pre-push.sh` passes locally. It runs, in
     the toolchain image:
     - the build, clang-format and clang-tidy;
     - the unit tests in release, ASan+UBSan and TSan;
     - short fuzzing (30 s per target);
     - the ARM64 build and all tests under qemu-user, with the golden
       hashes of synthetic scenes. This build leaves out the program
       (`SCI_BUILD_APP=OFF`), because the cross sysroot has no ARM64
       window-system libraries for SDL3; the native ARM64 jobs at gates
       build it.
   - Long fuzzing and the golden-hash and scenario runs with game files
     stay local.
   - Pushes come in batches of meaningful progress, not after every commit.
2. **Tiered triggers** (`.github/workflows/ci.yml`):
   - **Push to a work branch:** no CI. A manual run (`workflow_dispatch`,
     scope `quick`) gives one fast Linux x86-64 job (lint, then release
     build and tests) if a remote check is useful.
   - **Draft pull request (work in progress):** Linux x86-64 only, one job
     after the other, so the first failure stops the run:
     1. style and static analysis;
     2. build and all tests under ASan+UBSan in one job;
     3. TSan for the threaded tests only (CTest label `threads`,
        `tests/threads`).
   - **Pull request ready for review (the milestone gate):** the full matrix
     the spec requires, all jobs at once so every failure shows:
     - Linux x86-64 and ARM64 in release, ASan+UBSan and TSan. Every preset
       runs the golden hashes of synthetic scenes, so the cross-architecture
       comparison is against the same constants.
     - The fuzzers, 60 s per target.
     - Windows x64 in release and ASan, and Windows ARM64 in release.
     - macOS is optional and not part of the gate (2026-10-22, below).
   - **Release binaries** (`release.yml`): only for a pushed version tag,
     on Linux and Windows. A manual run on a tag with `macos` builds the
     macOS package as an artifact.
   - **No scheduled runs.**
3. **Avoiding wasted runs.**
   - A newer commit cancels the in-progress run of the same pull request. A
     draft run is cancelled as a whole. In a gate run, cancellation is per
     job, so a push that changes only documents cannot cancel a gate matrix
     that is still running.
   - Each commit is tested once: push events start nothing, and pull
     request events do the testing.
   - **Path filters.**
     - A pull request that changes only `docs/`, `reports/` or Markdown
       files starts nothing.
     - A push to an open pull request is checked by the `plan` job, which
       compares the push with the commit before: one rounded minute.
       Documents only: nothing more runs. `hints/` only: only the hint-file
       check runs, on the runner's own Python, without the toolchain image.
   - **Fail fast.** Format and lint come first, then the build, then the
     tests: work-in-progress runs stop at the first failure. Gate runs run
     the whole matrix (`fail-fast: false`, no dependencies between jobs).
   - **Re-runs** use "re-run failed jobs" (`gh run rerun --failed`), never
     the whole workflow.
   - **Timeouts.** Every job has one, sized to its measured duration plus a
     margin. A first run after a toolchain change rebuilds the image, which
     takes 4 to 9 minutes.
     | Job | Timeout (min) |
     |---|---|
     | plan, hint files | 5 |
     | Linux release, TSan | 15 |
     | Linux ASan | 20 |
     | lint | 25 |
     | fuzz | 30 |
     | Windows x64 | 30 |
     | Windows ARM64 | 20 |
     | macOS | 45 |
     | release jobs | 20 to 25 |
4. **Each minute cheaper.**
   - **ccache:**
     - In the toolchain image (pinned Ubuntu package) for the Linux jobs,
       through `docker/run.sh` with `SIERRA_HD_CCACHE=1`.
     - From pinned ccache 4.14.2 release archives (SHA-256 recorded in the
       workflow) for Windows and macOS.
     - Each job kind keeps its own cache: the newest is restored, and each
       run saves its own. GitHub evicts old entries beyond the repository's
       10 GB.
     - SDL3, the only built third-party library, goes through ccache as
       well. Its sources are vendored at a pinned version, so their objects
       hit unless the flags change.
     - With ccache, clang-cl writes debug information into the objects
       (`/Z7` instead of `/Zi`), because ccache cannot cache a shared PDB.
   - **The toolchain image** is cached in the GitHub Actions cache per
     architecture, as before.
   - **Fuzzing in CI:** 60 s per target, with the grown corpus cached
     between runs.
   - **Artifacts:**
     - Test outputs are uploaded only when a job fails: the log tail as an
       annotation, and the fuzz crash inputs, kept 7 days.
     - Release packages are kept 3 days; the GitHub release holds them
       after that.
5. **Runners.**
   - Windows runs only at gates and for release tags, macOS only on manual
     runs. Between gates, ARM64 is covered by qemu-user locally.
   - When a problem reproduces only on macOS, Windows or native ARM64, a
     manual run of that one platform (scope `macos`, `windows` or
     `linux-arm64`) is allowed between gates. The milestone report says so.
6. **Never at the cost of the project.**
   - A milestone is never approved without a green full matrix at its gate.
   - No check from the spec is dropped or weakened to save minutes. If the
     budget looks insufficient, I stop and ask instead of cutting checks.
7. **Budget and reporting.**
   - Every milestone report has a section "CI minutes": the minutes used,
     by platform, measured with `scripts/ci-minutes.py --branch <branch>`,
     and what could reduce them further.
   - The script sums each job's time rounded up to whole minutes, as
     GitHub bills it. The account's billing page is the authority; the
     session cannot reach it.

### A CI defect found while applying the policy

- **Linux jobs never failed on a build, lint or test error** from
  milestone 0 to 4b.
  - Their steps ran `docker/run.sh ... | tee build.log` under GitHub's
    default Linux shell (`bash -e`), which does not set `pipefail`, so a
    step took `tee`'s status.
  - The Windows and macOS jobs, which name `shell: bash` (that adds
    `-o pipefail`), were not affected.
- **Found by the first local run of `scripts/pre-push.sh`.** clang-tidy
  reported a function of milestone 4b's script scanner
  (`src/sci-vm/src/scan.cpp`) over the complexity limit, while the CI lint
  job had passed on the same commit.
- **Fixed.** Both workflows now set `shell: bash` as the default.
  **Decided.**
- **Earlier Linux results are confirmations only of what ran locally.**
  The reports of milestones 0 to 4a list local runs of the builds, tests
  and sanitizers, which failed properly; the lint findings were checked
  locally only on the files changed. The first gate under the fixed
  workflows confirms the whole tree.

### Rates

GitHub-hosted standard runners for private repositories, per minute, since
2026-01-01. These come from public pricing summaries, because GitHub's
documentation is not reachable from the session; to be confirmed on the
account's billing page.

| Runner | List price per minute | Included minutes used per minute |
|---|---|---|
| Linux x64, 2-core | $0.006 | 1 |
| Linux arm64, 2-core | $0.005 | 1 |
| Windows x64 and arm64, 2-core | $0.010 | 2 |
| macOS (M1, 3-core) | $0.062 | 10 |

The account (GitHub Free) includes 2,000 minutes a month for private
repositories. The included-minute multipliers are the historical ones,
also to be confirmed on the billing page. No platform fee for self-hosted
runners applies as of October 2026; GitHub announced one and then
postponed it.

### Measured before the policy

`scripts/ci-minutes.py`, all 103 runs of 2026-10-20 and 21, from milestone
0 to the 4b gate:
- 3,024 runner minutes, or 3,859 included minutes, at about $22 at list
  prices;
- Linux 2,693 minutes; Windows 268 minutes (536 included); macOS 63 minutes
  (630 included, at milestone 0 only).

Most of it was push runs: every push ran 8 Linux jobs, often cancelled
part-way by the next push. That exhausted the month's 2,000 included
minutes within two days.

### Estimates under the policy

Estimated from the measured job durations, before ccache's savings:

| Run | Linux min | Windows min | macOS min | Included min | List price |
|---|---|---|---|---|---|
| Push | 0 | 0 | 0 | 0 | $0 |
| Draft run (plan, lint, ASan, TSan threads) | about 21 | 0 | 0 | about 21 | about $0.13 |
| Gate (full matrix) | about 52 | about 17 | about 15 | about 236 | about $1.40 |
| Release tag | about 11 | about 10 | about 5 | about 81 | about $0.48 |

- **Gate breakdown:** plan 1, lint 10, six Linux preset jobs 25, fuzz 16;
  Windows x64 12 and ARM64 5; macOS three presets in one job.
- **macOS dominates:** about 150 of the 236 included minutes of a gate.
- **Per month:**
  - The 2,000 included minutes cover about 8 gate runs, or about 5 gate
    runs and 30 draft runs.
  - A milestone has needed one to three gate runs: the first, and runs
    after review fixes.
  - At the pace of the first two days (several milestones a day), the
    included minutes run out within days.
  - At list prices, the overage is small: about $1.40 a gate run.
  - So a spending limit is the deciding control, not the included minutes.
    **Pending:** the amount of the spending limit (the customer accepted
    the estimates at the 4b gate). Once the repository is public, its
    standard GitHub-hosted runners cost nothing, so the limit then only
    guards the private-repository case. The customer sets it (Settings, Billing and
    licensing, Budgets and alerts: an Actions budget that stops usage at
    the limit). The session cannot reach the billing settings.

### Platform priorities (customer decision, 2026-10-22)

"As macOS is optional, concentrate on Linux as first and Windows as second
priority." **Decided**, replacing macOS at every gate:
- Linux comes first, then Windows. A gate is green when its Linux and
  Windows jobs are, and work on a platform problem goes to Linux first.
- macOS returns to the milestone 0 decision: optional, run only on a
  manual run (CI scope `macos`, or the release workflow with `macos`). It
  stays a supported platform (spec section 1.2), and the customer can ask
  for a macOS run at any gate.
- A problem found on macOS is still fixed when it is cheap and cannot hurt
  Linux or Windows. For example, `std::ranges::iota` was replaced because
  the libc++ of Apple's SDK lacks it, found by the 4b gate's macOS job.
- **Known macOS gaps** (from the 4b gate's macOS job, built with `-k 0`),
  left until macOS is wanted:
  - the libc++ of Apple's SDK (Xcode 16.4) has no `std::jthread`, used by
    `ThreadExecutor`;
  - it has no `std::chrono::zoned_time`, used for the host's local time in
    `main.cpp`.
  Both can be replaced with `std::thread` and `localtime_r` when macOS is
  taken up.
- The estimates above drop the macOS share: a gate is about 52 Linux and
  17 Windows minutes, about 86 included minutes or about $0.50 at list
  prices for a private repository. Since 2026-10-22 the repository is
  public, so its standard GitHub-hosted runners cost nothing.

### Proposals (accepted by the customer at the 4b gate; not set up)
 The self-hosted runners below need
a machine of the customer's and are set up only when the customer provides
one; for a public repository they are not recommended (anyone can open a
pull request whose code would run on them).

- **A self-hosted Linux runner** would make the Linux jobs free (no
  platform fee as of October 2026).
  - This session's machine is not suitable. It is an ephemeral cloud
    container that is reclaimed after inactivity, and its Docker daemon
    stopped once during this milestone.
  - A machine of the customer's that is always on (a Linux PC, a VM or a
    small server with Docker) would do. The jobs run unchanged in the
    toolchain image.
  - It would serve the Linux jobs only; Windows and the native ARM64
    gate jobs stay GitHub-hosted.
  - A private repository with a single owner keeps the usual risk of
    self-hosted runners, code from strangers' pull requests, out of
    reach.
- **A self-hosted macOS runner** on the customer's Mac, if there is one,
  would remove the largest share of every gate: about 150 of 236 included
  minutes.

## Small agent tasks (2026-10-23)

The customer's rule, after two milestone 6b runs stopped at a usage limit
for about three hours each: work is split into tasks small enough to
finish between limits, so that each limit costs at most one small task.

- **One task, one result.** An agent gets one part of a milestone (for
  example the recording in sci-vm, one comparison, one set of scenarios,
  one check), not the whole milestone. It ends with commits and a short
  report; the next part is a new task that starts from those commits.
- **Size.** A task should take well under an hour, checks included. A part
  that would take longer is split again.
- **Long checks are split too.** Scenario runs, the every-N check and the
  pre-push checks run in pieces (a few scenarios, one N or one step of
  `scripts/pre-push.sh` at a time), each writing its result to a file, so
  an interruption loses one piece and a rerun skips the pieces done.
- **Commit before checks.** Work is committed before a long check starts,
  so a stop or a restart of the session never leaves it only in the
  working tree.
- **Unchanged:** the milestone gates, the checks they need and the CI cost
  policy. Splitting changes how the work is scheduled, not what is checked.

## Third-party components (milestone 0)

- SDL3, Nuked OPL3 and mt32emu are vendored when they are first needed:
  SDL3 at the end of milestone 3a (the window layer), Nuked OPL3 and mt32emu
  at milestone 7. Their pins are recorded here and in THIRD_PARTY.md at that
  point. Done for SDL3 (3.4.18), Nuked OPL3 (commit `765ec96`, see
  "Milestone 7: the OPL core") and mt32emu (2.8.3, see "Milestone 7:
  mt32emu"). Milestone 0 has no third-party code: its exit criterion is about
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

## Milestone 1: resources (2026-10-20)

- **Reference tooling: a patched ScummVM, not a harness.** The comparison
  uses `tools/scummvm-patches/0001-dump-resources.patch`. It adds about 100
  lines and makes the reference ScummVM dump every resource through its own
  resource manager, then exit. A harness compiling ScummVM's resource code
  was rejected for two reasons. It would need ScummVM's file system,
  streams, archive search and configuration layers. And it would test less:
  the patched engine runs the real start-up path, including source discovery,
  format detection, the patch-file scan and the game-specific hooks.
  **Decided** (spec section 12, milestone 1, leaves the choice to the agent).
- **Result.** All 1068 resources of the tested release match ScummVM byte for
  byte: data, size, source file and loaded id. That is 1066 from the volumes
  and 2 from the patch files `SCRIPT.052` and `TEXT.052`. The comparison was
  run with the native release build, an ASan+UBSan build, and the ARM64
  cross build under qemu-user.
- **Scope: SCI0 only.** The port covers what SCI0 needs: the SCI0 map and
  volume formats, compression methods 0 to 2, and SCI0- and SCI1-style
  patch-file names. Other formats (SCI01 maps, SCI1+ maps and volumes,
  methods 3, 4, 18-20, base36 audio patches, WAVE patches, Mac resource
  forks, message maps) are detected where ScummVM detects them and rejected
  as `Unsupported`. ScummVM's game-specific resource hooks (the resource
  patcher, the blacklisted patches, the General MIDI and D110 patch
  additions) have no entries for this game; they are not ported. **Decided**
  (spec section 1.3: other versions and other SCI games are out of scope).
- **Where the port deliberately differs from ScummVM.** All differences are
  for data the tested release does not contain; each is an error where
  ScummVM has undefined or non-deterministic behaviour. **Decided.**
  - Out-of-bounds reads or writes in the decompressors are reported as
    errors. ScummVM writes past its buffer.
  - A stored resource whose packed and unpacked sizes differ is rejected.
    ScummVM overflows or leaves memory uninitialised.
  - A packed-size field below 4 is rejected. ScummVM wraps it.
  - A Huffman link to the node itself is rejected; ScummVM loops forever.
    This was found by fuzzing.
  - An LZW stream that ends early is zero-filled. ScummVM leaves that memory
    uninitialised.
  - A map without a terminator, or naming a missing volume while being read,
    is an error. ScummVM keeps the entries read so far and warns.
  - Patch files of one type and naming style are applied in file-name order.
    ScummVM's order there depends on the directory listing.
  - Only files directly in the game folder are read. ScummVM also searches
    some subfolders (audio, movies), which this game does not have.
- **ScummVM's read-ahead is kept.** ScummVM's decompressors read from the
  open volume file. Their bit readers fetch bytes beyond the packed data and
  read zeros past the end of the file, and the end condition depends on the
  number of bytes fetched. The port reads the same way, so that every
  decode, successful or not, has the same outcome. **Decided.**
- **The volume header id.** When a volume header names another id than the
  map, ScummVM adopts the header's id for the loaded resource. The port
  reports it as `Resource::loaded_id` and keeps the map id as the key. In
  the tested release all ids agree. **Decided.**
- **Logging.** The logging facility of spec section 5.1 was added to
  sci-core with this milestone. It has levels, categories (`General`,
  `Resources`) and a sink installed by app or a tool. Resource warnings go
  to the `Resources` category. **Decided.**
- **Test harness.** `SCI_CHECK` is now variadic, so that conditions with
  braced initialisers need no extra parentheses. **Decided.**
- **Fuzz corpora.** 113 fuzzer-generated inputs were committed to
  `tests/fuzz/corpus/byte_reader` by mistake at milestone 0 (synthetic data,
  not game data). They were removed. Seed corpora hold only hand-made
  inputs, and `tests/fuzz/make_seeds.py` regenerates the sci-resource ones.
  The fuzz runner reports any single input that runs longer than 10 s.
  **Decided.**
- **clang-tidy.** `cppcoreguidelines-pro-bounds-constant-array-index` is
  disabled for the same reason as the unchecked-container-access check:
  standard-library hardening checks indices, and `.at()` would throw.
  **Decided.**

## Milestone 1 approval (2026-10-20)

Milestone 1 was approved and merged
([CityRunner/sierra-hd#2](https://github.com/CityRunner/sierra-hd/pull/2)).

## Milestone 2: path 1, views, fonts and the room viewer (2026-10-20)

- **Reference tooling: a second ScummVM patch.** As in milestone 1, the
  reference is the patched engine, not a harness.
  `tools/scummvm-patches/0002-dump-graphics.patch` hooks in after
  `initGraphics()`. With `SIERRA_HD_DUMP_GRAPHICS=<dir>` it draws every
  picture through `GfxPaint16::drawPicture` and the picture port, as
  `kDrawPic` does. It dumps the visual, priority and control maps as
  palette-index buffers, then dumps every view cel (`GfxView::getBitmap`)
  and every font glyph, then exits. **Decided.**
- **Picture variants.** Each picture is drawn five times into cleared maps:
  normal, mirrored, and with EGA palettes 1, 2 and 3. Each picture except
  the first is also drawn once more, added on top of the previous picture in
  number order ("overlay", the `addToPic` path). That gives 527 picture
  dumps for the 88 pictures. The game selects these options through
  `kDrawPic`; testing every combination for every picture covers more than
  the rooms use. **Decided.**
- **Font glyphs are read back from the screen.** `GfxFont::drawToBuffer`
  exists only in SCI32 builds, and the reference is built without SCI32.
  The patch draws each glyph with `GfxFont::draw()` onto the cleared screen
  and reads it back from the visual map. **Decided.**
- **Result.** All 899 dumps match byte for byte: 527 picture variants, 364
  views and 8 fonts. The comparison was run with the native release build,
  an ASan+UBSan build, and the ARM64 cross build under qemu-user.
- **The pair map.** Path 1 also writes a pair map, which ScummVM does not
  have and which is therefore not compared with it. For each pixel it holds
  the EGA pair (high nibble: colour at odd positions, low nibble: at even
  positions, where odd means x XOR y is odd) that the visual pixel was
  dithered from. Clearing the picture port sets it to 0xFF, solid white,
  like the cleared visual map. `sci-graphics-dump` checks on
  every picture that dithering the pair map gives the visual map. The pair
  map is the input sci-vector needs at milestone 3 (spec section 6.3).
  **Decided.**
- **Where the port deliberately differs from ScummVM.** Again only for data
  the tested release does not contain. **Decided.**
  - Embedded views in pictures (extended opcode 7) are rejected as
    `Unsupported`. ScummVM draws them through the view code; no picture of
    the tested release uses them.
  - A fill that starts outside the screen is an error. ScummVM reads outside
    its buffers.
  - A colour index beyond the four EGA palettes is an error. ScummVM reads
    past its table.
  - A view cel larger than its run-length data could describe (more than 15
    pixels per remaining byte) is rejected before allocating. ScummVM
    allocates it and reads past the resource. This was found by fuzzing
    (out of memory on a 65535 x 65535 cel).
- **The room viewer is a command-line tool until milestone 3a.** Spec
  section 12 asks for a room viewer at milestone 2, and the window layer
  (SDL3) first appears at the end of 3a. `sci-room-viewer` therefore writes
  PNG files: a picture or a stack of pictures (any map, mirrored, any EGA
  palette, any integer scale), a view sheet or a font sheet. The window
  version with zoom and map switching comes with 3a. Its PNG output is
  game-derived and goes to git-ignored directories (`reports/images/`,
  `build/`). **Decided.**
- **Tool PNG writer.** `tools/common/png.cpp` writes uncompressed (stored
  deflate) PNG files, so the tools need no zlib. The files are larger, which
  does not matter for development output. **Decided.**

## Milestone 2 approval (2026-10-20)

Milestone 2 was approved and merged
([CityRunner/sierra-hd#3](https://github.com/CityRunner/sierra-hd/pull/3)).
The milestone 2 report and docs had called the vector path "milestone 3a";
it is milestone 3 (the HD picture pipeline), and 3a is the separate gate
after it (hints, release v0.1 with the window). The labels are corrected.
The customer confirmed the order: milestone 3 first, then 3a. **Decided.**

## Milestone 3: HD picture pipeline (2026-10-20)

The pipeline is described in `docs/hd-pipeline.md`; these are its
decisions.

- **Path 2 reads path 1's trace.** `draw_picture` can record every drawn
  primitive and, per pixel, which one wrote the visual and priority maps
  last. The vector path takes its geometry (after mirroring and clipping)
  and its values from there, so the two paths cannot disagree about what was
  drawn. **Decided.**
- **Faces take their values from the final map.** Instead of replaying the
  fills geometrically, every face of the planar graph takes the value of the
  path-1 pixels it contains (those owned by fills or left as cleared). A
  fill that leaked through a gap at 1x thus gives both faces the same value,
  which keeps the leak as the player saw it (spec section 6.3), and closing
  a gap can never change a colour. **Decided.**
- **Gaps are closed where path 1's line pixels touch.** Fills spread to the
  four neighbours of a pixel, so lines whose pixels touch stop them. Line
  ends whose pixels (within 2 pixels of the end) touch another line's pixels
  are extended to it, joined to its end point or connected square to it,
  within 2 pixels; dots and the picture's border take part. This is the
  "near end points (1 to 2 px) and T-junctions" rule of spec section 6.3,
  with path 1's pixels deciding which gaps exist. **Decided.**
- **Exact topology in doubled coordinates.** Pixel centres are integers at
  doubled coordinates, and the arrangement uses int64 rationals whose
  products stay below 2^60 (checked). Holes are not located: each connected
  component's outer cycle is a face of its own and takes its value from its
  pixels like any other face. **Decided.**
- **Fallback rule.** A face falls back to 1x blocks when its region pixels
  disagree outside the edge band (mixed) or when it contains none (thin:
  slivers along lines, mostly under strokes). Textured brushes are drawn as
  the 1x pixels they own (spec section 6.3: textures by definition). The
  report lists every mixed face with its pixels and bounding box; thin faces
  are counted. **Decided.**
- **Strokes and brushes.** Visible lines are strokes 1 original pixel wide
  with round ends; lines overdrawn completely are left out, white lines are
  kept. A solid circle brush is the ellipse inscribed in its pattern box,
  a rectangle brush the box. The column a pattern may draw beyond the right
  edge (path 1 wraps it to the next row) is not drawn in HD. **Decided.**
- **Curve fitting.** Chains of visible lines up to 12 pixels long, turning by
  at most 50 degrees per vertex, are fitted with a line, a circular arc, a
  cubic spline or (closed and unpinned) an axis-aligned ellipse, within
  **0.5 original pixels** of the chain both ways; vertices where other edges
  meet are kept exactly; fits that would cross or enclose other geometry
  are rejected. 0.5 pixels is the lower end of spec section 6.3's range: at
  that tolerance every pixel of a fitted line is still touched by its
  stroke. Rotated ellipses are fitted as splines. **Decided** (confirmed
  at the milestone 3 gate).
- **Acceptance definitions** (spec section 6.4, `sci/render/acceptance.hpp`).
  Pair classes compare pairs by the colour they blend to, so (a, b) and
  (b, a) are one class. The reduction to 1x takes, per pixel, the value
  covering most of its 16 x 16 samples. The edge band is every pixel with a
  neighbour of another class among its eight. A swap is a group of four or
  more 4-connected differing pixels outside the band. The line check
  requires every pixel a visible line owns at 1x to be touched by the line's
  stroke; pixels covered completely by later layers are reported separately
  (drawing order, not a missing line). **Decided.**
- **Colours.** HD colours are the plain sRGB average of the pair (spec
  section 6.5), computed in 16 bits per channel with one final rounding.
  ScummVM's own EGA undithering (`GfxPalette::setEGA`, `blendColors`)
  averages in gamma 2.2 instead: black and white blend to 186 there and to
  128 here. The difference is intended by the spec. **Decided.**
- **Sampling.** 16 x 16 samples per HD pixel at the cell centres; a sample on
  a face boundary belongs to the face after it; of edges through the same
  point, the one closest to +x decides. The HD priority map takes the value
  at each HD pixel's centre, so occlusion follows the vectors; the switch to
  snap it to the sprite grid comes with the sprites (milestone 5).
  **Decided.**
- **Golden hashes.** SHA-256 of the HD frame's picture area (320N x 190N,
  RGBA) at N = 1, 2, 5, 10 and of the original frame at N = 1, for every
  picture drawn on its own (`tests/golden/pictures.txt`). Overlays are added
  when the scripts tell which rooms draw them (milestone 4). Synthetic
  scenes have golden hashes in CI (`tests/golden/synthetic_test.cpp`).
  **Decided.**
- **Debug highlighting** (spec section 6.3) is the CMake option
  `SCI_DEBUG_HIGHLIGHT` (off by default); the room viewer's `--highlight`
  then shows fitted strokes and fallback blocks. **Decided.**
- **Reference machine and budgets (proposal, spec section 10.3).** A laptop
  with a 4-core x86-64 CPU of about 2020 (for example an Intel Core i5-1135G7
  or AMD Ryzen 5 4500U), 8 GB of memory and integrated graphics. Room entry
  (path 1, both scenes, the HD frame and priority map) is measured at N = 10
  on the 4-core development container: see the milestone 3 report.
  Proposed budgets: room entry under 1 second at N = 10, and the first
  launch's analysis of all rooms under 15 seconds. **Decided** (confirmed
  at the milestone 3 gate).
- **Threads in the tools.** `tools/common` implements the executor with
  `std::jthread` at milestone 3. At milestone 3a it moves to app's
  `sci-platform` library, which the tools link too. **Decided.**

## Milestone 3 approval (2026-10-20)

Milestone 3 was approved and merged
([CityRunner/sierra-hd#4](https://github.com/CityRunner/sierra-hd/pull/4)).
The customer decided:

- **Fitting tolerance: 0.5 original pixels** (spec section 15, open question
  1). A trial at 1 pixel on picture 44 changed little and left 840 line
  pixels in 64 pictures untouched by their strokes. Rounder shapes are left
  to the hints of milestone 3a. **Decided.**
- **Edge blend space: sRGB**, as assumed (spec section 15). **Decided.**
- **Reference machine and budgets** as proposed in "Milestone 3": a 4-core
  x86-64 laptop of about 2020 with 8 GB of memory and integrated graphics;
  room entry under 1 second at N = 10; the first launch's analysis of all
  rooms under 15 seconds. **Decided.**
- The manual side-by-side review continues with further rooms. **Open.**

## Milestone 3a: hints and release v0.1 (2026-10-21)

- **SDL 3.4.18** (tag `release-3.4.18`, commit
  `829a65d769d935c4852f8159e964312c0957260a`, 2026-10-02) is vendored in
  `third_party/SDL3`: the library sources only (`VENDORED.md` lists what was
  left out). It is built as a static library. On Linux it is built with X11
  and Wayland, both loaded at run time. **Decided.**
- **`sci-platform`.** The parts of app without a window go into a library
  under `src/app/platform`, which the development tools link too:
  - the thread executor;
  - reading the game folder;
  - the configuration file;
  - identification of the release;
  - loading the hint files.

  Threads and OS files stay in app, as spec section 4.2 asks. **Decided.**
- **Hint format, version 1** (`hints/README.md`, `hints/schema.json`). It
  is JSON: integers only, printable ASCII, strict, with unknown members
  rejected.
  - Lines are named by their command index in the command stream as
    sci-pic parses it (one line per command). Chains and gaps carry no
    coordinates, so mirrored drawings need nothing extra.
  - Region labels carry one 1x pixel in stored, unmirrored coordinates.
    Spec section 6.7 allows both command indices and 1x coordinates.

  **Decided.**
- **Hint rules.**
  - A chain hint is fitted by the requested kind within 1 original pixel
    (the upper end of spec section 6.3's range), through every vertex
    where other lines meet.
  - The fit is kept only if it keeps the faces and every pixel the lines
    own at 1x stays touched by the stroke (the line check of section 6.4).
  - A gap hint closes at most 8 pixels and is kept only if mixed faces and
    1x-block pixels do not increase (the gap rule).
  - Hints the program does not use are listed with the reason in the
    replacement report; so are the ones it uses.

  **Decided.**
- **How the hints were made (the AI step of spec section 6.7).**
  - `sci-hint-tool candidates` proposes the chains that may be curves the
    automatic fitting missed: runs of three or more connected visible lines
    that turn, by at most 75 degrees at a joint, and are not all fitted
    already. It tries each candidate as an arc, a spline and a line under
    the hint rules.
  - The agent (Claude, reading the images) judged each candidate with an
    accepted kind on the picture: a curve of the drawn object, or a shape
    meant to be angular. It chose the kind, a label, region labels and
    symmetry, and marked doubtful decisions uncertain.
  - `make_hints.py` turned the decisions into the hint files.
  - 1709 chains (783 uncertain) and 508 region labels in 86 files.
    Pictures 55 and 992 have none.
  - No model runs in the program or in CI. The hint files are frozen at
    the milestone 3b gate, not at 3a (addendum A1).

  **Assumed until the customer's spot check.**
- **Gap hints only at mixed faces.** The automatic gap closing already
  closes every gap where the 1x line pixels touch. The other gaps are
  leaks the player saw, and closing one changes nothing visible unless the
  faces on its sides differ. So gap hints were looked for only at the 5
  mixed faces of the visual plane.
  - Every line end near each face was tried with every line within 8
    pixels, under the gap rule.
  - Of the gaps that improved the face most, the shortest was taken.
  - The 5 gap hints (pictures 5, 15 twice, 47 and 78; 2 to 4 pixels long)
    are marked uncertain.
  - They leave one mixed face (picture 47, 8 pixels; 648 pixels before).

  **Assumed until the customer's spot check.**
- **Priority plane.** The same hints apply to the priority plane. Where a
  chain's priority pixels differ from its visual pixels, the line check
  can fail there; then that plane keeps the automatic result. **Decided.**
- **Game identification** (spec section 10.2).
  - The tested release is recognised by the SHA-256 of `RESOURCE.MAP` and
    `RESOURCE.001` to `.004`. The interpreter files (`SCIV.EXE`, the
    drivers) are not needed and not checked.
  - A missing or different patch file (`SCRIPT.052`, `TEXT.052`) gives a
    warning.
  - The 5.25" release is recognised by ScummVM's MD5 of the first 5000
    bytes of `RESOURCE.MAP` and its size, with a warning. Its hints are
    skipped by their PIC hashes.
  - Anything else stops with a message.
  - MD5 is implemented in sci-core for this alone.

  **Decided.**
- **Configuration.** The file is `sierra-hd.ini` next to the executable,
  section `[sierra-hd]`, `format = 1`, with these keys:
  - `game_folder`;
  - `mode` (hd or original);
  - `scale` (auto, or 1 to 16);
  - `priority_snap` (on or off, default off; milestone 5): on snaps the
    HD occlusion edges to the sprite grid (spec section 6.6);
  - `mt32_rom`, which is unused until milestone 7.

  Unknown keys give a warning; a different format stops. The program
  writes the file on exit. **Decided.**
- **Release v0.1** (spec section 12; moved to after milestone 3b by
  addendum A1). The program and its release workflow are built at 3a;
  the `v0.1.0` tag that publishes them follows the 3b gate.
  - The program `sierra-hd` is the room viewer in an SDL3 window:
    - pictures in HD or original mode, with the visual or priority map;
    - views with their animation, and fonts;
    - N automatic (the largest that fits in physical pixels) or chosen;
    - fullscreen, and the status line in the game's first font.
  - There are no debug overlays: `SCI_DEBUG_HIGHLIGHT` stays a
    development option.
  - Only the changed rectangles are uploaded to the texture.
  - The release workflow builds Linux and Windows on x64 and ARM64, and
    macOS on a manual run.
  - The Linux program links its C++ runtime statically and needs glibc
    2.38 or newer, the version of the toolchain image (Ubuntu 24.04).
    **Decided.**
- **The analysis cache is deferred** (spec section 10.2). Without the VM
  the viewer analyses a picture when it is shown (at most about 0.5 s at
  N = 10, within the room-entry budget) and keeps the last four. The
  cache on disk, with the first-launch analysis of all rooms, comes with
  the VM at milestone 4, where rooms are entered by the game. **Assumed;
  a deviation for the customer to confirm.**
- **`--version`** prints only the version. The milestone 0 component list
  is gone, since every library linked by the program now has content
  except sci-audio and sci-vm. **Decided.**

## Addendum A1 (2026-10-21)

The customer added milestone 3b (`docs/SPEC_ADDENDA.md`, A1). It does not
change spec section 2. Its effect on the plan:

- **Milestone 3a** stays as built, with two changes:
  - the hint format gains `relations` ("in-front-of", "behind",
    "contains" between regions and chains). They are read and validated
    now, and used by the vector path from 3b;
  - the hint files are not frozen at the 3a gate.
- **Milestone 3b**, the next gate after 3a: section 6.10.
  - Clipping at contours, depth order and shared boundaries.
  - No stray fragments and no breaks.
  - The A1 checks, and depth and containment relations added by a rerun
    of the AI step.
  - The hints are frozen and the golden hashes regenerated at its gate.
- **Release v0.1** is tagged after the 3b gate.

**Decided** (the customer's addendum).

## Milestone 3b: drawing order, occlusion and stroke integrity (2026-10-21)

- **The A1 checks** (`sci/render/integrity.hpp`) sample the visual scene
  at 8 x 8 points per pixel and use the definitions agreed at the 3a gate:
  - a contact needs a seen end pixel next to a seen pixel of a line that
    passes there, no end of that line next to it, and an end that did not
    draw over it;
  - "connected" at joints is read geometrically: a sample of the line lies
    in or next to the other line's stroke shape, cuts included;
  - only lines that drew two or more pixels in common count as doubled;
  - fragments are 4-connected groups of samples outside the line's owned
    pixels.
  **Decided** (3a approval).
- **Residual cases carry a class and a size.** Overshoots whose end pixel
  lies past the contour at 1x, or that meet the contour at under 30
  degrees, and broken joints whose end meets another line it touches, are
  counted apart, so the report can show where the 1x picture itself, not
  the pass, decides the outcome. The checks are not relaxed by these
  classes: every case still counts. **Decided** (3b approval).
- **The stroke integrity pass** edits the visual plane's strokes after the
  scene is built (`sci-vector/src/stroke_integrity.cpp`). The order is:
  spurs, ends on contours, joints, runs not seen. Its limits:
  - an end reaches its contour within 2 pixels across (1.5 at 1x plus the
    fitting tolerance) and moves at most 3 pixels along the line;
  - ends closer than 0.75 already join;
  - hidden pixels 1.5 apart along the line form one run;
  - stroke points more than 0.5 past a line's end pixel are a spur.
  The priority plane is not changed: its strokes decide occlusion of
  sprites, where the pass's reasons do not apply. **Decided** (3b
  approval).
- **Connectors belong to the earlier line** of a joint, and are kept apart
  from the line's own strokes. A connector of the later line would be
  drawn over lines in between and split them. **Decided** (3b approval).
- **No depth relations were added.**
  - The HD scene keeps path 1's drawing order. The depth check finds 15
    pixels in the whole game, all at line ends, none from layer order.
  - 1x visibility contradicts the priority map at 21 pixels in 5 pictures:
    a later command wrote a nearer priority there without drawing the
    visual. As A1 says for contradictions without a hint, the original
    command order is kept; the report lists the pixels.
  - A relation between regions or chains would not change these cases:
    they are single pixels, not objects in the wrong order. So the AI step
    was not rerun, and the hint files are unchanged since 3a.
  - The vector path reads and validates relations but does not use them.
  **Decided** (3b approval).
- **The fallback of fitted chains to their segments** (A1, "where a fitted
  primitive cannot satisfy these rules") is not implemented. Built with no
  fitting at all, the scenes have about 3 to 5% fewer cases, so the
  fallback could remove at most that share. It would also need the checks
  inside scene building, about 0.5 s more per room. **Decided**
  (3b approval: option 1, not implemented).
- **The baseline** is measured with the development option
  `SCI_WITHOUT_STROKE_INTEGRITY` (scenes without the pass). **Decided**
  (development only, like `SCI_DEBUG_HIGHLIGHT`).
- **The pass's changes are reported** by kind and evidence in the report.
  `tools/hd-report --changes <file>` writes all of them (213,513,
  24 MB), which is too large to commit. **Decided** (3b approval).

## Milestone 3b approval (2026-10-21)

Milestone 3b was approved and merged
([CityRunner/sierra-hd#6](https://github.com/CityRunner/sierra-hd/pull/6)).
The customer decided:

- **Option 1 of the trade-off**: the remaining A1 cases are accepted as
  listed in `reports/milestone-3b-pictures.md`, by class and size. The
  checks stay as defined; the fitted-chain fallback and a local 1x
  fallback are not implemented. **Decided.**
- **The side-by-side review** is approved with the gate. **Decided.**
- **The hints are frozen** as committed at 3a (no relations were added);
  no further spot check was asked for. From now on a change to `hints/` is
  a reviewed commit like any other. **Decided.**
- **Release v0.1** is tagged after the merge, as a GitHub pre-release.
  **Decided.**

## Milestone 4a: the VM boot gate (2026-10-21)

Reference and comparison:

- **ScummVM's Event Recorder is not used** (spec section 7). It records
  the backend's events with millisecond timestamps of real time, in a
  binary file tied to ScummVM's event manager and its own timer and random
  seeding, and replays them only inside ScummVM. A replay must instead be
  timed in game ticks, be readable by both interpreters and be written by
  hand for short scenarios. The format is our own text format
  (`docs/formats/replay-and-trace.md`). **Decided.**
- **The deterministic mode of the reference** is the patch
  `tools/scummvm-patches/0003-deterministic-trace.patch`: a virtual clock
  advanced only by ScummVM's sleeps and waits and by kGetEvent's 10 ms
  delay; the music timer once per base tempo of the driver on that clock
  (4 ms for AdLib); the `sci` random source seeded with the run's seed at
  initGame; a fixed date (1990-01-01 12:00); replayed input and a virtual
  mouse; every kernel call written to the trace. At the stop the patch
  calls `quitGame()`, so the game ends at its next event poll. **Decided.**
- **Reference runs use the AdLib driver** (`--music-driver=adlib`), whose
  timer sets the music timing. MT-32 timing is compared at milestone 7.
  **Decided.**
- **Detected features are fixed for this game**, with the values the
  reference detects (`--debugflags` output): graphics functions of late
  SCI0, the early-SCI0 kSetCursor, move counts incremented, no fast cast,
  no pseudo mouse, DOS (modifiers with 0x0200). Only the SCI0-late code
  paths of ScummVM are ported. **Decided.**

Behaviour kept from ScummVM's release build:

- **Reading an uninitialised temporary** warns and gives 0, as ScummVM's
  release build does, unless a workaround gives another value. **Decided.**
- **`error()` becomes a fault**: the first message is kept and the
  interpreter stops (`Faults`); warnings go to the log category `vm`.
  ScummVM's asserts are faults too (for example a song volume above 127).
  **Decided.**
- **Unported kernel functions fault** with "<name> is not implemented".
  For 4b: Parse, Said, MenuSelect, DrawControl, HiliteControl,
  EditControl, the four file functions, SaveGame, RestoreGame,
  CheckSaveGame, GetSaveFiles, DoAvoider, SetJump. kDeviceInfo compares
  paths for equality where ScummVM matches a pattern; 4b ports
  `matchString`. **Decided.**
- **Script patches**: the 16 entries of ScummVM's Laura Bow 1 table are
  ported with their signatures and comments; ScummVM applies no generic
  patches. In the tested release four apply, at the same offsets as in the
  reference: the obstacle collision fix (script 998), the two speed-test
  patches (script 99) and the copy-protection random fix (script 414).
  **Decided.**

Determinism (spec section 5.3):

- **sin and cos** (kTimesSin, kTimesCos, kSinDiv, kCosDiv, kGetDistance)
  come from a table of glibc's results for the angles -360 to 360
  (`scripts/gen-trigonometry.py`), so they are the reference's on every
  platform. Correctly rounded values differ from glibc's at four angles.
  Other angles use the angle modulo 360, which can differ from glibc's
  result in the last bits. **Decided.**
- **kSqrt and kGetDistance** compute in binary32 in ScummVM. Since only
  `double` is allowed, each step is computed in `double` and rounded to
  binary32: for addition, multiplication and square root this gives the
  binary32 result exactly (53 >= 2 x 24 + 2). **Decided.**
- **kStrCmp** returns what glibc's strcmp and strncmp return, the
  difference of the first differing bytes. The C library only fixes the
  sign; the optimised build returned 1 where glibc returns 70, which the
  ASan run found. **Decided.**
- **Animate's sort** is a stable sort with ScummVM's comparator, which is a
  total order (it ends on the given order), so the result is the one
  ScummVM's unstable sort gives. **Decided.**

Graphics at 1x:

- **The display map is the visual map.** ScummVM keeps both; they hold the
  same pixels when EGA undithering is off, ScummVM's default and the
  reference's setting. Showing bits only checks the rectangle, since
  presenting the screen is the program's job. **Decided.**
- **Transitions take no virtual time**, except the palette fade (22 sleeps
  of two ticks), because ScummVM waits for the other transitions in real
  time, which the virtual clock does not see. **Decided.**
- **kDrawPic supports only the default picture window** (top 10, 320 x
  190), the one sci-pic draws into; another faults. The game uses only
  this one. **Decided.**
- **kMenuSelect** (the interactive menu) is 4b; entries, attributes, the
  bar and the status line are ported. **Decided.**

Sound:

- **The music player is in sci-vm, not sci-audio** (spec section 4.1 lists
  the silent driver under sci-audio). As in ScummVM, the player keys songs
  by VM object, writes their signals and is driven by kDoSound, so it
  belongs with the kernel functions; what reaches the silent driver is
  dropped. The device layer (OPL, MT-32, mixer) goes into sci-audio at
  milestone 7, under this player. **Decided** (confirmed at the 4a gate).
- The game has no SCI0 sound with a digital sample, so one faults.
  ScummVM's volume round trip through its configuration value always
  returns the same volume, so kDoSound MasterVolume is a plain set. The
  master volume starts at 11, ScummVM's default music volume of 192.
  **Decided.**

Scope:

- **The program does not run the VM yet, and the analysis cache stays
  deferred.** The 4a gate needs only the deterministic runs. Running the
  game in the window, with the cache of spec section 10.2, is proposed
  for 4b. **Decided** (confirmed at the 4a gate: both are added to 4b).

## Milestone 4a approval (2026-10-21)

- The gate is approved and merged.
- The music player stays in sci-vm; the audible devices go into sci-audio
  at milestone 7.
- 4b adds two items to the spec's scope: running the game in the program,
  and the analysis cache of spec section 10.2.

## Milestone 4b: kernel coverage (2026-10-21)

- **Saves are listed, not written, until milestone 8** (the customer's
  choice at the start of 4b). GetSaveFiles and CheckSaveGame work as in
  ScummVM with an empty save folder; SaveGame and RestoreGame report
  failure to the game, as ScummVM does when it cannot save, and log why.
  The project's save format comes at milestone 8 (spec section 9), and the
  scenario recordings do not save. **Decided.**
- **The game's own save and restore dialogs.** ScummVM replaces the
  game's save and restore methods with calls to its own dialogs unless
  `originalsaveload` is set. sci-vm keeps the game's methods, and the
  reference now runs with `originalsaveload=true`, so both run the same
  bytecode; the boot trace is unchanged by it. **Decided.**
- **Files the game opens** come from a `FileStore` the host supplies
  (sci-vm does no I/O): files the game wrote, else the game folder's, as
  ScummVM falls back to the game files. The tool uses an in-memory store
  that starts empty, as the reference's save folder does. **Decided.**
- **The referenced kernel functions** are found by `sci-vm-trace
  --scan-kernel`: every callk in the scripts' code blocks, after the script
  patches. The tested release references 97; DoAvoider is not among them.
  **Decided.**
- **The game runs in the program by default** (the customer's request at
  the 4a gate); `--viewer` keeps the room viewer of v0.1. The interpreter
  runs on the main thread (spec section 4.3): wherever virtual time would
  pass (kWait, the speed throttle, event polling, busy waits), sci-vm calls
  the host, which presents the frame, reads input and returns once that
  much real time has passed. If the program falls more than 250 ms behind
  (a slow room entry, a dragged window), it drops the lag instead of
  running fast to catch up. Interactive runs seed the random source from
  the host's performance counter; the deterministic runs keep their seed.
  **Decided.**
- **What the player sees** is what ScummVM copies to the screen (bitsShow,
  transitions), not the visual map drawing goes to. sci-vm keeps that
  shown screen, and with it the picture it shows. **Decided.**
- **HD mode while the game runs (milestone 4b approximation).** A pixel of
  the shown screen that nothing but the picture drew (the cover map, kept
  by sci-vm and saved and restored with the screen bits) comes from the HD
  background; any other pixel (sprites, text, windows, the menu bar) is
  drawn as an N x N block of its EGA colour. Sprites are occluded correctly
  because the interpreter only draws their visible pixels. F12 switches
  between HD and original mode at any time (spec section 1); F11 or
  Alt+Enter toggles fullscreen. **Decided** (approved at the 4b gate; the
  HD sprite pipeline belongs to milestone 5).
- **The game's own files** (and later the saves) are kept in a `files`
  folder next to the configuration; a file the game opens is looked for
  there first, then in the game folder. **Decided** (approved at the 4b
  gate).
- **SDL's single-precision window coordinates.** SDL3 reports and takes
  mouse and window positions as `float`; the program names that type
  `SdlCoordinate` (via `decltype`) and does all arithmetic on it in double.
  The values only place the pointer and never reach rendering, so the
  floating-point profile of spec section 5.3 is unaffected. **Decided.**
- **Busy waits on kGetTime.** Scripts (the copy-protection dialog among
  them) wait by reading kGetTime(elapsed) until enough time has passed,
  with no other kernel call in between. On a virtual clock no time would
  ever pass there, so in both engines such a read lets 1 ms pass first
  (`docs/formats/replay-and-trace.md`). Interactive runs need the same
  rule, since they also run on virtual time paced by the host. The earlier
  reference traces are unchanged by it. **Decided.**
- **Time of day in the deterministic mode** (start date and time approved
  at the 4b gate). The game times its scenes
  (the opening, conversations) by the time of day from kGetTime, so a
  fixed time froze them. The day now starts at Monday 1990-01-01, 12:00:00
  and runs with the virtual clock, in both engines; the interactive program
  reports the host's local time, as ScummVM does. **Decided.**
- **ScummVM's real-time remnants in the deterministic mode.** The
  screen-update throttle (which is also where a requested quit stops the
  game) and the text-edit cursor blink run on the host clock in ScummVM;
  the reference patch moves both to the virtual clock, and sci-vm ports
  them on the virtual clock. Otherwise where a run stops and when the
  cursor shows depended on the host's speed. **Decided.**
- **Screens are compared as well as kernel calls.** A kernel-call trace
  cannot see drawing: with a wrongly signed kDisplay background colour,
  every text box was filled white while the traces matched. The reference
  patch and `sci-vm-trace` now also write the shown screen once a second
  of virtual time, and the scenario gate compares both (the boot scenario:
  61 screens identical). **Decided.**

- **The text parser is in sci-text** (`sci/text/parser.hpp`): the
  vocabulary (vocab.000, 901, 900), tokenizing, synonyms, the grammar and
  the Said specs, without VM types; sci-vm's kParse and kSaid connect it
  to the game's objects, as ScummVM's Vocabulary and kparse.cpp do.
  **Decided.**
- **A grammar that keeps growing stops the parser.** ScummVM expands the
  grammar rules until memory runs out when a vocab.900 is recursive in a
  certain way; the port stops at 4000 rules or 1000 passes, warns, and
  turns the parser off for the game. The tested release stays far below
  the limit (no warning; the traces match); only damaged or fuzzed data
  reaches it. **Decided.**
- **An empty sentence parses as a success**, as in ScummVM; kParse never
  passes one, since it handles empty input first. **Decided.**

## Milestone 4b: the analysis cache (2026-10-21)

- **Where the cache lives.**
  - The scene serialization is in sci-vector, as bytes in and bytes out.
  - The cache with its file I/O is in sci-platform (app's library), because
    spec section 4.2 keeps the libraries free of OS files.
  - The files are in `cache/` next to `sierra-hd.ini`.
  - **Decided.**
- **The key** is the SHA-256 of the game digest, `analyser_version`,
  `scene_format_version`, the bytes of the picture's hint file and the
  variant (picture, plane, mirrored, EGA palette). **Decided.**
- **The game digest is computed over the resources** (type, number, size
  and data of each, as the resource manager delivers them), not over the
  files on disk. So a patch file that changes a picture changes the digest,
  and identical content in another file layout does not. It takes about
  the time of reading the game once at launch. **Decided.**
- **`analyser_version` is increased with every change of
  `tests/golden/pictures.txt`**, since any change to a scene changes those
  hashes. **Decided.**
- **What the first launch prepares.**
  - It prepares both planes of every picture as drawn without options.
  - Mirrored and EGA-palette variants are analysed and stored the first
    time they are needed.
  - Overlays (`kDrawPic` with add-to) are not cached, because their scene
    depends on the picture below.
  - **Decided** (approved at the 4b gate), as is the cache's size (83 MB,
    uncompressed).
- **Batches of 16 on the executor.** The executor's `run` blocks until it
  is done. So the progress bar and the window events are handled between
  batches, on the main thread, and only the main thread writes files.
  Closing the window cancels the analysis and quits; the entries written so
  far are kept. **Decided.**
- **The last 8 scenes are also kept in memory**, so going back and forth
  between rooms does not read files. **Decided.**
- **The original mode still draws path 1 directly**; only the HD mode uses
  the cache. **Decided.**
- **Measured** (release build, 4 cores, the tested release):
  - The first launch analyses all 176 entries in 2.8 s, against the 15 s
    budget.
  - A scene loads from its file in 4 ms, against 64 ms to analyse it.
  - The entries take 83 MB.

## Milestone 4b approval (2026-10-22)

- The gate matrix is green (Linux and Windows; macOS optional). The
  customer accepted every point of the report and said to go ahead with
  the next step: milestone 5. The pull request is merged by the customer,
  as before.
- Addendum A2 is scheduled as milestone SU, after milestone 6.

## Milestone 5: frame-by-frame comparison (2026-10-22)

- **Actor positions are compared after every kAnimate call** (spec
  section 12: "scripted walks give the same hero positions as ScummVM,
  frame by frame"). A frame is one kAnimate call; both engines write each
  cast entry in drawing order with the values GfxAnimate drew it with
  (position, z, priority, view, loop, cel) and the signal it leaves, keyed
  by the call's number and tick in the trace
  (`docs/formats/replay-and-trace.md`). Every cast member is compared, not
  only the hero, so other actors moving at the same time are covered.
  **Decided.**
- **The 1x maps compared are the ones the logic reads**: GfxScreen's
  priority and control maps, written at the ticks of the screen dumps.
  At those ticks no kAnimate is running, so moving actors have been
  restored away and the maps hold the picture, the added views and the
  stopped actors. **Decided.**
- **The reference hooks GfxAnimate::kernelAnimate** (patch 0003): the
  cast is recorded after `restoreAndDelete()` and written with the
  kAnimate trace line. sci-vm exposes the same data through
  `RunOptions::on_animate` and the maps through `RunOptions::on_tick`,
  without changing the trace. **Decided.**
- **Scripted walks** (`tests/scenarios/`): four replays for movement,
  priorities and collisions (keyboard and mouse walks against walls and
  furniture, both staircases, Lillian moving while Laura walks), besides
  the earlier walks, which are now compared frame by frame as well.
  The game has no path finding (DoAvoider is not referenced), so the
  mouse walks go round obstacles with several clicks. **Decided.**

## Milestone 5: the HD composition (2026-10-22)

The design is `docs/hd-composition.md`; its section "Changes from the design
made in the implementation" lists what changed while implementing it.

- **Kernel primitives "outside a window"** are those drawn while the
  picture window is the current port. In this game every kGraph line and
  box is drawn in the window manager's port (script 981 draws the double
  frames of the message windows; the copy-protection screen its name
  boxes), so they are interface; no scenario draws a primitive into the
  picture. **Decided.**
- **Picture overlays** (kDrawPic with add-to) are drawn by rooms 74, 290
  and 353 and by `Game::replay`; the full list is in "Milestone 5: the
  picture stacks" below. The scenario `attic.txt` reaches room 74's two;
  all are checked with `sci-hd-frames --stack` and their golden hashes.
- **Saved bits** with the priority map also carry the priority source
  plane; their hunks are larger than ScummVM's. The scripts never read
  saved bits, and the traces of all scenarios still match. **Decided.**
- **Stopped and always-update views** (drawn by kAnimate before
  `draw_cels`) are part of the background layer, drawn as 1x blocks with
  1x occlusion, as the design says. **Decided**, and confirmed by the
  customer on 2026-10-22 ("keep as is"), after the side-by-side images of
  seated actors (rooms 44 and 45).
- **Measured** (release build, 4 cores, `sci-hd-frames --timing`, room 37
  of `grounds.txt` with Laura walking down the stairs behind the banister,
  ticks 7180-7560; room 44 of `boot-and-bedroom.txt`, ticks 3800-4300):

  | | N = 5 | N = 10 |
  |---|---|---|
  | frame with changes, room 37: mean / max | 0.49 / 1.4 ms | 0.73 / 1.0 ms |
  | frame with changes, room 44: mean / max | 0.59 / 1.1 ms | 0.76 / 1.2 ms |
  | whole frame again (mode or snap switched) | 6.6 ms | 20.7 ms |
  | first frame of a picture, with rendering its HD images | 156 ms | 515 ms |

  About 3300 to 4200 1x pixels are composed per frame. Rendering the HD
  images of a new picture is the cost of entering a room, as before.

## Milestone 5: the picture stacks (2026-10-22)

The golden hashes cover the picture stacks the scripts can draw (spec
section 5.3: "with overlays where a room uses them";
`tests/golden/pictures.txt`, lines `stack ...`). They were found in the
scripts' bytecode (after the script patches), not from runs:

- **Where add-to comes from.** kDrawPic adds to the picture when it has a
  third argument and that argument is 0 (this game does not use the old
  graphics functions). The scripts have 52 kDrawPic calls (`sci-vm-trace
  --scan-kernel`). For each, the pushed arguments were read back from the
  instructions before the call: 47 have fewer than three arguments or a
  third argument of 1. The third argument is a constant in all 52, so no
  add-to depends on a variable. Five calls have a 0 (script and offset of
  the callk):
  - 074 at 002d and 0125: two local procedures of room 74. Procedure A
    (on entering from room 64, and in `doit` while Laura is in neither
    of the two areas below): global 61 = 0, picture 174 (style 3, palette 0), then 74 over
    it (style 3, palette 0). Procedure B (on entering from any other room,
    and when Laura walks into the area x 0-64, y 100-200; the area x -10
    to 58, y 0-100 draws nothing): global 61 = 1, 174
    (style 2, palette 1), then 74 over it (style 2, palette 0). Stacks
    `174+74` and `174.palette1+74`.
  - 290 at 01c1: room 290's local procedure 01ab draws 282 (style 6,
    palette global 61), then local 0 over it (style 7, palette global 61).
    Local 0 starts at 0 and is set only by the `fight` script: 182 in
    state 5, 382 in state 12, 182 in state 33. The procedure runs in states
    15, 18 and 21 (directly or through procedure 01f8), which follow state
    12, and in states 37 and 41, which follow state 5 (state 6 jumps to
    37) or state 33; the other jumps (state 0 to 3; at state 1, an event
    to 7 or 25) do not reach a draw before local 0 is set. Stacks
    `282+182` and `282+382`.
  - 353 at 005b: room 353 (`scene42c`, picture 62), local procedure 0044,
    called in state 2 of `stoke`: 42 (one argument, so cleared and
    palette 0), then 162 over it (style 7, three arguments, palette 0).
    Stack `42+162`. (Not in the first list of this milestone.)
  - 994 at 01e5: `Game::replay`, which the interpreter calls after a
    restore, draws the room's `curPic` (style 100, palette global 61),
    then global 57 over it when it is not -1. Global 57 is set only by
    `Rm::drawPic` (-1), `Rm::overlay` (its picture) and room 74's init
    (174, after `Rm::init` drew the room's picture 74, so `curPic` is 74).
    No script sends `overlay:` (every `pushi 270` is a number, not that
    selector), and rooms 64 and 75, the only ones room 74 leads to, draw
    their pictures with `Rm::drawPic`. So a game restored in room 74 shows
    74 with 174 over it, both with global 61's palette: stacks `74+174` and
    `74.palette1+174.palette1`. Restoring comes with the save format
    (milestone 8); the hashes are recorded now.
- **Options.** No call sets the mirrored flag (bit 14 of the second
  argument). The palette is a constant or global 61. Global 61 is 1 only
  in room 74 after procedure B, in room 64 when entered from there (it
  returns only to the previous room, 74, whose init sets global 61 again),
  in room 778 until its `newRoom` sets 0, and in room 786 (the
  Sleuth-o-Meter at the end of the game); room 75 sets 0 before drawing. So
  rooms 290 and 353 draw their stacks with palette 0. With palette 1, 282,
  182 and 382 would look different, so this matters.
- **Line format.** `stack <stack> <kind> <hash>`, with the stack written as
  the analysis cache names its entries (`tools/golden-hashes/README.md`).
  The HD frames come from the cache's variant of the stack, as the program
  draws them (`sci/platform/hd_pictures.hpp`), the original frame from the
  same drawing of the stack (`sci::platform::draw_variant`). The single
  pictures' lines did not change. The hashes of the stacks are the same
  under qemu-aarch64 (cross-aarch64 preset).
- **Scenario.** `tests/scenarios/attic.txt` walks from the bedroom to room
  47's doorway on the right and up into room 74 (`174.palette1+74`, tick
  5458), then into Fifi's room (`174+74`, tick 7552). Its traces, screens,
  maps and actor positions match the reference. No scenario reaches rooms
  290 and 353 (scenes later in the story), and a restore needs the save
  format, so those stacks are checked through the golden hashes and
  `sci-hd-frames --stack` only.

## Milestone 5 approval (2026-10-22)

- The gate is approved as is and merged: the Linux and Windows matrix is
  green; macOS is optional.
- Stopped and always-update views keep 1x occlusion in HD.
- Rooms 290 and 353, whose picture overlays no scenario reaches yet, are
  left to the playthrough of milestone 9; their stacks are covered by
  golden hashes.

## Milestone 6a: vocabulary and Said matching (2026-10-22)

- **The parser is compared directly, not only through the kernel calls.**
  Both engines write, with the trace line of every kParse call, the input,
  the outcome (parsed, grammar failed, unknown word, no words), the words
  the grammar got (class and group, after the room's synonyms and the
  pronouns), the parse tree the Said specs are matched against (ScummVM's
  `_parserNodes`) or the unknown word, and the event's type and claimed
  flag; with every kSaid call, the spec's bytes, how it matched (or why it
  was not matched) and the claimed flag it leaves
  (`docs/formats/replay-and-trace.md`, "Parser files"). The reference
  hooks `kparse.cpp` (patch 0003); sci-vm reports through
  `RunOptions::on_parser`, and the scenario gate compares the files as a
  fifth item (`tools/vm-trace/compare-parser.py`). **Decided.**
- **The corpus is generated from the vocabulary.** `sci-vm-trace
  --vocabulary` lists vocab.000, 901 and the scripts' synonyms as sci-text
  reads them; `tools/vm-trace/make-parser-scenarios.py` turns that into
  seven replays (`tests/scenarios/README.md`, "The parser corpus"):
  - two for the sentence forms: pronouns (before any noun, after a noun,
    after a failed parse), the room's synonyms, several sentences in one
    line, punctuation, capitals, long input, empty input, unknown words,
    numbers and broken grammar;
  - five that type every word: imperative verbs alone and with an object,
    nouns after look at or a verb, adjectives, prepositions and articles
    with a noun, adverbs, indicative verbs, conjunctions, pronouns after a
    noun, the wildcard words, and one typed form per suffix;
  - in five rooms with their own Said specs and synonyms: Laura's room
    (44), the back upstairs hallway (43), the front downstairs hallway
    (37), the kitchen (35) and outdoors, the back garden (4), reached
    along the routes of earlier recordings.
  **Decided.**
- **Coverage** (the parser files of the seven runs): all 1295 words of
  vocab.000 are typed at least once. 1289 are recognized; the other six
  cannot be, in either engine: the Said wildcards `!*` and `*` (the
  tokenizer drops the punctuation) and the four words stored with a dash
  or an apostrophe (`good-looking`, `manor-house`, `o'neil`, `run-down`:
  the lookup removes dashes and the tokenizer drops apostrophes before
  looking a word up, so the stored spelling never matches; typing them
  gives an unknown word). 1287 words are in at least one command the
  grammar accepts (all but the number word `10` and `noword`). By class
  (all typed): 770 nouns (768 recognized), 218 imperative verbs, 122
  adjectives (120), 51 adverbs, 37 prepositions, 34 noun-verbs, 21
  indicative verbs, 11 pronouns, 6 articles, 11 conjunctions and linking
  words (and, or, with, about, ...), 5 noun-indicative verbs, 4
  preposition-verbs and others (back, front, open, gold), 3 wildcards
  (`any`, `anyword`, `*`) and `!*`, `noword`. The seven runs make 1421
  kParse calls (1362 parsed, 24 rejected by the grammar, 32 with an
  unknown word, 3 without words) and 227944 kSaid calls (397 full and
  3911 partial matches, 218407 without a match, 5229 skipped), all equal
  in both engines, as are their traces, screens, maps and actor
  positions.
- **Timing of the commands.** Each command is typed at once (a `text`
  event), closed with Enter 40 ticks plus a tick per character later, then
  a second Enter 20 ticks later for a second message (the kitchen's
  description has two; an Enter with nothing on screen does nothing), and
  the next command follows 20 ticks after that. Commands after which Laura
  walks first (opening the suitcase) wait 300 ticks. Commands with quit,
  restart, save or restore are closed with Escape, since Enter would take
  the question's default button. The parser files show that every command
  reached kParse as typed, except where the game itself changes the
  input: the input line takes only what fits its width, so the long lines
  are cut. **Decided.**
- **Laura's room before tick 24000.** At about tick 24400 Lillian comes
  in, then Ethel, and their conversation's messages close on any key, so
  they would take the commands typed then. The scenarios in that room
  stop before; the others run to just under tick 40000. **Decided.**
- **The `text` replay event skips all white space before the text**, as
  the reference reads it (`sscanf` with `"%15s %n"`); sci-vm skipped one
  space. A text cannot start with a space; no earlier recording had one.
  **Decided.**
- **No parser difference was found.** sci-text and sci-vm gave the
  reference's results for every command of the corpus and of the earlier
  scenarios (which are now compared with the parser files as well). A
  unit test pins what the corpus established (stored dashes and
  apostrophes, bytes above 127, tabs). The only fix is in the replay
  reader above.

## Milestone 6a approval (2026-10-22)

- The gate is approved as is and merged: the Linux and Windows matrix is
  green; macOS is optional.

## Milestone 6b: text windows, menus and the mouse cursor (2026-10-23)

- **The window layout is compared directly, in 1x.** Both engines write a
  layout file: one line per window (its port, frame, saved area, style,
  priority, colours and title), disposed window, picture window, measured
  text (kTextSize and every other size), text box (rectangle, alignment,
  font), drawn text line (position, width, font, colour, greyed),
  kDisplay rectangle and saved area, control (type, object, rectangle,
  state, font, highlight), inverted rectangle (selections and the edit
  cursor), menu bar, open menu, kGraph saved and restored box, and cursor
  change (shape with hotspot and all 256 pixels, shown, hidden, position
  set by the game), in screen coordinates, keyed by tick and kernel call
  (`docs/formats/replay-and-trace.md`, "Layout files"). The reference
  writes it from hooks in GfxPorts, GfxText16, GfxPaint16, GfxMenu,
  GfxCursor and `_k_GenericDrawControl` (patch 0003); sci-vm from the
  same places in its port, through `RunOptions::on_layout`. The scenario
  gate compares it as a sixth item (`tools/vm-trace/compare-layout.py`).
  **Decided.**
- **Text is compared line by line, not glyph by glyph.** Each drawn line
  gives its start, width and characters; the glyphs themselves are
  checked by the screens (and milestone 2). Line breaks show as the lines
  of a box. **Decided.**
- **kGraph SaveBox and RestoreBox are written with the rectangle actually
  saved** (clipped to the port, in screen coordinates), not every saved
  rectangle: kAnimate's saves under the sprites are left out, as the actor
  positions and the maps already cover them. **Decided.**
- **Scenarios** (`tests/scenarios/README.md`): `menus-mouse.txt` chooses
  every item of the menu bar with the mouse, `menus-keys-2.txt` the items
  the keyboard scenarios left out, mouse presses in a menu opened with the
  keyboard, and every shortcut key. The save, restore, restart and quit
  dialogs are opened and cancelled; nothing is saved (milestone 8). With
  the earlier scenarios they cover 3169 windows in 123 sizes, 1 to 18
  rows of text, titled windows, windows at the screen's edges and over
  sprites, every control type, the three cursors, hiding and showing, and
  15 cursor positions set by the game. **Decided.**
- **Windows at every N are checked on the frames of the scenarios.**
  `sci-hd-frames --check-interface 1-16` runs a replay and checks every
  frame whose interface (or picture) changed, at every N from 1 to 16
  (the largest N, `max_scale`) in original and HD mode: original mode is
  the 1x screen in blocks; in HD every interface pixel is its 1x colour in
  an N x N block; with the interface recoloured, the frame changes in
  those blocks and nowhere else; the menu bar's rows are interface. The
  frames are the program's own (`GameFrame`, kept up to date as the
  program keeps them). Unit tests run the same checks on a synthetic
  screen with an HD background that differs in every sub-pixel, and show
  that they catch a wrong sub-pixel, a block that follows a window and a
  menu bar pixel that is not interface. **Decided.**
- **The cursor.** The program decodes the cursor with sci-view (the
  decoder the interpreter uses for the layout file) and scales it N x N,
  hotspot N times. It uses the system's cursor only where the system
  shows it pixel for pixel: a window pixel density of 1 and at most 256
  pixels a side. Elsewhere (macOS Retina and scaled Wayland, where SDL
  takes cursor pixels as points; a failed SDL_CreateColorCursor) the
  presenter draws the scaled cursor over the frame, 1:1, its hotspot on
  the mouse's physical pixel; the GPU only places the finished image.
  Windows' cursors stay pixel for pixel (SDL scales them only with
  SDL_HINT_MOUSE_DPI_SCALE_CURSORS, which the program does not set; the
  cursor file format holds 256 pixels a side, N = 16). **Decided.**
- **The mouse mapping** is one tested function: the window coordinate
  times the pixel density, rounded down to the physical pixel, minus the
  letterbox, divided by N rounding down, clamped to 320 x 200. Where the
  game sets the mouse, the pointer goes to the centre of the block, which
  maps back to the same 1x pixel at every density and N. The old code
  rounded towards zero, which gave the same results only thanks to the
  clamping. **Decided.**
- **run-all-scenarios.sh takes `--jobs <n>`.** **Decided.**

## Milestone 6b approval (2026-10-23)

- The gate is approved as is and merged: the Linux and Windows matrix is
  green; macOS is optional.

## Milestone SU dropped (2026-10-28)

- The customer dropped sprite undithering (addendum A2, milestone SU)
  completely. Sprites in HD mode stay the original cels scaled N x N
  (milestone 6b), and the milestone 9 playthrough is done without
  undithering. **Decided** by the customer.
- The SU work was never merged or pushed: the local branch `milestone-su`
  (detection and threshold tuning) and its git-ignored images are
  deleted. Milestone 7 started from main without it (2026-10-23, when SU
  was first put on hold).

## MT-32 ROMs (2026-10-28)

- The customer supplied Roland ROMs. They are kept on the local machine
  only, outside the repository (`/home/user/roms/roland`), and are never
  committed or uploaded; CI runs without them. SHA-256:
  - `MT32_CONTROL.1987-10-07.v1.07.ROM`
    `a73a06c23ed38370e58a11fb1b86f7ea4c547061a60d7aa62bae446235fd2dff`
  - `MT32_CONTROL.1988-09-30.vX.XX-patched.ROM`
    `c088c026ad99e13204ac4aa157052f443c52424cbd6394a82abd60bab4c34b91`
  - `MT32_PCM.ROM`
    `d9164063f293410cf33f2f64cdcea6893b44723fe41938e07ec9aef58b406238`
  - `CM32L_CONTROL.1989-12-05.v1.02.ROM`
    `08d68695e5bff6bbcbb751d6e4453db9f5ead0b5f41e86480798a04376dfe090`
  - `CM32L_PCM.ROM`
    `0dc50190bb7ea3fb212c970346b771a511772b528801e7eda6d27efda8c40698`
- The milestone 7 checks use the MT-32 1.07 control ROM with the MT-32
  PCM ROM (the original MT-32 the game was written for); the CM-32L set
  is checked too, since the spec accepts both. **Decided.**

## Milestone 7: the OPL core (2026-10-23)

- **Nuked OPL3 is vendored at commit
  `765ec962e473aeb767e4cba74ffdc8f588ffbfe8`** (2026-08-24, version 1.8 in
  the source), the head of `master`: upstream has no tags, and this commit
  carries the 2026 fixes for 4-op channels. Only `opl3.c`, `opl3.h` and
  `LICENSE` exist upstream; they are copied unchanged into
  `third_party/nuked-opl3/`, with their SHA-256 in its `VENDORED.md`.
  The reference ScummVM carries a speed-optimised fork of the older commit
  `cfedb09` (`audio/softsynth/opl/nuked.cpp`, "1.8-fast.2"); the two later
  commits change only 4-op channels (set up through register 0x104 of the
  second bank), which an AdLib driver never writes.
  ScummVM's output is resampled to its mixer rate, so it is compared by
  ear, not sample by sample (spec section 12). **Decided.**
- **Licence:** LGPL 2.1 or later, compatible with the program's GPL v3 or
  later (LGPL 2.1 section 3, and LGPL 3 being GPL v3 plus permissions); the
  licence text goes into the release archives. Recorded in THIRD_PARTY.md.
  **Decided.**
- **Build:** a static C99 library `sci_nuked_opl3`
  (`cmake/SciThirdParty.cmake`), built in every configuration including the
  ARM64 test build without app (the C compiler is now named in the aarch64
  toolchain file too). Like SDL3 it gets the global floating-point and
  sanitizer flags but not the project's warning set, so its warnings never
  fail the build; its header is included as a system header, only in
  `src/sci-audio/src/opl3.cpp`. Only sci-audio links it. **Decided.**
- **Determinism:** with `OPL_ENABLE_STEREOEXT=0` (the default, set
  explicitly) the core has no floating point: the only `sin` and `double`
  in `opl3.c` are in the stereo extension's panning table, which is also
  its only mutable global. Everything else is constant tables and state in
  `opl3_chip`, so instances are independent. The core relies on the
  arithmetic right shift of negative values (implementation-defined in C),
  which Clang does on every target. Golden hashes are taken at the native
  rate (49716 Hz) with `OPL3_Generate`, not `OPL3_GenerateResampled`:
  resampling is free under spec section 8 and belongs to the mixer.
  **Decided.**
- **Register writes** go through Nuked's write buffer
  (`OPL3_WriteRegBuffered`), as in ScummVM's Nuked wrapper: a write applies
  at the next sample but at least 2 samples (about 40 us) after the
  previous one, close to the real bus delays a driver's wait loops honour,
  and still a pure function of the write and sample sequence. An immediate
  write is kept for tests. **Assumed:** the AdLib driver (next task) checks
  this against ScummVM's output by ear.
- The released-note floor: an operator at full attenuation whose phase
  still runs outputs -1 on its negative half-wave, as on the chip; a chip
  that never had a key-on outputs exact zeros. **Decided** (behaviour of
  the emulator, tested).

## Milestone 7: the AdLib driver (2026-10-23)

- **Ported:** `MidiDriver_AdLib` and `MidiPlayer_AdLib`
  (`engines/sci/sound/drivers/adlib.cpp`) as `sci::audio::AdlibDriver`,
  with the dual OPL2 of ScummVM's Nuked wrapper (`OPL::init`, `OPL::write`,
  `OPL::dualWrite` in `audio/softsynth/opl/nuked.cpp`); in sci-vm's player
  the driver-facing parts of `MidiParser`, `MidiParser_SCI` and `SciMusic`
  (`sendToDriver`, `allNotesOff`, `activeNote`, `sendInitCommands`,
  `initTrack`, the command queue). **Decided.**
- **SCI0 late, as for the player** (milestone 4a: ScummVM detects late SCI0
  sound for this game): play mask 0x04, 9 voices (`_numVoiceMax`; 8 only in
  SCI0 early), and the late `initTrack` header (per channel a voice count
  and a play mask; channels with the AdLib bit 0x04 get their voices). The
  early header and the SCI1 parts (velocity tables, channel volumes in the
  velocity, extra and donated voices, the SCI1.1 rhythm key map with
  `findVoiceLateSci11` and its voice queue, which nothing else reads) are
  not ported. **Decided.**
- **Device interface:** `sci::audio::MidiDevice` (send, init_track,
  set_volume, play_switch, on_timer), the part of ScummVM's `MidiPlayer`
  the SCI0 player calls. The silent driver is no device: the player drops
  the calls. **Decided.**
- **No device-dependent choice in the player.** ScummVM's player asks the
  driver for its play mask, rhythm channel (the channel filter), polyphony
  (kDoSound GetPolyphony), timer rate (the music timing) and reverb. The
  silent driver already reports the AdLib driver's values (milestone 4a),
  and `music.cpp` checks with static assertions that they equal
  `AdlibDriver`'s, so timing, signals and cues are identical with and
  without the device (tested, and the boot, intro and bedroom-wait
  scenarios are unchanged). The MT-32 driver reports other values in
  ScummVM (another play mask, a rhythm channel, more voices); the MT-32
  task decides how the timing follows the device. **Decided** for AdLib.
- **The command queue is kept.** As in ScummVM, MIDI messages and track
  inits sent from kDoSound (ScummVM's main thread) wait until the next
  music timer call, which sends them before it plays the songs; the
  global volume and the play switch reach the device at once. In ScummVM
  this is thread safety; here it gives the same order of device calls, a
  pure function of the VM's timing. **Decided.**
- **Parser behaviour towards the device:** the channel volume (controller
  7) is scaled by the song's volume; all notes off sends a note off for
  every note the parser started on the timer, then controllers 0x7B and
  0x40 on all 16 channels (SCI0 maps every channel); init commands go to
  the channels the song uses, the control channel 15 included; the track
  init is sent on every play, also of a paused song (ScummVM bug #11476).
  Not ported: state tracking and channel remapping (`remapChannels`
  returns at once for SCI0), hanging notes (SCI notes have no length and
  the parser makes no smart jumps, so none arise), SysEx (the AdLib driver
  ignores it and reports no delay). **Decided.**
- **Stereo:** ScummVM opens the driver on a dual OPL2 (`STEREO` true): the
  left chip is bank 0 of the OPL3 with its channels on the left output
  (0xC0-0xC8 ORed with 0x50), the right chip bank 1 on the right (0xA0);
  0x105 = 1 is written at once (not through the bus delay) after the reset;
  register 5 is never written and waveforms are masked to the four OPL2
  ones. Panning lowers the level of one side, as the driver does.
  **Decided.**
- **Patches:** patch resource 3; without it the bank in ADL.DRV of the
  early drivers (5684, 5720 or 5727 bytes, at 0x45A); neither is an error
  (ScummVM: "Failed to initialize sound driver"). The game has patch.003
  of 2690 bytes (96 instruments) in RESOURCE.001; its ADL.DRV (8997 bytes)
  is not a size ScummVM reads. The SCI1.1 bank (5382 bytes) is refused as
  not SCI0. `ResourceManager::file` gives the ADL.DRV bytes. **Decided.**
- **Out-of-bounds reads of ScummVM are skipped:** a voice that never played
  (channel -1) or has no patch yet (before the first track init) writes no
  frequency or level, where ScummVM reads outside its arrays. No sound of
  the game reaches these paths in the reference's normal flow (every play
  starts with a track init). **Assumed.**
- **Rhythm:** the SCI0 AdLib driver has no rhythm channel
  (`hasRhythmChannel` false, the OPL rhythm register 0xBD stays 0); channel
  9 plays only with its AdLib play mask bit, as a melodic channel.
  **Decided.**
- **Rendering for the golden hashes:** `sci::vm::render_sound` (sci-vm,
  where the player is) plays one sound resource from its start on a new
  driver: a music timer call, then the OPL frames up to the next call
  (frame `floor(n * 49716 / 250)` starts call n), as ScummVM's OPL stream
  calls its callback and then generates; ScummVM's fixed-point step at the
  mixer rate is not reproduced (resampling is free). It stops when the
  song ends (no release tail) or at the frame limit, with loop count 1 and
  the game's start master volume 11. `tools/sound-render` prints the frames,
  calls, peak and SHA-256 per sound; all 119 sound resources of the game
  render, none silent (the quietest, sound 99, peaks at 35; 84 end within
  20 seconds), the same twice. **Decided** (the next task picks the hashed
  set).
- The device is attached in the program by the next task ("Milestone 7:
  mixer and audio output").

## Milestone 7: the OPL comparison and the sound golden hashes (2026-10-28)

- **OPL register logs in both engines.** Patch 0003 writes every register
  write of ScummVM's AdLib driver (`MidiDriver_AdLib::setRegister`) to
  `SIERRA_HD_OPL=<file>`; `sci-vm-trace --opl <file>` plays the music on
  `AdlibDriver` (otherwise the silent driver) and writes ours from the
  driver's write hook, at the same level: the value the driver writes to
  the port, before the dual OPL2's masks. Format and time key in
  `docs/formats/replay-and-trace.md`, "OPL files". **Decided.**
- **Time key: the music timer call.** Both engines run the music timer on
  the virtual clock, once per 4000 us, on the interpreter's thread, so the
  number of timer calls made is the same at every point of a run; a write
  carries the count when it is made (0 for the driver's start-up). The
  OPL sample position was not used: the reference's chip generates in the
  audio thread at the mixer's pace, not on the virtual clock. **Decided.**
- **The reference's note ageing moves onto the virtual clock.** ScummVM
  ages the notes (by which `findVoice` picks the voice for a new note) in
  `MidiDriver_AdLib::onTimer`, called by the OPL chip's callback in the
  audio thread; in the deterministic mode that callback now does nothing
  and the virtual clock calls the ageing right after each music timer call,
  as our `Music::on_timer` calls `AdlibDriver::on_timer`. Without it the
  voice choice would depend on the host's audio timing. **Decided.**
- **Result:** the logs match in boot (3600 ticks, seed 0: 1286 writes over
  timer calls 0 to 14933, 200 key-ons), intro (30000 ticks: 81604 writes
  over timer calls 0 to 118318, 11622 key-ons) and bedroom-wait (20000
  ticks: 1040 writes over timer calls 0 to 13141, 118 key-ons), with all
  six other comparisons matching too. No port bug showed up. The
  comparison is the seventh in `run-scenario.sh` (on by default,
  `SIERRA_HD_COMPARE_OPL=0` skips it). **Decided.**
- **No per-sound comparison with the reference:** ScummVM cannot play one
  sound resource alone without new engine code (a sound object, kDoSound
  calls, the main loop), so the in-game comparison stands for it; the
  intro plays the game's main themes. **Decided.**
- **Golden hashes:** all 119 sound resources, each rendered with
  `render_sound` for its first 20 seconds (84 end sooner), SHA-256 of the
  samples at 49716 Hz in `tests/golden/sounds.txt`
  (`sci-sound-render --golden <file> [--check]`). They match on x86-64
  (release, about 35 s; the same as the debug build) and on ARM64 (the
  cross-aarch64 build under qemu-user, about 3 minutes). Like the picture
  hashes they need the game and run locally, not in CI. **Decided.**

## Milestone 7: mixer and audio output (2026-10-28)

- **Who runs what.** The interpreter, the music player, the AdLib driver,
  the OPL chip and the mixer all run on the main thread; SDL's audio
  thread only plays (spec section 4.3). After each music timer call the
  chip generates the frames up to the next call (frame
  `floor(n * 49716 / 250)` starts call n, as in `render_sound`), the mixer
  converts them to the device's rate, and they are put into an
  `SDL_AudioStream` bound to the default playback device
  (`src/app/src/audio_output.cpp`, a `MidiDevice` wrapping the driver).
  The stream, locked inside SDL, is the only thing the threads share;
  there is no state of ours that both touch. The other design (the audio
  thread generating the chip's output and applying register writes queued
  with sample stamps) was not taken: it needs our own lock-free queue and
  gives no lower latency, since the writes only exist once the main
  thread has run the timer. **Decided.**
- **The timer calls follow real time during waits.** Virtual time passes
  only in `Clock::advance`, which calls the host's wait and then ran all
  the timer calls of the wait at its end: a 100 ms kWait would make 100 ms
  of music at once, after 100 ms of nothing, and need over 100 ms of
  queue. Now the host calls `Game::pass_wait(elapsed)` while it waits, and
  `Clock::run_music_in_wait` runs the calls due by then. The interpreter is
  stopped in the wait and the timer does not read the clock, so it sees
  the same calls in the same order; the deterministic runs have no wait
  hook and do not change (unit test
  `clock_runs_the_music_timer_during_a_wait`; scenarios below).
  **Decided.**
- **Clocks and latency.** The timer calls follow the wall clock
  (`SDL_GetTicks`, which the host's virtual time tracks); the device plays
  by its own crystal. The stream's queue is held near a target of
  max(60 ms, three device buffers): `queue_speed` changes the mixer's
  resampling step in proportion to the queue's error, by at most 0.2 %
  (3.5 cents, inaudible), which absorbs any drift the two clocks can have.
  New audio is dropped above twice the target (after a stall the
  interpreter then catches up on, so the music stays in step with the
  game), and the device plays silence when the queue runs dry (a stall of
  the main thread longer than the queue). The queue starts filled with the
  target of silence. Latency from a timer call to the speaker is the
  queue plus SDL's device buffer: about 60 to 100 ms (70 ms plus a 23 ms
  buffer with the 1024-frame buffers SDL reports at 44100 Hz). Pausing
  is the game's own (kDoSound pause stops the notes; the timer and the
  output go on). **Decided.**
- **Resampler: 4-point cubic Hermite (Catmull-Rom),** in double (spec
  section 5.3 allows no float). Four multiply-adds per sample; its passband
  is much flatter than linear interpolation's (which ScummVM's default
  rate converter uses, and which loses about 3 dB at a third of the input
  rate), and the OPL's energy above 22 kHz, which a 44100 Hz output folds
  back, is small. A windowed-sinc polyphase filter would cost several
  times more for no audible gain on this signal. The mixer also applies
  the volume and clips to 16 bits; it is not deterministic across
  platforms and does not need to be (spec section 8). Tests: output length
  at 22050 to 96000 Hz, a 440 Hz sine within 4 steps at 48000 Hz, a 5 kHz
  sine within 1 % in level at 44100 Hz, silence, volume, clipping, the
  speed correction (`tests/unit/audio/mixer_test.cpp`). **Decided.**
- **Configuration** (format 1 unchanged; a file without the new keys gets
  the defaults): `music = adlib | mt32 | off` (default `adlib`; `mt32`
  falls back to AdLib with a warning until the MT-32 task; `off` is the
  silent driver, with the same timing and no audio device) and
  `volume = 0..100` (per cent, default 100). Without an audio device, or
  without patch.003, the program logs a warning and plays on the silent
  driver. Under CI or Xvfb `SDL_AUDIODRIVER=dummy` plays into a device
  that consumes at the real rate. **Decided.**
- **Threads under TSan:** `sci-audio-output-tests` (label `threads`) opens
  the output on SDL's dummy driver and drives the driver from the main
  thread for half a second of timer calls while SDL's thread pulls: clean
  under TSan (three runs), and no frame dropped. **Decided.**
- **Smoke test** (`SDL_AUDIODRIVER=disk`, Xvfb, debug build, 40 s with the
  container's start-up): SDL reported 44100 Hz with 1024-frame buffers,
  queue 3072 frames. The file held 24.7 s of 16-bit stereo: silence until
  the title music started at 1.06 s, then music to the end (RMS about 1100
  to 1400 per half second, peak 4506, no clipped sample, zero crossings
  that change with the notes). One gap: 57 ms of silence at 2.89 s cut into
  a waveform that continues unchanged after it, an underrun (the main
  thread stalled longer than the queue early in the title, in the debug
  build); no other run of exact zeros. The customer listens to it on real
  hardware later. **Assumed** (no sound card here).
- **Game behaviour unchanged:** boot (3600 ticks, seed 0) and intro
  (30000 ticks) match the reference in all seven comparisons, the OPL
  writes as before (boot 1286 writes over timer calls 0 to 14933, intro
  81604 over 0 to 118318). **Decided.**
- **Open:** a long stall of the main thread (for example an HD picture
  analysed on room entry, up to about 0.5 s in a slow build) still empties
  the queue and is heard as a gap; the music then resumes in step.

## Milestone 7: mt32emu (2026-10-28)

- **libmt32emu 2.8.3 is vendored**, tag `libmt32emu_2_8_3`, commit
  `3b05ec276f9e605af86b0eaef7f5eda43477a31f` (2026-06-06), the latest
  library release (the tags were renamed from `mt32emu_x_y_z` at 2.8.0; the
  reference ScummVM carries 2.7.0). Only the synthesiser core is copied,
  unchanged, from upstream's `mt32emu/src/` to `third_party/mt32emu/mt32emu/`:
  no file streams, MIDI stream parser, resamplers, C interface, version
  tagging or tests. A hand-written `config/config.h` replaces the one
  upstream's CMake generates (static, C++ API only, no version tagging).
  SHA-256 of every file in `third_party/mt32emu/VENDORED.md`. **Decided.**
- **Licence:** LGPL 2.1 or later, compatible with GPL v3 or later as for
  Nuked OPL3 (THIRD_PARTY.md); its `sha1/` is BSD 3-clause, also
  compatible. `COPYING.LESSER.txt` and the SHA-1 notice go into the release
  archives (`scripts/package-release.sh`). **Decided.**
- **Build:** the static C++20 library `sci_mt32emu`
  (`cmake/SciThirdParty.cmake`), on every platform including the ARM64 test
  build, with the global floating-point and sanitizer flags, its warnings
  off (`-w`, `/w` under clang-cl) and its headers as system headers, linked
  privately by sci-audio and included only in `src/sci-audio/src/mt32.cpp`.
  C++20 because the renderer shifts negative values left
  (`Synth.cpp`), defined since C++20 and undefined before: built as C++17,
  the ASan + UBSan build stopped there with the ROMs. As C++20 the
  sci-audio tests are clean under ASan + UBSan with and without ROMs.
  The code has no `try` or `throw`, so it builds without exceptions. For
  clang-cl the only MSVC-specific code is `#pragma pack` and
  `__declspec(align(1))` in `Structures.h` (both accepted by clang-cl) and
  the DLL export attributes, off in a static build; `_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES`
  is defined as upstream does for MSVC. **Assumed:** the Windows build is
  checked by the next Windows CI run (not built here).
- **The wrapper** `sci::audio::Mt32` (`sci/audio/mt32.hpp`) takes the ROMs as
  bytes: sci-audio does no file I/O, so app reads the files from the
  configured folder (the driver task). mt32emu identifies them by size and
  SHA-1; the model is CM-32L for its `ctrl_cm32l*` control ROMs and MT-32
  otherwise. Errors: an unknown file or a ROM of the wrong kind
  (InvalidData), half a ROM image, or a pair that does not belong together
  (Unsupported). Output at 32000 Hz (coarse analogue mode, as in ScummVM)
  through mt32emu's integer renderer (its default); MIDI at once
  (`playMsgNow`) or queued at a frame, with mt32emu's MIDI cable delay
  (1 ms for 3 bytes) for short messages. mt32emu's report handler, which
  prints to stdout, is replaced by one that keeps the display text only.
  **Decided.**
- **ROMs identified** (customer's set, "MT-32 ROMs" above): MT-32 control
  1.07 as `ctrl_mt32_1_07` "MT-32 Control v1.07" with `pcm_mt32` "MT-32
  PCM ROM"; CM-32L control 1.02 as `ctrl_cm32l_1_02` "CM-32L/LAPC-I Control
  v1.02" with `pcm_cm32l` "CM-32L/CM-64/LAPC-I PCM ROM". The MT-32 control
  ROM with the CM-32L PCM ROM is refused (mt32emu expects the 512 KB PCM
  ROM). **Decided** (tested).
- **Floating point:** the integer renderer uses floating point only to
  build mt32emu's tables at start-up (`log`, `exp2`, `sin` rounded to
  integers), the pan factors, the output gain factors and the MIDI cable
  delay in samples. The spec requires determinism of the OPL core only
  (section 8); mt32emu is not hashed. **Decided.**
- **Determinism:** mt32emu calls the C library's `rand()` (`TVP.cpp`, a
  0 to 3 sample jitter of the pitch timer, which the real unit's MCU
  shows), so its output is a function of the ROMs, the input and
  `rand()`'s state: two synthesisers fed the same in one process differ,
  and after `srand(1)` they are the same (tested on x86-64 and on ARM64
  under qemu). The C libraries' `rand()` differ, so the output differs
  between platforms. The project's own code never calls `rand()` (a
  clang-tidy rule), so the game logic is not affected; mt32emu calls it on
  the thread that renders, which is the main thread. **Decided**; MT-32
  output is never hashed. **Open:** whether the MT-32 driver's tests want
  bit-exact output (they can seed `rand()` first, as the unit test does).
- **Behaviour seen in the tests** (MT-32 1.07): idle output is exact
  zeros; a released note decays to a floor of about 2 steps that stays;
  master volume 0 attenuates by about 44 dB, not to silence; a key struck
  again while its note still sounds keeps that note's level.
- **ROMs in the tests:** `SIERRA_HD_MT32_ROMS` names the folder; without it
  the ROM tests pass with a "skipped" note (CI). Locally the folder is
  mounted read-only (`SIERRA_HD_MOUNTS=/home/user/roms/roland:/roms`); no
  file from it is in the worktree. **Decided.**
- **Not yet:** the SCI0 MT-32 driver (ScummVM's `MidiPlayer_Midi`), the
  ROM path in the configuration and the fallback to AdLib without ROMs
  (spec section 8) are the next task.

## Spec items carried forward

- Edge coverage is blended in the same sRGB-encoded space as the pairs, with
  no linear-light conversion (spec section 6.5). **Decided** (confirmed at
  the milestone 3 gate).
- ScummVM's Event Recorder is evaluated as the basis of the replay format
  before that format is designed (spec section 7, needed from milestone 4a).
  **Decided** at milestone 4a: not used (see there).

## Milestone 0 placeholders

- Each library without real content contains only a `component_name()`
  function, which the program's `--version` output uses to show that
  everything links. These functions are removed as soon as the library has
  real content (sci-resource: milestone 1; sci-pic, sci-view and sci-text: milestone 2;
  sci-core, sci-vector and sci-render: milestone 3).
