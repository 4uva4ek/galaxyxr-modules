# Controller Whitening 0.2.0

Version 0.2.0 adds camera-projection failure rejection to the existing whitening correction. Both changes are applied/restored by one owner. Ten instruction edits and 184 bytes of guard code are identical to the manually tested ORION patch. The user reported a strong improvement on October 17; this is subjective feedback, not proof of fault incidence or physical accuracy.

A separate private RX mapping of an original vendor-library page supplies the guards. Only copy-on-write RAM is modified; the original file is unchanged. The helper retains this page after restoration for in-flight returns and verifies/reuses it on later activation. Unsupported layout, firmware or unknown bytes cause refusal. Failed activation disables the module through its existing lifecycle. Installing the automatic ZIP leaves it disabled. An existing external trial is reported without being adopted or stopped.

Native math/ABI evidence and the live temporary activation are reused; repeated test suites were not run for this release at the user's request. Build/archive integrity checks only. Automatic startup of this new version has not yet been exercised on the headset.

## Previous release history

Current release **0.1.7** includes a stable `updateJson` feed for Manager updates. See [installation from Sources or ZIP](../../docs/INSTALLING-MODULES.md). The package remains automatic-only and installs **DISABLED**.

The whitening correction and runtime scripts are unchanged. Version 0.1.7
recognizes only the exact paired IMU wait-gate modification during full-code
validation, so it can check and restore whitening while that known peer patch
is present. Partial and unknown changes remain rejected. This package does not
install or reactivate the retired IMU Time experiment.


Version 0.1.3 restores the old owner before a planned Android restart and starts
a fresh owner at the next service stage. Pointer diagnostics include the failing
address/value and target identity; calibration guards remain intact. Two
consecutive active-to-active soft restarts passed. Cold-boot guards expire
without changing the checkbox.
See [current release evidence](../../docs/RELEASE-2026-10-11.md).

[Automatic ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-whitening/v0.1.7/gxr-controller-whitening-0.1.7-auto.zip)
with a SHA-256 checksum in the same release. The package ships disabled.

Corrects a covariance whitening operation in the Galaxy XR controller tracking
service. The change applies to private process memory. The original vendor
library, firmware, calibration and partition contents remain unchanged.

The automatic package is installed **disabled**. Installing or updating it does
not authorize activation. KernelSU's checkbox is the sole activation preference;
there is no separate opt-in setting. **Action** reports status and recovery
instructions without toggling the module.

## Evidence and limits

Offline execution of the original ARM64 code confirmed that the process update
used the wrong orientation of a Cholesky inverse. Executing the exact replacement
instructions corrected the covariance law in nine propagation cases. Independent
tests covered the replacement solver's dimensions, memory boundaries and calling
convention.

After a temporary native-headset test on October 11, 2026, the user reported that
native tracking felt correct. This is a subjective result from one headset,
not an objective accuracy measurement. PCVR and native object throwing have not
been validated. The result does not establish that every earlier jump had the
same cause.

## Change and compatibility

Supported target:

```text
/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service
/vendor/lib64/libsctrl.so
SHA-256: 4dfb10d768c5cdf14b50873057c59eeacc83a3532c78d10b21652bb9727fe01f
```

For process covariance `Q = L L^T`, the unchanged solver left-multiplies its
process rows by `W`. This requires `W = L^-1`. The original call computed
`W = L^-T`, which instead represents covariance `L^T L` when correlations exist.

Two instruction changes in the 12-byte window at ELF virtual address
`0x5a95dc..0x5a95e8` select the existing lower-triangular solver and pass it the
direct matrix descriptor. No new code cave, filter, smoothing threshold, LED
selection rule or camera setting is introduced.

The helper checks the exact library hash, target process identity, private code
mapping and expected instructions. It briefly stops all target threads and
requires every program counter to be outside the complete argument/call window
before changing or restoring the instructions. It reads back the result and
releases the threads. Unsupported firmware, conflicting code or active
calibration causes refusal. Switching can briefly interrupt tracking.

