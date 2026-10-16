# Galaxy XR OTA Control

KernelSU module for Galaxy XR (SM-I610). It stops Samsung firmware updates by
disabling `com.wssyncmldm` and `com.sec.android.soagent` for user 0 and setting
`ota_disable_automatic_update` to 1. The same script restores all three to the
values recorded in a baseline captured immediately before the first block.

Author: **AngelDark92**. Status: **not yet validated on a headset.** Local host tests
cover the script logic; the package is not published and has no live update feed.

## Difference from the other GXR modules

Every other module in this repository patches private process memory and leaves
the device untouched on disk. This one writes persistent Android state: the
per-user package enabled flags in `package-restrictions.xml` and the global
setting. Both survive a reboot without the module. That state is the point of
the module, and it is the reason the baseline exists.

- No Knox state change, no partition write, no system file, APK or library edit.
- `ota_disable_automatic_update` is an AOSP-level hint that Google's update
  service reads. On a Samsung build the two package disables are the effective
  block; the setting is kept because it is part of the requested scope.
- Disabling `com.wssyncmldm` stops the Samsung DM/FOTA client, so no firmware or
  security updates and no service that rides that client while the block is live.
- Reversal is exact only where a baseline exists. Without one, restore is skipped
  and the log says so, so nothing that was already disabled gets enabled.

## KernelSU loading

Verified against the KernelSU module guide and the KernelSU-Next structure
notes:

- KernelSU reads `/data/adb/modules/<id>/` and treats every subdirectory that
  contains `module.prop` as a module, so this package needs no Magisk-specific
  files: no `magisk/` directory, no Zygisk, no `$MAGISK_VER`.
- `disable` is a KernelSU status flag: present means the module is skipped.
  The ZIP ships it, so installation never activates anything.
- `skip_mount` is the second empty marker. The GXR installer and
  `tools/update_feed.py` reject a package without it; the module has no
  `system/` overlay, so the marker changes nothing at runtime.
- `auto.sh` maps the verbs the other GXR modules and the release pipeline use
  (`resume`, `enable`, `disable`, `status`) onto `ota-ctl.sh`. It never touches
  the KernelSU markers; `action.sh` owns those.
- `service.sh` runs in the non-blocking late_start service stage. KernelSU is
  not expected to wait for it, so it detaches the boot work with `nohup` and
  returns at once.
- KernelSU runs no script of a disabled module, so the boot stage cannot restore
  an unticked module. Ticking the checkbox arms the re-assert; un-ticking stops
  it and requires `action.sh off` (or module removal, which restores once the
  next boot completes) to restore. The boot stage
  re-checks the off and remove markers after its boot wait, so turning the module
  off while it waits does not block updates afterwards.
- KernelSU executes module scripts in BusyBox ash with standalone mode on. In
  that mode PATH order does not decide which command runs, so `pm`, `settings`
  and `getprop` are called by absolute path (`/system/bin/...`) with a PATH
  fallback when that path is absent.
- Module scripts run as root, so the `id -u` guard passes without `su`.
- `action.sh` is the KernelSU Action button entry, `uninstall.sh` runs on
  removal, and `updateJson` is a KernelSU metadata field. The module id matches
  the KernelSU rule `^[a-zA-Z][a-zA-Z0-9._-]+$`.
- The ZIP installer sources `customize.sh`; this one aborts on purpose, as in
  every other module here, so install through the documented deployment path.

## Controls

The KernelSU checkbox is the saved boot preference; the boot stage re-asserts the
blocked state while it is on. Action reports and never toggles. Live control is a
root shell command, so one press cannot change the update policy:

```sh
su -c 'sh /data/adb/modules/gxr_ota_control/action.sh on'      # block now
su -c 'sh /data/adb/modules/gxr_ota_control/action.sh off'     # restore now
su -c 'sh /data/adb/modules/gxr_ota_control/action.sh status'  # raw diagnostics
```

