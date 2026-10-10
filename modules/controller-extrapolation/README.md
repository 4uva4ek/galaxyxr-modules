# Controller extrapolation 0.1.0 (manual)

Enables the existing OpenXR runtime's controller extrapolation inside the running
`com.valvesoftware.steamlinkvr` process. KernelSU Action toggles the change. This
does not add a HAL client, change the tracking algorithm or force a 60 ms horizon.
The runtime extrapolates to the timestamp requested by the application.

There are no startup scripts, mounts, properties, policy changes or background
workers. The original runtime file is never written. Closing the app **process**
or rebooting restores stock. Opening the menu alone may not end the process.
Disabling the module in the manager does not undo an existing memory patch:
use `off` or terminate the app process before uninstalling.

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

```sh
python tools/build.py controller-extrapolation --ndk /path/to/ndk/28.2.13676358
python modules/controller-extrapolation/tests/verify_package.py
```

The standard ZIP installer deliberately aborts. The Samsung KernelSU installer
previously rewrote metadata startup files. Deploy reviewed files manually into
a new module directory, without invoking `ksud module install`.

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

Compare stock instructions and enabled extrapolation using the same APK and PC
driver settings. Record stationary jitter, smooth motion, reversals, tracking
loss and throws separately. Keep PC pose timestamp bias/horizon fixed and record
it; do not import the former HAL 60 ms setting into a system-wide patch.
Then isolate the remaining velocity-frame layer from pose prediction. No camera,
calibration, firmware, HAL backend or persistent property change is included.
