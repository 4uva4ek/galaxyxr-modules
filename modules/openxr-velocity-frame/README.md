# OpenXR controller velocity frame correction

Current controls: [Action report and recovery](../../docs/ACTION-REPORT.md).

Corrects controller linear and angular velocity coordinates in each application's
private OpenXR runtime mapping. It also accounts for the velocity of offset pose
origins (`omega cross offset`). Controller poses and timestamps are unchanged;
this module does not enable additional prediction.

Version **0.3.0** publishes an automatic package only, installed disabled.
The KernelSU checkbox remains the sole activation preference. It discovers eligible
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
correction layers. Version 0.3.0 supports Controller Extrapolation **0.4.0**:
correction now runs before prediction, with world-space linear velocity and
input-local angular velocity for the stock predictor. Returned angular velocity
remains world-space. The shared producer buffer is never changed.

The old 0.2.1 payload cannot be upgraded inside a live app. Disable both app
modules and fully restart the app before updating. Unknown hooks are rejected.

## Installation and rollback

Do not invoke `ksud module install` on the tested Samsung setup: its installer
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
su -c 'sh /data/adb/modules/gxr_openxr_velocity_frame/auto.sh enable'
```

The last command is for the automatic package only. `off` disables automatic
activation, stops its watcher and restores current app hooks. Unchecking the module
also causes the running watcher to attempt restoration within its polling cycle.
If restoration reports failure, closing the affected apps clears their private
memory patches. A reboot clears memory but can reapply the auto variant unless
it is disabled first. A full reboot has not been used to validate this release.

An interrupted activation leaves a persistent pending marker that prevents automatic
retry. An app exit during the first 30 seconds or a verification error disables the
watcher; explicit re-enablement is required after investigation. This is conservative
and can also trigger when the user intentionally closes an app during that window.
The watcher depends on KernelSU dispatching its service stage after root is available.

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
No automatic worker, HAL activation, reboot or subjective combined test was run.

The embedded Samsung descriptor geometry matched 26,784 recorded input poses.
Host tests cover 6,000 randomized calculations, validity/finite guards, unchanged
poses and 100 rigid-body origin-shift identities. These checks establish the math
and patch mechanism, not physical tracking accuracy in every app.
