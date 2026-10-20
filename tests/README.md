# tests

- `support/`: the minimal test harness (`SCI_TEST`, `SCI_CHECK`).
- `unit/`: unit tests, one executable per library.
- `fuzz/`: libFuzzer targets with their seed corpora. Without `SCI_FUZZ`,
  each target replays its corpus as a CTest test.

Tests that need the game files (golden hashes, comparisons with the
reference ScummVM) are added from milestone 1, with instructions for running
them locally.
