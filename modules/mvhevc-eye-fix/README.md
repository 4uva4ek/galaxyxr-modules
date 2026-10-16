# Galaxy XR MV-HEVC Eye Fix

KernelSU module for Galaxy XR (SM-I610). Corrects the left/right eye inversion that
Samsung Gallery XR shows for MV-HEVC spatial videos by bind-mounting patched copies of
`libstagefright.so` and `libaplayer.so` over the stock libraries in `/system/lib64`.

Author: **AngelDark92**. Status: **validated on the owner's Galaxy XR (2026-10-15)** — module
enabled, both libraries applied by the KernelSU service stage, Samsung Gallery XR shows
left-primary MV-HEVC spatial video with correct eyes (logcat `onStereoModeInfo mode: 1`,
stereo mode 4). Not published; no update feed.

## What it changes

Three patches in two platform libraries (all in copies; verified with capstone):

| library | offset | change |
| --- | --- | --- |
| `libstagefright.so` | `0x1bbb20` | `cinc w23,w8,ne` -> `cinc w23,w8,eq` — MediaFormat converter primary-eye computation |
| `libaplayer.so` | `0x233e18` | `cinc w23,w8,ne` -> `cinc w23,w8,eq` — libaplayer's own primary-eye computation |
| `libaplayer.so` | `0xf568c` | `ldr w3,[sp,#0x24]` -> `mov w3,#1` — force the value reported to the app |

The computations were inverted: left-primary MV-HEVC files (all Samsung camera
captures, Apple spatial video) produced `primary-eye = 2` (right) instead of `1`
(left). That value reaches Galaxy XR through SemMediaPlayer
(`onInfo(MEDIA_INFO_STEREO_PRIMARY_EYE = 10985, extra)`); with `extra = 2` the app
declares `MULTIVIEW_RIGHT_PRIMARY` (stereo mode 5) and the eyes are crossed. With the
patches the app receives `extra = 1`, keeps `MULTIVIEW_LEFT_PRIMARY` (mode 4) and the
video displays correctly. The third patch forces `extra = 1` for this test build,
independent of the upstream producer.

- Source binaries and patched hashes: `module/NOTICE.txt`. Stock hashes are checked by
  `service.sh`; the module refuses if a device library is not the reviewed build.
- Scope: left-primary MV-HEVC video only (test build forces left).
- Verified scope: the two payloads differ from the stock libraries by exactly the
  three patch sites below and nothing else (byte diff against the headset files), and
  the module writes no state outside its own directory: no telemetry, no network use.

## Package layout (GXR installer contract)

Root files plus one `bin/` payload directory: `module.prop`, `install-info.txt`,
`NOTICE.txt`, `SHA256SUMS`, `customize.sh`, `service.sh`, `action.sh`, `auto.sh`,
`uninstall.sh`, empty `disable` and `skip_mount` markers, `bin/libstagefright.so`,
`bin/libaplayer.so`.
`SHA256SUMS` lists `action.sh`, `auto.sh` and `module.prop` as required by the
manager's installed-lifecycle verification.

## How it is applied

- `service.sh` (KernelSU service stage, every boot while enabled) verifies
  `SHA256SUMS`, prepares both payloads (`chown 0:0`, `chmod 0444`,
  `chcon u:object_r:system_lib_file:s0`) and bind-mounts them over the stock
  libraries. Fail-closed per library on hash mismatch.
- `action.sh status|on|off` (root) applies or removes both mounts immediately —
  restart Gallery XR afterwards. The no-argument run (manager Action) applies if
  needed and reports while the module is enabled; on a disabled or removing module it
  restores the stock libraries instead. `off` stops Gallery XR first, retries a busy
  mount, falls back to a lazy detach, and re-verifies the stock hashes and the mount
  table; it prints `GXR_UPDATE_STATE=STOCK` for the manager's replacement check only
  when the restoration is verified, otherwise it exits 1 and says to reboot.
- `service.sh` starts a detached watcher while the module is enabled. KernelSU runs
  no script when a module is unticked, so the watcher polls the disable/remove
  markers and the module directory itself: on untick, removal, or a deleted
  directory it stops Gallery XR, unbinds both libraries with the same
  retry/lazy-detach/verify logic, logs the result and exits. Disabling therefore
  reverts within seconds, without a reboot.
- `uninstall.sh` (KernelSU removal entry) runs in post-fs-data, before Android
  services exist: the bind mounts do not survive a boot, so it has nothing to
  unmount and needs no Android service. Live removal is covered by the watcher; a
  reboot always restores the stock libraries.
- `auto.sh status|on|off` is the lifecycle entry required by the manager; it
  delegates to `action.sh`.
- No boot image, partition or system file is modified. Nothing stays mounted while
  the module is disabled — the watcher reverts anything left live. No telemetry, no
  network use; the only state written is the module's own `last-run.log`.

## Install

Manager (GXR installer ≥ gxr-v1.0.6): install `gxr-mvhevc-eye-fix-0.3.3-auto.zip` from
`/sdcard/Download/`; it arrives disabled. Enable only after a clean install, then
reboot (service stage).

Direct deployment (root shell) if the Manager refuses the package:

```sh
mkdir -p /data/adb/modules/gxr_mvhevc_eye_fix
cd /data/adb/modules/gxr_mvhevc_eye_fix
unzip -o /sdcard/Download/gxr-mvhevc-eye-fix-0.3.3-auto.zip
chmod 755 action.sh service.sh customize.sh auto.sh
rm -f disable
reboot        # or: sh action.sh on   (applies now, no reboot)
```

## Test

Play an MV-HEVC video in Samsung Gallery (immersive view). Expected: eyes not crossed;
logcat `onStereoModeInfo mode: 1` (previously `2`) and surface stereo mode 4
(previously 5). Quick A/B: `sh action.sh off` + restart Gallery -> crossed again;
`sh action.sh on` + restart Gallery -> correct.

## Rollback

`sh action.sh off` (live; verified against the stock hashes), untick the module (the
watcher reverts within seconds), or delete it. A reboot clears everything in any
case. Abort at boot: KernelSU safe mode (hold Volume-Down), then remove the module.
No Knox state, no partition writes.

## Build / host checks

The builder includes the patched payloads when `module/bin/` holds them (they are
not published); without them it produces the metadata-only package that the
repository's contract check builds in CI.

```sh
python modules/mvhevc-eye-fix/build.py
python modules/mvhevc-eye-fix/tests/verify_package.py   # replays the GXR installer's validation
python tools/check_module_layout.py --module mvhevc-eye-fix
python tools/check_catalogue.py
```
