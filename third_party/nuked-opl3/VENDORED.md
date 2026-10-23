# Nuked OPL3, vendored

- Upstream: https://github.com/nukeykt/Nuked-OPL3
- Version: 1.8 (the version line of `opl3.c`), commit
  `765ec962e473aeb767e4cba74ffdc8f588ffbfe8` (2026-08-24, "remove NEW bit
  checks for 4-op channels"), the head of `master` when vendored on
  2026-10-23. Upstream has no release tags; this commit is the latest and
  carries the two 4-op fixes of 2026, which the 2024 commits do not.
- Licence: LGPL 2.1 or later (`LICENSE`; the notice in `opl3.c` and
  `opl3.h`)

Copied unchanged, with their SHA-256:

| File | SHA-256 |
|---|---|
| `opl3.c` | `59eb873fdb6d52bc7977a0fcbb97c1bae03dd71cc88e3acddbcc01b7121bfe03` |
| `opl3.h` | `a84266b8d71a4929f15f573afbe407fd24310b77757d855835dacabf68763679` |
| `LICENSE` | `20c17d8b8c48a600800dfd14f95d5cb9ff47066a9641ddeab48dc54aec96e331` |

Left out: nothing else is in the upstream repository.

Built by `cmake/SciThirdParty.cmake` as the static C library
`sci_nuked_opl3`, with `OPL_ENABLE_STEREOEXT=0` (the default): the stereo
extension is the only part that uses floating point (`sin` for its panning
table) and the only global state (that table). Without it the core is
integer-only and all its state is in `opl3_chip`.

To update, copy the same files from the new commit, check the SHA-256 and
record the new commit here, in `THIRD_PARTY.md` and in `docs/ASSUMPTIONS.md`;
the OPL golden hashes (`tests/unit/audio/opl3_test.cpp`) show whether the
output changed.
