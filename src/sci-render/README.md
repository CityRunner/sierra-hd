# sci-render

HD rasteriser and compositor: vector scenes at 320N x 190N, sprites, priority occlusion, text and windows (spec sections 6.5, 6.6, 6.8, 6.9).

Milestone 0 contains only the library skeleton; the content arrives with the
milestones named below.

## Invariants

- No external dependencies (spec section 4.2).
- Output is bit-identical on every supported machine; colours, blending and hashes are integer.
- Results never depend on the number of threads or on scheduling.
