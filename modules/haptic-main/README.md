# Haptic MAIN 0.5.6

Current release **0.5.6** includes a stable `updateJson` feed for Manager updates. See [installation from Sources or ZIP](../../docs/INSTALLING-MODULES.md). The package remains automatic-only and installs **DISABLED**.

Native patch behavior and runtime scripts are unchanged from the previous public release. This is a distribution/metadata update.


After `sys.boot_completed=1`, activation waits up to 20 seconds for the
controller service and JNI mapping, applying immediately when ready. Announced
soft restarts allow a verified worker handoff; unexpected failures still stop
retries. Cold-boot interruption guards expire without changing the checkbox.
See [behavior and validation](../../docs/COLD-BOOT-GUARDS-2026-10-11.md).

Current controls: [Action report and recovery](../../docs/ACTION-REPORT.md).

**Regular release:** [Automatic ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/haptic-main/v0.5.6/gxr-haptic-main-0.5.6-auto.zip).
Only the automatic variant is published, shipped disabled. SHA-256 is attached to the release.

A KernelSU module for Galaxy XR SM-I610 that routes standard OpenXR vibration
from SUB near the trigger to MAIN in the grip. The user confirmed operation on
both controllers in Steam Link 2.0.20 without HAL layers on October 10, 2026.

**Action** reports state and recovery instructions without toggling. Automatic
activation uses the saved KernelSU checkbox preference at the next service stage
and requires a separate request. Checking an idle module does not launch it
immediately. In GXR 1.0, follow its documented soft reboot after root. See
[activation and recovery](../../docs/AUTOSTART.md).

A full reboot or controller service restart clears the current memory patch.
With auto enabled, a later KernelSU startup can apply it again: disable auto before
rebooting for rollback. A running worker observes the disabled checkbox and
attempts restoration within its polling cycle; verify the Action report.
Explicit `action.sh off` stops the worker and restores code. If restoration fails,
leave disabled and reboot.

```sh
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh status'
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh on'
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh off'
```

Log: `/dev/gxr_haptic_main/log` (tmpfs). With auto disabled, a full reboot restores
the original code. A reboot was not performed during validation; software rollback
through Action was tested.

## How it works

The switcher pauses the threads of `com.sec.android.secxrcontrollerservice`,
validates the function, and replaces the routing instruction and the diagnostic log block in its private mapping
using ptrace. It then detaches immediately. Switching may briefly pause tracking.
The APK and system library on disk remain unchanged.

In `nativePerformHapticFeedback` at offset `0x1238c`:

```text
STOCK: 62 01 00 94    bl 0x12914      ; JNI CallIntMethod: Vibrator.ordinal()
MAIN:  20 00 80 52    mov w0, #1      ; MAIN
```

Positive non-PCM pulses use frequency step 2 (the stock mapping for a 100 Hz request)
and preserve the incoming amplitude exactly (the stock service already limits ordinary requests). This is not a measured physical motor frequency.
Duration, PCM, zero/negative/NaN amplitude and vibration stopping remain unchanged.
The diagnostic log block at 0x12870..0x128b4 is replaced by bounded inline arithmetic.
No new allocation, calls, buffer, thread, or code cave is introduced.
Threads stopped within that block cause refusal before writes. All changed words
are read back; a failed transaction attempts verified restoration of every word.
The stock service continues to use its own HAL. The module has no separate HAL
client, Frida, or Shizuku dependency. It does not affect direct HAL calls from
other applications; other callers of the same JNI function are also redirected.

## Compatibility

Only this exact pair of SHA-256 hashes is supported:

| Original file | SHA-256 |
| --- | --- |
| `/product/priv-app/secxrcontroller/secxrcontroller.apk` | `8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb` |
| `/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so` | `80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36` |

The switcher also checks the process, private mapping, 1,640 bytes of the function,
and the current patch state. Legacy MAIN is recognized as an upgrade source. Unknown firmware or a conflicting modification
causes it to refuse the change. `native/expected.h` contains the reference function
bytes used for this check.

## Installation

The ZIP uses the KernelSU module format but its standard installer aborts.
Deploy reviewed files manually into a new module directory. In the tested Samsung build,
`ksud module install` also rewrites `/metadata/.../modules.rc`, so we placed the
files directly in a new `/data/adb/modules/gxr_haptic_main` directory without
running the installer or changing boot files. Other KernelSU and firmware versions
have not been validated.

Build from the repository root: `python tools/build.py haptic-main --ndk PATH`.
See the [validation record](../../docs/VALIDATION-2026-10-10.md) for results and limitations.

## Tuning validation

Before building, run `python modules/haptic-main/tests/build_tuning_test.py --ndk PATH`
to assemble tuning.S and check the committed tuning.h. The generated standalone
`dist/haptic-main/test-tuning` runs 3603 actual ARM64 cases plus NaN and signed zero.
It never opens another process or invokes controller services.

Install the automatic package disabled after a manual on/off check; do not
activate it automatically as part of deployment. Preserve the previous module.

Version 0.5.1 removes the gain and restores the `MAIN pid=` status prefix required by the 30-second worker health check. Version 0.5.0 incorrectly placed tuning details between MAIN and pid, causing a false failure and safe restoration.

Version 0.5.5 includes the lifecycle work introduced in 0.5.3, cold-boot
guard reset from 0.5.4, and service-readiness waiting. Haptics was verified active
after the coordinated soft-restart tests; see the
[current release evidence](../../docs/RELEASE-2026-10-11.md).
The standard `emulated-soft-reboot.sh` stage only notifies an existing worker.
The worker waits for Android, then permits replacement of the old process during
that announced restart. Surviving processes retain their patch and health checks.
Unannounced service exits and interrupted activations still block automatic retry.
An old pending marker is not silently cleared; replacement must be installed
disabled and recovery/activation separately requested. Native routing and tuning
are unchanged. See [restart validation](../../docs/MODULE-RESTART-2026-10-11.md).
