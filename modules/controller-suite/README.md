# Galaxy XR Controller Suite

Author: 4uva4ek. Local candidate 0.1.2 (public release remains 0.1.1). Requires KernelSU GXR 1.0.11 or later for reviewed migration.

Components: controller-log-relief, controller-whitening, controller-timestamp-precision, hal-cache-bypass, tail-velocity.

Automatic-only, installed disabled. KernelSU is the master activation preference.
WebUI saves explicit tuning; it never activates a disabled suite.
Native/vendor compatibility signatures and child recovery owners are preserved.
A suite supervisor coordinates dependency order and stops the whole suite on failure.

Build: `python modules/controller-suite/build.py --ndk28 PATH --ndk29 PATH --runtime FILE --scene FILE`.

Replaces: gxr_controller_whitening, gxr_controller_timestamp_precision, gxr_hal_cache_bypass. Haptic Main is separate.

WebUI language follows the headset locale (English fallback), with System / English / Russian selection. Version 0.1.1 changes the UI only; existing tuning and native behavior are unchanged.

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
behavior are not established by these checks. Publication was authorized by the
user on 2026-10-18; no new device activation is part of publication.

## Local 0.1.2 migration

Update both suites while disabled before enabling either. Controller Suite now owns
the HAL-wide LogLevel=0 gate. Steam Link Suite no longer changes HAL memory; its
priority component discovers visible third-party OpenXR applications, verifies UID
and runtime mapping, and applies only a known thread profile. The initial profile
is Steam Link; unknown apps are reported and unchanged. Generic Thread-9 is excluded.
Priorities restore on background/sleep, exit or disable; realtime policies are untouched.
This does not request OpenXR CPU/GPU BOOST. New live behavior is not yet validated.
