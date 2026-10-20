# sci-vector

Vector analysis of pictures (path 2, spec sections 6.3 and 6.4): from what
path 1 drew (`sci::pic::DrawTrace`) to a vector scene per plane. The
algorithm is described in `docs/hd-pipeline.md`.

- `build_scene.hpp`: `build_scene` and the statistics of the replacement
  report (gap closures, fits, fallbacks).
- `scene.hpp`: the scene, plain data for sci-render: faces with values, scan
  edges, and layers (strokes, ellipses, boxes, blocks) in drawing order.

Hints (spec section 6.7) arrive at milestone 3a.

## Invariants

- No external dependencies (spec section 4.2).
- Topology is exact (int64 rationals in doubled pixel coordinates, every
  product checked); fitting uses double under the floating-point profile
  with only +, -, *, / and sqrt (spec section 5.3).
- Every face takes its value from path 1's map; where its pixels disagree,
  it falls back to 1x blocks. Fits never change which pixel lies in which
  face.
- Results never depend on the number of threads; parallel work goes through
  the task-execution interface supplied by `app`.
