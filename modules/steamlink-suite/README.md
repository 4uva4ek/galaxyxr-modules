# Galaxy XR Steam Link Suite

Author: 4uva4ek. Version 0.1.6. Requires KernelSU GXR 1.0.11 or later for reviewed migration.

Components: openxr-velocity-frame, steamlink-pose-time, openxr-priority. Controller
Extrapolation was removed in 0.1.6; the suite still replaces (restores and removes)
the standalone `gxr_controller_extrapolation` module during migration.

Targets (0.1.3): packages starting with `com.valvesoftware.steamlinkvr` (Steam Link
and renamed copies such as `.x_23`) and `dev.chronolink.client` (ChronoLink); other
apps are never patched. Velocity Frame acts on the pinned OpenXR runtime in both
clients. Pose Time is Steam Link only, with scene profiles for
2.0.20 and 2.0.23 selected by live code bytes. App priorities have a Steam Link profile; ChronoLink is reported
unchanged until its threads are profiled.

Automatic-only, installed disabled. KernelSU is the master activation preference.
WebUI saves explicit tuning; it never activates a disabled suite.
Native/vendor compatibility signatures and child recovery owners are preserved.
A suite supervisor coordinates dependency order and stops the whole suite on failure.

Build: `python modules/steamlink-suite/build.py --ndk28 PATH --ndk29 PATH --runtime FILE --scene SCENE_2_0_20 --scene SCENE_2_0_23` (clean `libvrlink_scene.so` of each supported version).

Replaces: gxr_openxr_velocity_frame, gxr_steamlink_pose_time, gxr_stream_cpu_relief, gxr_controller_extrapolation. Haptic Main is separate.

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

## Soft-restart handoff (0.1.5 source, first released in 0.1.6)

A KernelSU soft restart stops Android and force-stops Steam Link. Up to 0.1.4 App
Priorities kept querying the framework during that window, failed ("Wakefulness
unavailable") and the supervisor quarantined the whole suite. Since 0.1.5 the
supervisor treats the restart gate (`busy` or a new generation) as a planned
handoff. Components restore their state and exit with a handoff code when
`busy` exists or `sys.boot_completed` is reset. The suite clears `auto.pending`
without a failure guard, and the service stage after the restart starts it again.
Errors outside a restart still quarantine the suite. Device validation of an
active-to-restart cycle is pending.
