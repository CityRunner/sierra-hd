# sci-core

Shared foundations for every other library: the error type, invariant checks,
bounds-checked binary reading, the floating-point profile check, the logging
facility, checked integer arithmetic with rationals and the orientation
predicate (`exact.hpp`), explicit rounding (`rounding.hpp`), SHA-256, and the
task-execution interface (`executor.hpp`). Deterministic replacements for
platform-dependent standard library facilities come when needed.

## Invariants

- No external dependencies and no I/O. Anything that needs the outside world
  (log output, assertion reporting) goes through a sink or handler that `app`
  installs.
- Failures on input are `sci::core::Result<T>` (`std::expected<T, Error>`);
  broken invariants are `SCI_ASSERT`, which stays enabled in release builds.
- Everything here is deterministic: no results depend on the platform, the
  standard library implementation or the number of threads.
