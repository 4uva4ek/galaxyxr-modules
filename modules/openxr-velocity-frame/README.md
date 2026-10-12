# OpenXR controller velocity frame correction

Current release **0.3.5** includes a stable `updateJson` feed for Manager updates. See [installation from Sources or ZIP](../../docs/INSTALLING-MODULES.md). The package remains automatic-only and installs **DISABLED**.

Includes the 0.3.4 app-exit/PID-identity lifecycle fixes. Correction math and compatibility remain unchanged; enabled app-exit/relaunch validation on the headset is still pending.


Current controls: [Action report and recovery](../../docs/ACTION-REPORT.md).

Corrects controller linear and angular velocity coordinates in each application's
private OpenXR runtime mapping. It also accounts for the velocity of offset pose
origins (`omega cross offset`). Controller poses and timestamps are unchanged;
this module does not enable additional prediction.

Version **0.3.4** introduced the lifecycle changes now included in **0.3.5**.
App exit, crash and PID reuse now leave the worker waiting for new applications.
This does not restart applications or change the correction math. See
[lifecycle validation](../../docs/VELOCITY-LIFECYCLE-2026-10-12.md).
The last published release is
[0.3.3](https://github.com/4uva4ek/galaxyxr-modules/releases/download/openxr-velocity-frame/v0.3.5/gxr-openxr-velocity-frame-0.3.5-auto.zip).
During an announced Android restart, temporary Package Manager unavailability
is a bounded waiting state. Native discovery uses a distinct result for this
condition and rechecks process identity before writing. Cold-boot guards expire
without changing the checkbox. The worker survived the recent soft-restart tests
and waited for an OpenXR target; those cycles did not test an app patch. See
[release evidence](../../docs/RELEASE-2026-10-11.md).
The KernelSU checkbox remains the sole saved activation preference. Startup uses
the next standard service stage; checking an idle module does not launch it
immediately. On GXR 1.0, follow its documented soft reboot after root. The worker discovers eligible
apps and checks the patch every 30 seconds. Enable only after separate authorization.

## Scope and compatibility

There is **no Steam Link-only allowlist**. Eligibility is established from Android
Package Manager's third-party package list, exact main-process package name and
matching UID, plus the private executable mapping of the supported runtime.
System processes, system packages and secondary/isolated app processes are excluded.
The native helper independently repeats these checks; the watcher is not the only guard.

Firmware libraries must match exactly:

| Library | SHA-256 |
| --- | --- |
| `/system/lib64/libopenxr_android.so` | `a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92` |
| `/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so` | `80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36` |

Validated firmware: I610UEU2AZF3. The user confirmed smoother motion without jerks
in Steam Link 2.0.20 with the runtime correction. Other applications, including
Chronolink, are eligible if the above checks pass, but their behavior has **not**
been validated. An app's own velocity compensation may conflict with standard
runtime output. Throws have not been separately confirmed.

Disable the old `gxr_controller_velocity_frame` module and restore its current
app-memory patch first. The helper rejects its known live hooks and old APK
correction layers. Version 0.3.3 supports Controller Extrapolation **0.4.2**
(the same native contract as 0.3.0/0.4.0):
correction now runs before prediction, with world-space linear velocity and
input-local angular velocity for the stock predictor. Returned angular velocity
remains world-space. The shared producer buffer is never changed.

The old 0.2.1 payload cannot be upgraded inside a live app. Disable both app
modules and fully restart the app before updating. Unknown hooks are rejected.

## Installation and rollback

Older/standard `ksud module install` must not be used on the tested Samsung setup: that installer
can rewrite metadata startup files. `customize.sh` deliberately aborts. Extract
the selected ZIP on the host and copy its reviewed contents into a **new**
`/data/adb/modules/gxr_openxr_velocity_frame` directory. Do not overlay another
variant or overwrite an existing installation while its worker is running.
Set scripts and `bin/gxr-openxr-velocity` executable, verify `SHA256SUMS`, and run
status only. Leave the `disable` marker present; enabling requires a separate request.
No root acquisition, metadata, boot image, partition or original runtime file is changed.

```sh
su -c 'sh /data/adb/modules/gxr_openxr_velocity_frame/action.sh status'
su -c 'sh /data/adb/modules/gxr_openxr_velocity_frame/action.sh off'
su -c 'sh /data/adb/modules/gxr_openxr_velocity_frame/action.sh on'
```

The last command explicitly enables and starts the worker after reviewed recovery.
`off` disables automatic
activation, stops its watcher and restores current app hooks. Unchecking the module
also causes the running watcher to attempt restoration within its polling cycle.
If restoration reports failure, closing the affected apps clears their private
memory patches. A reboot clears memory but can reapply the auto variant unless
it is disabled first. A full reboot has not been used to validate this release.

An interrupted write transaction leaves a persistent pending marker that prevents
automatic retry. Successful native readback clears that marker immediately;
application survival for 30 seconds is no longer required. Exits, crashes, zombies
and PID reuse discard the old process record and leave the worker enabled.
The worker never launches or restarts an application. A live process losing
eligibility, unknown identity, incompatible bytes, failed writes or health
verification still disable the module and require investigation.
The watcher depends on KernelSU dispatching its service stage after root is available.
Version 0.3.2 handles the standard KernelSU pre-restart notification without
changing the checkbox. The existing worker waits for Android; disappeared app
processes from that announced restart can be replaced. Version 0.3.4 also accepts
ordinary app exits outside a restart. A still-living patched process that loses eligibility is not
silently forgotten. PackageManager discovery uses a three-second query deadline
and a separate pre-write WAITING result; at most 30 unsuccessful discovery/operation
cycles are allowed before disabling. Failed writes and compatibility errors are
never retried automatically. Native correction math and compatibility are unchanged.
See [restart validation](../../docs/MODULE-RESTART-2026-10-11.md).

Native result 4 means the exact PID/start identity ended before any writes, including
read/attach races. Shell actions pass the discovered start identity into the native
helper; a recycled PID can never silently become the mutation target. Result 4 does
not consume the PackageManager failure budget. Process access errors with a still-live
or unreadable identity remain errors. No error after the write boundary is converted
into an ordinary exit. The 30-second read-only health checks remain enabled.

Runtime state/logs are in `/dev/gxr_openxr_velocity_frame`; only module files,
the disable preference and activation-pending marker persist in `/data`.

## Build and verification

Provide your own copy of the pinned runtime; proprietary libraries are not distributed.
Python 3.10+ and Android NDK 28.2.13676358 are required.

```sh
python modules/openxr-velocity-frame/build_module.py --ndk /path/to/ndk --runtime /path/to/libopenxr_android.so
# Windows host test of the actual C++ calculation:
python modules/openxr-velocity-frame/verify.py --ndk /path/to/ndk
python modules/openxr-velocity-frame/test_compatibility.py --ndk /path/to/ndk
# Requires unicorn==2.1.4:
python modules/openxr-velocity-frame/test_pre_prediction.py --runtime /path/to/libopenxr_android.so
python modules/openxr-velocity-frame/verify_package.py
```

The automatic ZIP and checksum file appear in `dist/openxr-velocity-frame/`. Building
never connects to the headset. No APK or Steam Link version is needed for building.

Four instructions inside `consumePose` select preprediction correction, corrected
linear input, a private stack copy of local angular input, and a common exit hook
for interpolation. A per-call register marker prevents double correction and is
restored to the original boolean. The 1296-byte payload fits the verified 1312-byte
RX tail. Both validators accept only exact stock or complete correction bytes and
the exact stock/NOP extrapolation gate; unknown or partial changes are rejected.

Both module Actions share an operation lock. All threads stop before mutation;
the correction additionally refuses changes with live code/return addresses into
`consumePose` or the payload. Disabling restores only the owning module's sites.
The inert payload remains until app exit. A conservative in-flight refusal means
retry after tracking is idle; it does not authorize weakening the guard.

Verification: 1,728 actual ARM64 runtime prediction cases in Unicorn, 10,096
corrupted-byte rejections, all four code combinations, independent restore and
6,000 host math cases. On-device manual on/off in Steam Link PID 8494 confirmed
both activation orders and independent restoration; both patches ended STOCK.
That initial manual test did not exercise an automatic worker, HAL activation,
reboot or subjective combined motion. The user subsequently confirmed good
operation with the combined correction and extrapolation, as recorded for
Controller Extrapolation 0.4.0. This was subjective confirmation; native throwing
and full-reboot behavior remain unvalidated.

The embedded Samsung descriptor geometry matched 26,784 recorded input poses.
Host tests cover 6,000 randomized calculations, validity/finite guards, unchanged
poses and 100 rigid-body origin-shift identities. These checks establish the math
and patch mechanism, not physical tracking accuracy in every app.
