# ScummVM patches

Patches applied to the pinned ScummVM revision by
`tools/reference-scummvm/build.sh`, in name order. Each file is a unified
diff against that revision, named `NNNN-short-description.patch`. It starts
with a comment saying what it adds and which milestone needs it. The build
exports the tree again whenever a patch changes.

| Patch | Milestone | Adds |
|---|---|---|
| `0001-dump-resources.patch` | 1 | With `SIERRA_HD_DUMP_RESOURCES=<dir>`, dump every resource as the resource manager loads it, then exit |

Planned: buffer dumps of the visual, priority and control maps (milestone 2),
kernel-call traces, and the deterministic mode with a fixed seed, virtual
clock and replayed input (milestone 4).
