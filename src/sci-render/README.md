# sci-render

Rasterisation of vector scenes at 320N x 190N (spec sections 6.5 and 6.6),
original mode (spec section 6.1), and the acceptance check of the HD
pipeline (spec section 6.4). The compositor (sprites, priority occlusion,
text and windows, spec sections 6.8 and 6.9) follows with the VM.

- `rasterize.hpp`: `SceneSampler`, `render_hd` (16 x 16 samples per pixel),
  `render_priority` (the HD priority map), `reduce_to_1x`.
- `colors.hpp`: the EGA palette and the build-time table of pair colours.
- `original.hpp`: the original mode frame and EGA indices as RGBA.
- `acceptance.hpp`: `check_acceptance`.
- `reference.hpp`: `reference_value`, the simple reference the sampler is
  tested against.

## Invariants

- No external dependencies (spec section 4.2).
- Output is bit-identical on every supported machine; colours, blending and
  hashes are integer, geometry is double under the floating-point profile.
- Every sample lies in exactly one face; layers paint over faces in drawing
  order.
- Results never depend on the number of threads or on scheduling.
- At N = 1, original mode is path 1's visual map.
