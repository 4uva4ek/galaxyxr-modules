# Galaxy XR Controller Suite

## 0.1.4 - 2026-10-19

- A KernelSU soft restart while the suite is active is now a planned handoff. The supervisor notices the restart (new generation, busy, ready removed or `sys.boot_completed` reset), restores all components and clears its pending marker without a failure guard. The service stage after the restart starts the suite again with the checkbox unchanged.
- Tail Velocity exits with a distinct handoff code on a restart gate change instead of reporting a component failure. Read-only preflight and restoration also work while Android reports boot incomplete; model and library hash checks remain.
- Startup waits briefly for a previous worker that is still finishing a handoff.
- A controller service exit/restart, a failed health check or a failed restoration still disables the suite and blocks automatic retry in that kernel boot.
- The suite keeps the first failure reason (for example the failing component) instead of overwriting it with a generic worker message.
- Shares the supervisor with Steam Link Suite 0.1.6. 0.1.3 was a local device build only and was not published; update from 0.1.2 or 0.1.3 while disabled.
- Validation: package diff against 0.1.2 (other component payloads byte-identical), layout/contract checks and host tests of the restart handoff passed; a local 0.1.3 build installed disabled with verified stock restoration. An active-to-soft-restart cycle has not yet been validated on the device.

## 0.1.2 - 2026-10-18

- Move HAL-wide LogLevel=0 ownership to Controller Suite.
- Split app priority management into a visible-OpenXR profile mechanism; Steam Link is the initial supported profile.
- Restore priorities on background/sleep and exclude ambiguous Thread-9.
- Update both suites disabled before enabling. Soft-restart startup, HAL ownership and active Steam Link priorities verified on device.

## 0.1.1 - 2026-10-18

- First public suite release, consolidating the existing components under one activation preference and WebUI.
- Declare replaced modules for reviewed migration in KernelSU GXR 1.0.11 or later; install disabled.
- English/Russian WebUI follows the headset locale, with a manual language override.
- Preserve existing component tuning, compatibility checks and recovery ownership.

See the module README for components, migration and validation limits.
