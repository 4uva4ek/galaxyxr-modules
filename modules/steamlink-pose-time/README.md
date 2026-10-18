# Steam Link Pose Time

> **Deprecated:** included in [steamlink-suite](../steamlink-suite/README.md). Update to KernelSU GXR 1.0.11 or later and install the suite. Removed from Sources; historical releases remain available.

Author: **4uva4ek**. Version **0.1.7**. Requires the update-aware KernelSU GXR Manager (1.0.6 or later).

Preserves the epoch of the controller pose actually consumed by Steam Link.
The app's standard signed microsecond field at pose block `+0x54` carries the
relative epoch to vrlink. Position, orientation, velocity, tracking state, HMD
and the shared packet header remain unchanged. The correction is
`(consumed_slot_label - compensation - packet_header_epoch) / 1000` microseconds
after clock conversion, with no learned mean or warmup. The compensation is a
runtime setting, **0–30 ms, default 15 ms**, changed from the module's KernelSU
WebUI. **28.82 ms is the original label delay**: the measured constant lead of the
runtime's slot label over the HAL target whose pose the slot carries (2026-10-12
captures), so 28.82 forwards the true content epoch. **0** forwards the raw slot
label, the behaviour of 0.1.1–0.1.3 that the user found steady but lagging.
Smaller values shorten SteamVR's extrapolation horizon (steadier, later); larger
values lengthen it (fresher, more acceleration jerk).

## WebUI and compensation storage

`webroot/index.html` is opened from the module's WebUI button in KernelSU Manager
(GXR 1.0.6 shows it for an enabled module). It calls only this module's helper
(`bin/gxr-steamlink-pose-time lead get|set <microseconds>`) and `action.sh`.
The helper validates the value (integer 0..30000 µs) and writes
`compensation.conf` atomically in the module directory; nothing else writes it.
The worker re-reads the file every 2 s and, when it changed, stores the value in
the app-private control block under a brief freeze of the target (the same
mechanism as activation, because the write uses `PTRACE_POKETEXT`), so a change
applies within about 2 s while attached, without a reboot, and is used at the
next activation otherwise. A failed live update is non-fatal and retried. The payload clamps an
out-of-range value to the default; the health check rejects it. Saving does not
enable the module. Reviewed deployment preserves `compensation.conf` like
`guard-boot-id`; removing the module removes the setting.

Requires **Velocity Frame** and a supported Steam Link scene profile. The standalone
release is built for **2.0.20**; Steam Link Suite builds include **2.0.20 and 2.0.23**
and accept any package starting with `com.valvesoftware.steamlinkvr`.
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

In that recording 44 of 10 796 packets per hand still carried stock timing; 41 of
them repeated pose bytes that an earlier packet had already sent with an epoch,
i.e. the producer republished an identical pose under a newer slot label and the
0.1.2 payload treated the two labels as ambiguous. Those packets caused four of
the five largest API backsteps (28–42 mm at ~1.5 m/s). 0.1.3 resolves such
matches to the sample with the latest receipt, which is the `GetControllerState`
call that built the packet; packets without any matching sample still pass
through. The 0.1.3 payload has not yet been re-measured live. Native throwing
and rendered-frame prediction are unmeasured.
Automatic enabled app-relaunch/soft-reboot behavior has not been device-validated;
the installed-disabled path, status and native ABI were checked. See the
[release validation](../../docs/POSE-TIME-2026-10-13.md).

Version 0.1.5 turns the fixed 28.82 ms of 0.1.4 into the runtime compensation
above (default 15 ms) and adds the WebUI, `lead` helper command, `compensation.conf`,
the `lead_us` status field and a four-case selftest; matching, guards, lifecycle
scripts and the Manager contract are those of 0.1.3. Only the raw-label wire
offset has been measured live so far.
Version 0.1.4 changed only the wire epoch (content epoch, `epoch=content` in status)
and the native selftest expectation; matching, guards, lifecycle scripts and the
Manager contract are those of 0.1.3. With the raw label the PC saw a median
poseTimeOffset of ~+39.4 ms against ~+15.2 ms stock, so SteamVR extrapolated
~24 ms less than stock: steadier motion, visible lag. The content epoch is
expected near +10.6 ms; whether the horizon-related jerk returns is unmeasured.
Version 0.1.3 changes the payload's handling of identical pose bytes under two
slot labels (latest receipt wins, counter `relabelled`) and makes the worker's
Velocity Frame check tolerant of a partial or changing correction write: unknown
consumer code is a bounded wait (30 s per app process, confirmed by a second
read), not a same-boot failure. Lifecycle scripts and the Manager contract are
those of 0.1.2.
Version 0.1.2 added Manager `action.sh off/status` restoration reporting and a
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
