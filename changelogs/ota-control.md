# OTA Control

Author: **4uva4ek**.

## Unreleased

No pending public changes.

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