`on` and `off` act immediately, without a reboot or soft reboot, and they are
also the KernelSU preference: `on` removes the `disable` marker, so the checkbox
turns ON and the boot stage re-asserts the block; `off` creates it. KernelSU does
not run the scripts of a disabled module, so unticking the checkbox in the
Manager only stops future boot re-asserts; it does **not** restore updates by
itself. Run `action.sh off` to restore, or remove the module: its removal
restore finishes once the next boot completes (see Safety). When status finds
the preference off and the block still live, it prints the restore command.

The Manager's install/replace flow restores by itself, because it calls
`action.sh off` before swapping the module directory.

The last status line is the universal label: `ENABLED` while the block is live,
`DISABLED - Waiting` with the preference ON and nothing applied yet,
`DISABLED - KernelSU preference OFF ...`, or `DISABLED - Error:` with the
recorded cause and the retry commands.

State lives in `/data/adb/gxr-ota-control/`:

| File | Meaning |
| --- | --- |
| `baseline` | Enabled state of each package and the setting value before the first block |
| `state` | `blocked` or `allowed`, the last confirmed change made by this module |
| `failed` | Cause of the last failed change; blocks the STOCK report until a change succeeds |
| `log` | Timestamped change and boot history |

`GXR_UPDATE_STATE=STOCK` appears in the status output only when no failure is
recorded and every package and the setting match the baseline, i.e. when the
device is safe for a module directory replacement. A module that was never
activated reports STOCK: nothing of ours is applied.

## Safety

- Packages install **disabled**; `disable` and `skip_mount` are part of the ZIP.
- `customize.sh` refuses the generic installer path, as in the other modules.
- No telemetry and no network use anywhere in the module: only the local Android
  package and settings tools are called, and all state stays under
  `/data/adb/gxr-ota-control/`.
- Removal restores the baseline, but not inside `uninstall.sh` itself: KernelSU
  runs that script in post-fs-data of the next boot, before `pm` and `settings`
  exist. It copies `ota-ctl.sh` to the `/dev` tmpfs and starts one process of
  that boot which waits for `sys.boot_completed`, restores from live state
  against the baseline (so a block that failed after the first package is still
  reverted), then deletes the state directory. On failure the state directory
  stays for manual recovery. Nothing survives the following reboot.
  `action.sh off` before removal remains the reliable path.
- A failed change writes the `disable` marker and stops retrying in the same
  boot; the cause is reported by Action.
- Manual restore, if the module directory is gone:

```sh
su -c 'pm enable --user 0 com.wssyncmldm; pm enable --user 0 com.sec.android.soagent; settings put global ota_disable_automatic_update 0'
```

A package that was already disabled before the first block must stay disabled;
check `pm list packages -d` before running the line above.

## Host tests

```sh
python modules/ota-control/test_ota_ctl.py
```

The test stubs `pm`, `settings`, `getprop` and `id` and runs the real module
scripts: baseline capture with a vendor-disabled package and a pre-set setting,
block, restore, boot re-assert, boot restore, a killed boot stage that must not
block, failure latching, refusal while removal is requested, removal-time
restore, the detached `service.sh` entry point and both tool-resolution
branches. It never touches a device.

## Build

```sh
python modules/ota-control/build.py
```

No NDK is required: the module has no native payload. The result is
`dist/ota-control/gxr-ota-control-0.1.0-auto.zip` plus its `.zip.sha256`.

The module is **not** in `modules.json` and has no `updates/ota-control.json`
feed: those describe reviewed public distribution and need separate publication
authorization.

```sh
python tools/check_module_layout.py --module ota-control
```

## Remaining validation

Once a headset is connected: confirm that both package names exist and read their
current enabled state and `ota_disable_automatic_update` before the first block,
then verify `pm list packages -d` and the setting after `on` and after `off`, and
confirm that the Android Settings update screen no longer offers an update.
Boot re-assert and removal-time restore are untested on a device.
