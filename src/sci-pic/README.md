# sci-pic

PIC resource parsing into a command stream, and the exact 1x rasteriser (path 1, spec section 6.2) producing the visual, priority, control and pair maps.

Milestone 0 contains only the library skeleton; the content arrives with the
milestones named below.

## Invariants

- At N=1 with enhancements off, the visual, priority and control maps are bit-identical to ScummVM, compared as palette-index buffers.
- Game logic always reads the maps of path 1.
- Integer arithmetic only.
