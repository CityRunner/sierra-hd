# tests

- `support/`: the minimal test harness (`SCI_TEST`, `SCI_CHECK`).
- `unit/`: unit tests, one executable per library.
- `fuzz/`: libFuzzer targets with their seed corpora. Without `SCI_FUZZ`,
  each target replays its corpus as a CTest test.

Seed corpora are hand-made; `fuzz/make_seeds.py` regenerates the
sci-resource ones. libFuzzer runs write new inputs to a scratch directory
(`scripts/run-fuzzers.sh`), never into the corpora.

Tests that need the game files run locally, never in CI:

- Resources (milestone 1): `tools/resource-compare/README.md`.
- Pictures, views and fonts (milestone 2): `tools/graphics-compare/README.md`.
