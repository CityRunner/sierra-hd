# sci-vm

The SCI0 bytecode interpreter: classes, objects and selectors, kernel functions, animation and motion (spec section 7).

Milestone 0 contains only the library skeleton; the content arrives with the
milestones named below.

## Invariants

- The ScummVM state model (segment and offset registers, segments, heap) is kept.
- Time and randomness come from the virtual clock and the ported random source, so traces can match ScummVM.
- Further dependencies (graphics, sound) are added as kernel functions need them, keeping the graph acyclic.
