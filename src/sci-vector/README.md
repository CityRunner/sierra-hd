# sci-vector

Vector analysis of PIC command streams: planar graph, curve fitting, hints and fallbacks; produces the vector scene (spec sections 6.3 to 6.7).

Milestone 0 contains only the library skeleton; the content arrives with the
milestones named below.

## Invariants

- No external dependencies (spec section 4.2).
- Topology uses exact integer arithmetic on the original 1x coordinates; refinement uses double under the floating-point profile (spec section 5.3).
- Results never depend on the number of threads; parallel work goes through the task-execution interface supplied by `app`.
