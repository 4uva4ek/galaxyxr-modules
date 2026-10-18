# Galaxy XR Steam Link Suite

Author: 4uva4ek. Release 0.1.1. Requires KernelSU GXR 1.0.11 or later for reviewed migration.

Components: openxr-velocity-frame, steamlink-pose-time, stream-cpu-relief, controller-extrapolation.

Automatic-only, installed disabled. KernelSU is the master activation preference.
WebUI saves explicit tuning; it never activates a disabled suite.
Native/vendor compatibility signatures and child recovery owners are preserved.
A suite supervisor coordinates dependency order and stops the whole suite on failure.

Build: `python modules/steamlink-suite/build.py --ndk28 PATH --ndk29 PATH --runtime FILE --scene FILE`.

Replaces: gxr_openxr_velocity_frame, gxr_steamlink_pose_time, gxr_stream_cpu_relief, gxr_controller_extrapolation. Haptic Main is separate.

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
