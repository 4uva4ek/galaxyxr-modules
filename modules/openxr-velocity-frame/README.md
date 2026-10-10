# OpenXR controller velocity frame correction

Current controls: [Action report and recovery](../../docs/ACTION-REPORT.md).

Corrects controller linear and angular velocity coordinates in each application's
private OpenXR runtime mapping. It also accounts for the velocity of offset pose
origins (`omega cross offset`). Controller poses and timestamps are unchanged;
this module does not enable additional prediction.

Version **0.2.1** has two packages, sharing ID `gxr_openxr_velocity_frame`:

- **manual**: no startup script. Action reports status; use `action.sh on` to apply.
- **auto**: KernelSU `service.sh` starts a watcher after Android boot completes.
  Action reports current patch status. The watcher discovers eligible apps,
  applies correction when their runtime loads, and verifies it every 30 seconds.

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
correction layers. The separate controller extrapolation patch must also be off:
this release requires stock `consumePose`, since prediction reads the original
input vectors before this correction. These modules are not a tested combination.

## Installation and rollback

Do not invoke `ksud module install` on the tested Samsung setup: its installer
can rewrite metadata startup files. `customize.sh` deliberately aborts. Extract
the selected ZIP on the host and copy its reviewed contents into a **new**
`/data/adb/modules/gxr_openxr_velocity_frame` directory. Do not overlay another
variant or overwrite an existing installation while its worker is running.
Set scripts and `bin/gxr-openxr-velocity` executable, verify `SHA256SUMS`, and run
the absolute path to `service.sh` for the authorized automatic installation.
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
python modules/openxr-velocity-frame/verify_package.py
```

Both ZIPs and checksum files appear in `dist/openxr-velocity-frame/`. Building
never connects to the headset. No APK or Steam Link version is needed for building.

The wrapper hooks the controller consumption call at runtime offset `0x1d9e18`.
Its 1112-byte payload fits a verified unused private RX page tail at `0x272ae0`.
Only controller types 2/3 and six known inputs are changed. Original function bytes,
mapping, firmware hashes and tail contents are checked before writing. All target
threads pause during writes. Rollback restores call reachability and activation;
inert payload bytes remain until app exit so in-flight return addresses remain valid.

The embedded Samsung descriptor geometry matched 26,784 recorded input poses.
Host tests cover 6,000 randomized calculations, validity/finite guards, unchanged
poses and 100 rigid-body origin-shift identities. These checks establish the math
and patch mechanism, not physical tracking accuracy in every app.
