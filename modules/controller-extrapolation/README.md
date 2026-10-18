# Controller extrapolation 0.4.3

> **Deprecated:** included in [steamlink-suite](../steamlink-suite/README.md). Update to KernelSU GXR 1.0.11 or later and install the suite. Removed from Sources; historical releases remain available.

Current release **0.4.3** includes a stable `updateJson` feed for Manager updates. See [installation from Sources or ZIP](../../docs/INSTALLING-MODULES.md). The package remains automatic-only and installs **DISABLED**.

Native patch behavior and runtime scripts are unchanged from the previous public release. This is a distribution/metadata update.


Version 0.4.2 adds cold-boot guard expiry and shared lifecycle reporting without
changing the native extrapolation patch. ON and OFF preferences are preserved.
This module remained OFF during the recent coordinated soft-restart tests.
See [current release evidence](../../docs/RELEASE-2026-10-11.md).

The previous native implementation was user-confirmed on 2026-10-11;
0.4.2 changes lifecycle handling only.

Current controls: [Action report and recovery](../../docs/ACTION-REPORT.md).

Version **0.4.2** is compatible with OpenXR Velocity Frame **0.3.3/0.3.4** (the same native
contract as 0.4.0/0.3.0). Automatic package only, shipped disabled.
Build Velocity Frame first to regenerate the exact shared native compatibility
contract. Unknown/partial hooks are rejected; each module restores only its sites.

The correction supplies the predictor with world linear and input-local angular
velocity while returning world angular velocity to OpenXR. Combined motion
quality, overshoot and throwing still need validation.

Enables the existing OpenXR runtime's controller extrapolation inside the running
`com.valvesoftware.steamlinkvr` process. KernelSU Action reports state and recovery instructions without toggling. This
does not add a HAL client, change the tracking algorithm or force a 60 ms horizon.
The runtime extrapolates to the timestamp requested by the application.

Automatic activation requires a separate user request. The KernelSU checkbox is
the sole saved preference; activation follows the next standard service stage,
not the checkbox click. On GXR 1.0, follow its documented soft reboot after root.
If the worker cannot stop, `off` still attempts guarded native
restoration and reports the worker timeout separately. A shared operation lock
serializes this module with Velocity Frame.

There are no mounts, properties, or policy changes. The original runtime file is
never written. Closing the app process or rebooting clears the current patch,
but auto can apply it again. Disable auto before rebooting for rollback. Opening
the menu alone may not end the process. A running worker observes the disabled
checkbox and attempts restoration; `action.sh off` provides an explicit stop and
restore command. Verify the runtime result; if
restoration fails, leave disabled and terminate the app.

```sh
su -c 'sh /data/adb/modules/gxr_controller_extrapolation/action.sh status'
su -c 'sh /data/adb/modules/gxr_controller_extrapolation/action.sh on'
su -c 'sh /data/adb/modules/gxr_controller_extrapolation/action.sh off'
```

`STOCK` means the original instruction is present, not that extrapolation is
necessarily disabled: a separate APK hook or system flag can also enable it.
`EXTRAPOLATION` confirms the memory patch, not measurement of improved tracking.
Logs and the operation lock are under `/dev/gxr_controller_extrapolation` (tmpfs).

## Exact compatibility and mechanism

Runtime: `/system/lib64/libopenxr_android.so`, SHA-256
`a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92`.
Validated firmware: `I610UEU2AZF3`, Steam Link `2.0.20/5001712`.

The runtime reads `enable_controller_pose_extrapolation_consumer_side` in
`XRInputBufferConsumer` construction and stores it at Accessor offset `0x10`.
In `Accessor::consumePose` (`0x1b359c..0x1b3a58`), the branch at `0x1b3860`
skips extrapolation when this flag is false:

```text
STOCK:         8a 03 00 34    cbz w10, 0x1b38d0
EXTRAPOLATION: 1f 20 03 d5    nop
```

Only that flag check is bypassed. The timestamp equality check, device/input
type checks, sample selection and call to `math_pose_extrapolate_and_assign`
remain intact. This works for existing consumers without changing their heap
objects. The function fingerprint is checked before and after each change.
Unknown code or mappings are rejected. Threads pause briefly during the write.

The switcher is derived from the repository's haptic-main memory switcher.
The runtime hash is checked by Action; the binary additionally checks process
identity, file size, complete function bytes and the private executable mapping.
The native binary is an implementation detail; use Action for the complete guards.

## Build and deployment

Provide your own pinned `libopenxr_android.so` matching the hash above. Prepare
OpenXR Velocity Frame first, including when building only extrapolation: it
generates the exact shared compatibility header. That generated vendor-code
fingerprint is deliberately excluded from Git. Do not substitute another firmware
or bypass a failed hash check.

```sh
python modules/openxr-velocity-frame/prepare.py --ndk /path/to/ndk/28.2.13676358 --runtime /path/to/libopenxr_android.so
python tools/build.py controller-extrapolation --ndk /path/to/ndk/28.2.13676358
python modules/controller-extrapolation/tests/verify_package.py
```

The standard ZIP installer deliberately aborts. The Samsung KernelSU installer
previously rewrote metadata startup files. Deploy reviewed files manually into
a new module directory, without invoking `ksud module install`.
Install or update disabled, restoring an enabled previous module before replacement.
Version 0.4.1 changes packaging and startup/report instructions; its native
extrapolation and compatibility guards remain those of 0.4.0.

## Validation on 2026-10-10

- ARM64 build and package integrity/checksum/startup-exclusion checks passed.
- On the headset, guarded `status -> on -> off -> status` passed in PID 16953.
  The process remained alive, `TracerPid=0`, SELinux Enforcing, original runtime
  SHA-256 unchanged. Full reboot was not performed.
- Installed APK was backed up locally and separately on the headset. Removed
  only the extrapolation library and its implicit-layer JSON, plus old signing
  metadata for resigning. All 124 retained ZIP entries were byte-identical before
  signing; the signed output is checked separately. Certificate matches the input.
- The removed layer also contained an adaptive pose jitter filter. That filter
  is now absent. The separate velocity-frame correction remains in the APK.
- Update installation succeeded. The user confirmed the module works during
  streaming. Actual pose update rate has not been measured separately.
- After the user opened streaming, Action enabled the patch in new PID 19623;
  `TracerPid=0`. No extrapolation or HAL-pose library was mapped; the separate
  velocity-frame library was mapped. Installed APK SHA-256 matched the verified
  signed output: `298d709365481ac4e012ba8227730dd4cbdcc47848728529bcd6d23da570d629`.
  Final state for this first motion test: EXTRAPOLATION enabled manually.

Local evidence: `E:/GalaxyXR/diagnostics/extrapolation-ksu-20261010/`.
Original APK on headset:
`/data/local/tmp/gxr-extrapolation-20261010/original-steamlink.apk`.

## Next tracking investigation

Offline replay on 2026-10-12 validates the existing minimal gate patch using
retained recordings, including negative prediction intervals and repeated API
queries. See [consumePose replay and prepared candidate](../../docs/CONSUMEPOSE-2026-10-12.md).
The native patch is unchanged; no new tracking capture or deployment was needed.

Compare stock instructions and enabled extrapolation using the same APK and PC
driver settings. Record stationary jitter, smooth motion, reversals, tracking
loss and throws separately. Keep PC pose timestamp bias/horizon fixed and record
it; do not import the former HAL 60 ms setting into a system-wide patch.
Then isolate the remaining velocity-frame layer from pose prediction. No camera,
calibration, firmware, HAL backend or persistent property change is included.
