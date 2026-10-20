# SystemUI Optimization

## 0.4.0-experimental — 2026-10-20

- 0.4.0-experimental: combine retained wait/depth fixes with panel coalescing.
  Validate both components, roll back failed activation, reject mixed status/on
  and restore known partial states on off. Pin all three library hashes.
  Add verified STOCK reporting and installer-facing off without changing the
  status-only Action button. Keep disabled automatic packaging.
- Combined bench CPU: 55.30% before, 53.32% with all fixes, 59.64% after.
  Notification, volume-dialog and contrast-dialog show/hide transitions passed.
  CPU savings vary; not all critical dialog types or visual/input behavior have
  been validated. Published only in the experimental catalogue as a prerelease.

## Local development history before the first public release
- Add a separate panel-coalescing prototype; fix identity matching for reused
  NodeHandle proxies. Device traces confirm removal of 12 hidden-panel toggle
  cycles per frame while notification, volume and contrast-dialog transitions
  still work. Two final A/B/A runs show small CPU savings (1.65/3.35 points of
  one core). Not integrated into the automatic package; preserve both 0.3.0
  fixes and document remaining lifecycle/critical-dialog validation limits.
- 0.3.0-experimental: combine duplicate-wait removal and selected-depth
  release/submit correction in one validated apply/restore transaction, as
  requested. Reject mixed states and protect both functions during writes.
- 0.2.0-experimental: remove two redundant outer swapchain waits, preserving
  the inner acquire/wait and failure behavior. Active A/B/A confirms call-order
  errors disappear and return on rollback; CPU savings are not established.
  Keep the 0.1.0 depth candidate separately.
- Start a dedicated module investigation with a read-only host baseline collector.
- Record process CPU identity checks, graphics memory and XR scene evidence.
- Add pinned private-memory schema key/number fast paths with original fallbacks.
- Retain the independent selected-depth release/submit candidate and its promising
  active A/B/A result (80.21 -> 71.96 -> 84.97% of one core); benefit is not yet
  confirmed by repetition, and call-order errors persisted.
- Add bounded runtime experiments, exact restoration, idle refusal and ARM64 tests.
- Prepare disabled automatic packaging with process/boot identity and failure guards.
- No published release or update feed; no claim of validated broad CPU savings.