## Activation and status

Uses the standard KernelSU 1.0 service stage. The checkbox saves the activation
preference; it does not dispatch an immediate enable command. After enabling,
use the normal root soft-reboot flow to reach the service stage. Action separately
verifies the instructions and ownership. It does not start or stop anything.
An already-running worker observes the disable marker and restores its patch.

The module uses its own service-stage worker and recovery helper. A target
service restart after activation, an interrupted active session or a failed
health check blocks further automatic activation and records the reason. It does
not repeatedly patch a restarting system service. The interrupted-session marker
is a failure guard, not a second activation preference.

Reports use English `ENABLED` and `DISABLED` labels with explicit waiting or error
reasons. Read the verification result: a checkbox alone does not establish that
the patch is live, and a restoration error does not establish that it is gone.
Action explains how to retry and how to recover.

Root-terminal equivalents:

```sh
sh /data/adb/modules/gxr_controller_whitening/action.sh status
sh /data/adb/modules/gxr_controller_whitening/action.sh on
sh /data/adb/modules/gxr_controller_whitening/action.sh off
```

The explicit `on` command starts the worker immediately and is also the deliberate
retry after an interrupted lease or a resolved error. Use it only after a separate
decision to enable this automatic module. A checkbox change or soft reboot does
not bypass quarantine. Runtime
logs and worker state are kept in `/dev/gxr_controller_whitening` on tmpfs;
installed files and failure markers remain in the module directory under `/data`.

## Installation, update and recovery

The package uses the KernelSU module layout, but `customize.sh` deliberately
aborts the standard installer. The tested Samsung installer can also rewrite
startup files under `/metadata`. Use reviewed direct deployment into
`/data/adb/modules/gxr_controller_whitening`, preserving the package's empty
`disable` marker. No early-boot hook or metadata startup file is needed.

Before replacing an existing module, disable it and verify restoration of the
patch owned by that module. Preserve the previous package, deploy the replacement
disabled, and verify its contents and status. Do not enable it as part of
installation or update.

A separately running whitening experiment can remain active while this module
is installed disabled. The module must report `DISABLED` and explicitly identify
that external activity; it must neither adopt nor stop the experiment as part of
installation or a status check. Enabling later requires an explicit handoff:
restore the standalone experiment first, verify stock, then enable the module.

To recover a module-owned activation immediately, run `action.sh off`, then press
Action and confirm restoration. Turning the KernelSU checkbox off saves the same
disabled preference; an existing worker observes it, but verify the runtime
result instead of treating the checkbox as proof.
If restoration cannot be verified, leave the module disabled and fully reboot
the headset. A reboot clears the private memory patch and estimator state, but
an enabled automatic module can reapply its change. Disable it before rebooting
for rollback. If the Manager is unavailable, create the module's `disable` marker
from an available root terminal before rebooting.

Restoring the instructions does not rewind the filter's accumulated state.
A service restart or full reboot creates a fresh filter state. An abrupt helper
termination during the two-word transaction can transiently disrupt the service;
the recovery helper is not a guarantee of crash-free execution. Original files
and the disabled module preserve the stock reboot path.

## Build

From the repository root, using Android NDK **29.0.14206865**:

```sh
python tools/build.py controller-whitening --ndk /path/to/android-ndk
python modules/controller-whitening/tests/verify_package.py
```

Output: `dist/controller-whitening/gxr-controller-whitening-0.2.0-auto.zip` and
its SHA-256 file. Only the automatic variant is built or distributed. The ZIP
contains `disable`; building and verifying it never connects to the headset.
Samsung binaries and device calibration are not included.

### 0.2.1 soft-reboot recovery

Status and restoration can verify the original code while Android reports boot incomplete. Activation and calibration preflight still require boot completion. All model, PID/start, library hash and full-RX guards remain. Build/package checks only; no extra reboot or runtime activation test.
