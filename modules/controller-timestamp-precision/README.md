# Controller Timestamp Precision 0.1.1

[Automatic ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-timestamp-precision/v0.1.1/gxr-controller-timestamp-precision-0.1.1-auto.zip)
with a SHA-256 checksum in the same release. The package ships disabled.

Version 0.1.1 uses the standard KernelSU service stage. Press Action to check
the actual process and ownership; release documentation is not a runtime-state report.

Preserves the original integer nanosecond request in the Galaxy XR controller
HAL's software-backend prediction path. The original code converts that large integer
to `float32`, then back to an integer. This unnecessarily rounds the requested
prediction time, with precision worsening as the monotonic clock grows.

This is a separate module from Controller Whitening. It changes a call in the
controller HAL; it does not change covariance whitening, smoothing, prediction
limits, LED selection, camera settings or the IMU stream.

The automatic package is installed **disabled**. Installation does not authorize
activation. KernelSU's checkbox is the sole activation preference. **Action**
reports verified runtime ownership, waiting/errors and recovery instructions.

## Exact change

Supported service and HAL:

```text
/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service
/vendor/lib64/hw/vendor.samsung.hardware.secxrcontroller@1.0-impl.so
SHA-256: e73da96524a11b6414c0bac60df8f76e494dcab4cdbc99b934132501e9c6f076
```

The call at ELF VA `0x22274` is redirected to a 72-byte adapter at `0x2ee00`.
The adapter preserves the caller's original `uint64` request and reuses the
existing method's prologue, joining its unchanged body at `0x2261c`. The public
float-argument entry at `0x225dc` is unchanged, so other callers retain their ABI.

The original conversion between CLOCK_MONOTONIC and CLOCK_BOOTTIME remains in
place, including its microsecond clock-offset sampling and subsequent double
arithmetic. The returned request timestamp becomes exact `uint64`; the callback
still has the ordinary integer precision limits of double. This corrects a
request-time conversion, not the physical controller measurement clock.

Runtime checks require the exact HAL hash, original/full code, a private RX
mapping covering the adapter, zero or exact own adapter bytes, and GOT binding
`base+0x2fdb8 == base+0x225dc`. Unsupported or conflicting code is refused.
The helper briefly stops all service threads for guarded writes and readback.
It installs the adapter before redirecting the call. On off, it restores the
original call and retains the now-unused adapter so in-flight calls can finish.
No vendor file, firmware, partition, calibration or root configuration changes.

## Evidence and limits

The exact candidate passed 7,420 full native ARM64 calls in the offline harness,
including output structures, clock arithmetic, return paths and ABI/memory
guards. An independent 528-execution review compared complete join-state frames
under two load addresses and four rounding modes. The public float API remained
unchanged. Synthetic 90-Hz streams at 48 monotonic hours had 304 repeated targets
out of 900 intervals before this conversion fix and zero afterward.

These are numerical and binary-execution results, not measured visual tracking
improvement. They do not establish that this path is used by every application.
The short adapter has no additional unwind metadata; asynchronous diagnostic
unwinding within it has not been validated. Native throwing and PCVR quality
require separate evaluation. No live activation is implied by a built or
installed package.

The native source, package verification and mocked lifecycle tests are included
here. Proprietary firmware and the original device-specific research recordings
are not distributed in this repository.

## Lifecycle and status

Uses the standard KernelSU 1.0 service stage. The checkbox saves the activation
preference; it does not dispatch an immediate enable command. Use the normal root
soft-reboot flow to reach the service stage after enabling. Action only reports
status; an existing worker observes disable and restores its owned patch.
An enabled worker waits for Android boot completion and performs guarded preflight
once. It does not repeatedly patch a restarting system service.

A persistent lease records module ownership throughout an active session. An
interrupted lease on automatic resume disables and quarantines the module until
explicit retry. The lease is crash evidence, not another enable preference.
Service restart, owner death, checksum failure or a failed health check creates
the KernelSU disable marker and records the reason. The native owner receives
SIGTERM if its supervising worker dies; a separate native guardian handles
owner failure. Disabling verifies owner PID/start/executable before signalling
it. A stopped owned helper receives TERM then CONT; service threads are never
continued or terminated by that lifecycle recovery operation.

Reports use English `ENABLED` and `DISABLED` labels with explicit remaining or
external-patch warnings. A checked box alone does not prove live application.
A separately active timestamp experiment is neither adopted nor stopped by
installation/status; it must be explicitly stopped before handing ownership to
this module. The timestamp helper does not restore or adopt a whitening patch.

```sh
sh /data/adb/modules/gxr_controller_timestamp_precision/action.sh status
sh /data/adb/modules/gxr_controller_timestamp_precision/action.sh off
```

The explicit command below starts the worker immediately and deliberately retries
a resolved error or interrupted lease. Use it only after a separate authorization
to enable this module. A checkbox change or soft reboot does not bypass quarantine:

```sh
sh /data/adb/modules/gxr_controller_timestamp_precision/action.sh on
```

For immediate restoration, run `action.sh off`, then press Action and verify the
result. Turning the checkbox off saves the preference; it is not proof of a
completed restoration.

Runtime logs live in `/dev/gxr_controller_timestamp_precision` on tmpfs; module
files and failure/lease markers live under its `/data/adb/modules` directory.
For an unverified restoration, leave the module disabled and fully reboot.
Reboot removes private code changes, but an enabled automatic module can reapply
after a clean session; disable before rebooting for rollback. Do not restart the
controller service as a recovery procedure.

## Build and disabled deployment

From the repository root, using Android NDK **28.2.13676358**:

```sh
python tools/build.py controller-timestamp-precision --ndk /path/to/android-ndk
python modules/controller-timestamp-precision/tests/verify_package.py
python modules/controller-timestamp-precision/tests/test_lifecycle.py
```

Only `dist/controller-timestamp-precision/gxr-controller-timestamp-precision-0.1.1-auto.zip`
is built. It includes an empty `disable` marker. The standard installer is
deliberately blocked by `customize.sh`: the tested Samsung installation path
can rewrite metadata startup files. Use reviewed direct deployment instead.

Before deployment, validate the exact packaged helper; a different rebuilt binary
cannot inherit another binary's runtime test result. Preserve the prior module
and any independent experiment. For an update, disable the existing module and
verify restoration of its owned patch before replacing its directory. Deploy
reviewed files with the `disable` marker already present, verify package checksums
and executable permissions, and run status only. Do not invoke enable or service
startup as part of installation. Enabling the replacement requires a separate
decision after verification.
