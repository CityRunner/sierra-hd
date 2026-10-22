# reports

One report per milestone and sub-gate (`milestone-N.md`): what was done, test
results, the CI minutes used, deviations from the spec, and open questions.

The section "CI minutes" (from milestone 4b on; `docs/ASSUMPTIONS.md`, "CI
cost policy") gives the minutes of the milestone's branch by platform,
measured with `scripts/ci-minutes.py --branch <branch>`, with the included
minutes and list price; any run of macOS, Windows or native ARM64 between
gates and why; and what could reduce the minutes further.

Images derived from the game art go to `reports/images/`. That directory is
git-ignored and never committed.
