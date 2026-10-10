# Haptic MAIN 0.2.0

A KernelSU module for Galaxy XR SM-I610 that routes standard OpenXR vibration
from SUB near the trigger to MAIN in the grip. The user confirmed operation on
both controllers in Steam Link 2.0.20 without HAL layers on October 10, 2026.

**Action** toggles MAIN ↔ STOCK. There is no automatic startup or background process.
A full reboot or controller service restart restores STOCK; press Action again
to enable MAIN. Disabling the module in the manager does not undo an existing
memory patch. Run `off` or reboot the headset before uninstalling.

```sh
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh status'
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh on'
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh off'
```

Log: `/dev/gxr_haptic_main/log` (tmpfs). If disabling fails, a full reboot restores
the original code. A reboot was not performed during validation; software rollback
through Action was tested.

## How it works

The switcher pauses the threads of `com.sec.android.secxrcontrollerservice`,
validates the function, and replaces one ARM64 instruction in its private mapping
using ptrace. It then detaches immediately. Switching may briefly pause tracking.
The APK and system library on disk remain unchanged.

In `nativePerformHapticFeedback` at offset `0x1238c`:

```text
STOCK: 62 01 00 94    bl 0x12914      ; JNI CallIntMethod: Vibrator.ordinal()
MAIN:  20 00 80 52    mov w0, #1      ; MAIN
```

Amplitude, frequency, duration, PCM, and vibration stopping remain unchanged.
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
and the current instruction value. Unknown firmware or a conflicting modification
causes it to refuse the change. `native/expected.h` contains the reference function
bytes used for this check.

## Installation

The ZIP uses the KernelSU module format. Before using the standard installer,
check its behavior in your KernelSU build. In the tested Samsung build,
`ksud module install` also rewrites `/metadata/.../modules.rc`, so we placed the
files directly in a new `/data/adb/modules/gxr_haptic_main` directory without
running the installer or changing boot files. Other KernelSU and firmware versions
have not been validated.

Build from the repository root: `python tools/build.py haptic-main --ndk PATH`.
See the [validation record](../../docs/VALIDATION-2026-10-10.md) for results and limitations.
