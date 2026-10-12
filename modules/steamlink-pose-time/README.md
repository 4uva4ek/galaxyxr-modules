# Steam Link Pose Time

Author: **4uva4ek**. Version **0.1.2**. Requires the update-aware KernelSU GXR Manager (1.0.6 or later).

Preserves the epoch of the controller pose actually consumed by Steam Link.
The app's standard signed microsecond field at pose block `+0x54` carries the
relative epoch to vrlink. Position, orientation, velocity, tracking state, HMD
and the shared packet header remain unchanged. The correction is
`(consumed_slot_epoch - packet_header_epoch) / 1000` microseconds after clock
conversion, with no learned mean, warmup or chosen millisecond compensation.
This is the full-epoch RAM version approved by the user on 2026-10-13.

Requires the supported **Steam Link 2.0.20 main package** and **Velocity Frame**.
Other versions and renamed copies are excluded. Controller Extrapolation and
foreign hooks are rejected. The source epoch is paired with the synchronous
native call and exact final pose bytes, never a later independent slot read.

## Controls and recovery

The automatic package installs **disabled**. KernelSU enablement is the sole
activation preference. Enable only when requested, then use the documented
KernelSU soft reboot workflow. There are no live-checkbox extension hooks.
The worker waits for Steam Link's VR library and Velocity Frame. Controller
sleep, missing samples and unsupported per-packet timing pass through; they
do not impose an experiment-duration limit or turn off the module.

Action reports English live state, packet counters and recovery steps;
it does not enable the module. Disabling/removing makes the worker restore
its three app-private GOT pointers. If restoration fails, leave disabled and
restart Steam Link. Anonymous code remains until app exit for safe in-flight
returns. A full reboot clears RAM; **an enabled module can reapply**, so reboot
alone is not permanent rollback. No APK/vendor original, HAL service, firmware,
partition, calibration store or early boot component is modified.

Exact firmware and native signatures, package/UID, PID/starttime, own-pointer
checks, transaction lock, worker lease and interrupted-activation guard contain
failures. Guard evidence resets on confirmed cold boot without changing either
KernelSU preference. Runtime integrity/dependency errors disable the module and
attempt restoration. No broad periodic memory hash or sample polling loop.

## Build and evidence

Run from the repository root:

```powershell
python tools/check_module_layout.py --module steamlink-pose-time
python tools/check_catalogue.py
python modules/steamlink-pose-time/build.py --ndk /path/to/android-ndk-r28c --runtime /path/to/libopenxr_android.so --scene /path/to/libvrlink_scene.so
```

Build uses Android NDK 28.2.13676358 and verifies the exact local scene SHA.
Only the automatic disabled archive is produced under `dist/steamlink-pose-time`.
The independent stable update feed is `updates/steamlink-pose-time.json`.
Generic installers intentionally abort: reviewed deployment extracts the
disabled package without executing an installer or changing startup settings.

## Validation and limitations

The full-epoch RAM payload was manually tested on Galaxy XR firmware I610UEU2AZF3
with Steam Link 2.0.20 and Velocity Frame. The user approved its behavior.
A 30-second recording contained 10,800 packets with no logger drops. Supported
fresh-slot grid error p95 was ~1.1 microseconds; median PC poseTimeOffset was
~+39.4ms. These are transport measurements, not physical latency calibration.

Rare guarded pass-through packets retain stock timing (29 in the recording),
close to most of the largest API backsteps. This known limitation remains in the
exact tested payload. Native throwing and rendered-frame prediction are unmeasured.
Automatic enabled app-relaunch/soft-reboot behavior has not been device-validated;
the installed-disabled path, status and native ABI were checked. See the
[release validation](../../docs/POSE-TIME-2026-10-13.md).

Version 0.1.2 adds Manager `action.sh off/status` restoration reporting and a
portable build command, without changing the native correction payload.
The Action button itself remains report-only. An unpublished local 0.1.1
installation needs reviewed direct replacement because its old Action lacks
this update contract; do not bypass a Manager restoration refusal.

This fix uses vrlink packets, so it is **not a universal OpenXR timing module**.
No claim of an IMU fix or revival of the failed Bracket direction is made.
Provide your own pinned runtime and Steam Link scene library for a build;
generated vendor signatures and vendor binaries must not be committed.

Native helper selftest is a narrow callback ABI, source-binding, field-write and
disabled-passthrough check. Existing motion evidence is reused; no synthetic
motion suite. Preserve Android NDK/third-party attribution in packaged notices.
