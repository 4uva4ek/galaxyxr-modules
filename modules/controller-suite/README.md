# Galaxy XR Controller Suite

Author: 4uva4ek. Version 0.1.4. Requires KernelSU GXR 1.0.11 or later for reviewed migration.

Components: controller-log-relief, controller-whitening, controller-timestamp-precision, hal-cache-bypass, tail-velocity.

Automatic-only, installed disabled. KernelSU is the master activation preference.
WebUI saves explicit tuning; it never activates a disabled suite.
Native/vendor compatibility signatures and child recovery owners are preserved.
A suite supervisor coordinates dependency order and stops the whole suite on failure.

Build: `python modules/controller-suite/build.py --ndk28 PATH --ndk29 PATH --runtime FILE --scene FILE`.

Replaces: gxr_controller_whitening, gxr_controller_timestamp_precision, gxr_hal_cache_bypass. Haptic Main is separate.

WebUI language follows the headset locale (English fallback), with System / English / Russian selection. Version 0.1.1 added language selection; version 0.1.2 changes HAL log ownership and app priority management as described below.

## Installation and migration

Update KernelSU GXR first. In Modules -> Sources, select this suite from
https://github.com/4uva4ek/galaxyxr-modules and review the replacement prompt.
The Manager restores and removes the listed standalone modules before installing
the suite disabled. Enable it explicitly, then use the Manager soft restart.
Action reports status; WebUI edits settings. Haptic MAIN remains separate.

Validation: source/package contracts, checksums, settings bounds and EN/RU
language behavior passed. The Manager migration path passed 19 installer tests;
device inspection after user installation confirmed the seven replaced modules
were absent. Combined-suite physical tracking quality and long-term lifecycle
behavior are not established by these checks. Device validation for 0.1.2 is described below.

## 0.1.2 migration

Update both suites while disabled before enabling either. Controller Suite now owns
the HAL-wide LogLevel=0 gate. Steam Link Suite no longer changes HAL memory; its
priority component discovers visible third-party OpenXR applications, verifies UID
and runtime mapping, and applies only a known thread profile. The initial profile
is Steam Link; unknown apps are reported and unchanged. Generic Thread-9 is excluded.
Priorities restore on background/sleep, exit or disable; realtime policies are untouched.
This does not request OpenXR CPU/GPU BOOST. Device checks after a soft restart verified
HAL LogLevel=0, the existing live hooks, and six owned Steam Link threads at nice=-20.
See [validation](../../docs/SUITES-0.1.2-VALIDATION.md) for scope and limits.

## Soft-restart handoff (0.1.4)

Up to 0.1.2 a KernelSU soft restart while the suite was active ended with the suite
disabled: the restart resets `sys.boot_completed`, Tail Velocity stopped on that,
the supervisor recorded it as a component failure, and the restoration check then
refused to read while boot was incomplete. The patches were restored, but
`auto.pending` and `state/failure` blocked the next start.

Since 0.1.4 the supervisor treats the KernelSU restart gate (new generation, `busy`,
missing `ready` or `sys.boot_completed` reset) as a planned handoff. It restores
every component, clears `auto.pending` without a failure guard and lets the service
stage after the restart start the suite again. The checkbox is not changed.
A controller service exit or restart, failed health check or failed restoration
remain failures and still block automatic retry in that kernel boot. A framework
restart outside KernelSU is not a handoff: KernelSU dispatches no service stage
for it, so a suite stopped by such a restart starts again only after the next
soft restart. This case was not tested.
