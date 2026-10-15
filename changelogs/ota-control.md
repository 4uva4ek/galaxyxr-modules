# OTA Control

Author: **AngelDark92**.

## Unreleased

### 0.1.1

- Module author field, README and changelog attribution changed to AngelDark92.
- Removal cleans everything the module owns: after a verified restore,
  `uninstall.sh` deletes the state directory, so no module state survives an
  uninstall. A failed restore keeps the state directory for diagnosis.
- Verified: no telemetry and no network use anywhere in the module; all state is
  local, under `/data/adb/gxr-ota-control/`. No change to the block itself.
- Packaged artifact `gxr-ota-control-0.1.1-auto.zip`, sha256
  `0c4cb00827458cf4098d6d038e27ab5a1d07e084b72efcf1d42b67258d1c4836`; the host
  suite passes (12 scenarios).

## 0.1.0 — 2026-10-13

- First local source. Not published; no release ZIP and no update feed exist.
- Stops Galaxy XR firmware updates by disabling `com.wssyncmldm` and
  `com.sec.android.soagent` for user 0 and setting
  `ota_disable_automatic_update=1`, and restores each of the three to the value
  recorded in a baseline taken immediately before the first block.
- Live control from a root shell: `action.sh on|off|status`, effective at once,
  without a reboot or a KernelSU soft reboot. Action itself only reports.
- The KernelSU checkbox is the boot preference. The boot stage waits for
  `sys.boot_completed`, then re-asserts the blocked state and repairs a package
  that something re-enabled. With the module off, the boot stage restores the
  baseline instead.
- Failure latching, `GXR_UPDATE_STATE=STOCK` reporting under the Manager
  contract, and removal-time restore through `uninstall.sh`.
- Host tests stub the Android commands and cover baseline capture, block,
  restore, boot re-assert, boot restore, failure latching, removal refusal,
  uninstall restore, the detached service entry point and both tool-resolution
  branches. No device validation yet: package names, live package state, the
  setting key and the update screen are unconfirmed on a headset.
- KernelSU specifics: `disable` status flag in the ZIP, `service.sh` detached
  because KernelSU does not wait for the late_start service stage, and `pm`,
  `settings` and `getprop` called by absolute path because KernelSU runs module
  scripts in BusyBox ash standalone mode where PATH order does not decide the
  command. No Magisk-only files are used.
- Corrected the restore contract: KernelSU runs no script of a disabled module,
  so unticking the checkbox does not restore at the next boot. `action.sh off` is
  the restore, and the status output names that command when the preference is
  off while the block is still live. The install/replace flow still restores by
  itself because it calls `action.sh off`.
- Removal now restores unconditionally, so a block that failed after the first
  package no longer leaves that package disabled on the device. The boot stage
  also re-checks the off and remove markers after its boot wait, so a module
  turned off during the wait is not blocked afterwards.
