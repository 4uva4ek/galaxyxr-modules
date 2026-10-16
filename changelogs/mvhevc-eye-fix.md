# mvhevc-eye-fix — changelog

## Unreleased

### 0.3.6

- The module now disables itself on a full shutdown/reboot, so the next boot
  honestly shows it OFF until it is enabled and activated again; a soft restart
  keeps the checkbox exactly as the user set it.
  - The watcher now also watches the power state: when a real shutdown/reboot
    begins (`sys.powerctl` or `sys.shutdown.requested` set while
    `sys.boot_completed` is still 1 — the Manager power menu, button shutdowns and
    adb reboots all set this) it writes the `disable` marker, lazily detaches both
    mounts and exits; a HUP/INT/TERM trap runs the same check.
  - KernelSU's emulated soft reboot (the manager's soft restart) resets
    `sys.boot_completed` to 0 first, sets neither power property and sweeps module
    processes itself, so the check never fires for it; the relaunched service
    stage re-applies the patch and re-arms the watcher.
  - Rationale: with temporary root a cold boot leaves the patch off until a soft
    restart, so a ticked box after a cold boot was misleading.
  - Fails closed like the repository's autostart workers (Haptics MAIN): an
    unexpected non-zero watcher exit also writes the marker and detaches the
    mounts, so a dead watcher is never silently paired with a ticked checkbox.
- Packaged artifact `gxr-mvhevc-eye-fix-0.3.6-auto.zip`; the published asset hash
  is advanced by the pipeline in `updates/mvhevc-eye-fix.json`.

### 0.3.5

- Cleanup fixes for the disable and delete paths:
  - Every apply path arms the revert watcher: `service.sh` (boot and soft restart)
    and both applying paths of `action.sh` (manager Action, `on`) start it through
    `watch-start.sh`, a single launcher that keeps one instance, and they arm it
    even after a partial apply. Previously only the service stage started it, so a
    patch applied through the Action button without a boot or soft restart stayed
    mounted after an untick or a delete until a reboot.
  - Bound detection is pinned: `check`/`apply` compare the live library against the
    pinned patched hash instead of the local payload copy, and every bind is
    re-hashed after mounting (a mismatch unmounts and refuses). A payload whose
    bytes are not the reviewed patched build can no longer be reported as applied
    while the device serves stock.
  - `make_payload` generates into a unique `*.gen.<pid>` temporary, sweeps stale
    temporaries and treats a failed rename as a failure (previously unchecked), so
    concurrent generations cannot corrupt a verified copy; only a verified result
    is renamed into place.
  - The watcher retries a busy unmount up to 10 times, logs an explicit failure line
    instead of claiming restoration when it could not verify it, and removes its
    `watcher.pid` on exit.
- Documentation corrected to match: install-info test/restore steps, the state-file
  list (`last-run.log`, `watcher.pid`, generated copies under `bin/`), the
  reboot-as-restore wording in `README.md` and `module.prop`, and the 0.3.4 artifact
  line below (the published CI-built asset, not the author-machine build).
- Documented where a re-apply actually happens after a reboot: on KernelSU
  temporary-root setups (the Galaxy XR flow) the service stage runs only after root
  acquisition and the manager's soft restart, so a plain cold boot leaves the patch
  off even with the module ticked; the README, install-info and module.prop wording
  now says so instead of implying every reboot re-applies.
- Packaged artifact `gxr-mvhevc-eye-fix-0.3.5-auto.zip`; local and CI ZIP bytes
  differ, so the published asset hash is recorded by the pipeline in
  `updates/mvhevc-eye-fix.json`.

### 0.3.4

- The patched libraries are generated on the device from the stock libraries at
  first apply instead of shipping as packaged payloads. `service.sh` and `action.sh`
  copy `/system/lib64/libstagefright.so` and `libaplayer.so`, apply the three
  documented byte patches, verify the pinned patched hashes from `module/NOTICE.txt`
  and only then bind-mount. No vendor binaries ship in the package or the repository;
  the reviewed-stock hash check still precedes every apply, and a tampered generated
  copy is regenerated and re-verified.
- `build.py` now refuses to package `bin/` payloads (fail closed).
- `tests/test_payload_patch.py` proves the generation offline against a synthetic
  fixture and pins `make_payload()` in both scripts; `tests/verify_package.py`
  rejects any packaged `bin/` payload.
- Published artifact `gxr-mvhevc-eye-fix-0.3.4-auto.zip` (built by the release pipeline; the author-machine build hashes differently), sha256 b11e193455eafeb36992f45af9e7e254dacc0482c09a405d5b5ba06e17dd8bcf, recorded in `updates/mvhevc-eye-fix.json`.
- Re-checked on the owner's Galaxy XR (2026-10-16): the patched libraries were
  generated on the device from the stock libraries, applied by the KernelSU service
  stage, and Samsung Gallery XR shows left-primary MV-HEVC video with correct eyes
  (owner confirmation of the physical effect).

### 0.3.3

- Unticking the module now reverts by itself. KernelSU runs no script when a module
  is unticked (device logs: the manager writes the marker only), so 0.3.2 could not
  restore on untick — observed on the headset 2026-10-15, mounts still live after the
  toggle. `service.sh` now starts a detached watcher (loop in argv, so it survives the
  module directory being deleted) that polls every few seconds for the disable/remove
  markers or a missing module directory, then stops Gallery XR, unbinds both libraries
  with the same retry/lazy-detach/verify logic, logs the result and exits.
- The Action button on a disabled or removing module now restores the stock libraries
  instead of applying the patch; on an enabled module it still applies if needed and
  reports.
- Reports use the repository's universal labels (`docs/ACTION-REPORT.md`): `ENABLED`
  while the patch is applied, `DISABLED` with the reason otherwise, never the
  substring `ACTIVE` (the GXR manager vetoes it).
- `uninstall.sh` rewritten to the removal contract: KernelSU runs it in post-fs-data,
  before Android services exist, and the bind mounts do not survive a boot — the
  script has nothing to unmount and calls no Android service; the watcher covers a
  removal while the headset keeps running.
- Restore paths, in any case: watcher (seconds after untick or removal), `action.sh
  off` (verified), `uninstall.sh` (removal), reboot (always).
- `build.py` no longer requires the binary payloads: when they are absent (a fresh
  clone, CI) it builds the metadata-only package with an explicit note, which is
  what the repository contract check validates; with the payloads present the
  package is unchanged. The module refuses to apply without its payloads.
- Packaged artifact `gxr-mvhevc-eye-fix-0.3.3-auto.zip`, sha256
  `c24c361cfdb00f364396c80411ba0ebfbbefa834924bba9eb088033e8bfaaa4a`; the
  `tests/verify_package.py` GXR-installer replay passes.

### 0.3.2

- Module author field, README author line and this changelog attribution changed to
  AngelDark92.
- Cleanup guarantees: added `uninstall.sh` (KernelSU runs it on removal of an enabled
  module — the observed path; with the module disabled, run `action.sh off` first, or
  reboot) and a service-stage branch that unbinds when that stage is ever dispatched
  for a disabled or removing module. `action.sh off` now
  stops Gallery XR before the unbind so it releases the mappings, retries a busy
  mount, falls back to a lazy detach, and re-verifies the stock hashes and the mount
  table; it prints `GXR_UPDATE_STATE=STOCK` only when the restoration is verified and
  exits 1 otherwise.
- Scope re-verified against the stock libraries: the two payloads differ by exactly
  the three documented patch sites (the two inverted `cinc` condition bytes and the
  four bytes of the forced reported value) and by nothing else. No telemetry and no
  network use anywhere in the module; the only state written is `last-run.log` in the
  module's own directory.
- Removal edge observed live on 2026-10-15: disabling and then deleting 0.3.1 on the
  headset unlinked the payload files but left both file bind mounts live until
  reboot (`mountinfo` source ends `//deleted`; both paths still read back the patched
  hashes). Nothing of the module can run after its files are gone, so the documented
  flow is: delete while enabled (uninstall.sh), or `action.sh off` before disabling;
  a reboot always restores the stock libraries.
- Packaged artifact `gxr-mvhevc-eye-fix-0.3.2-auto.zip`, sha256
  `05e091d4de7ee626cadbfeaad934fb693d7bae71de4226776f5bb113d8cda8dc`; the
  `tests/verify_package.py` GXR-installer replay passes.

### 0.3.1

- Validated on the owner's headset 2026-10-15: enabled in the GXR manager; its soft
  restart dispatched service.sh, which applied both bind mounts as root; Samsung
  Gallery XR then played the test MV-HEVC video with correct eyes
  (`onStereoModeInfo mode: 1`, stereo mode 4). Packaged artifact unchanged:
  `gxr-mvhevc-eye-fix-0.3.1-auto.zip`, sha256
  `9a1974182c4dbe2921ade950faf0e503505b8eb5276b28e279864d0f4023ad8d`.
- The module Action button (and a manual run without arguments) now applies both
  mounts if needed and then reports; the manager's replacement checks still use
  the explicit `off` and `status` arguments. Lets a test run happen without
  enabling the module or rebooting.

### 0.3.0

- Root cause corrected: libaplayer.so computes its own `primary-eye` value and is the
  producer that feeds SemMediaPlayer's `onInfo(MEDIA_INFO_STEREO_PRIMARY_EYE = 10985)`;
  the 0.2.x patch to libstagefright alone did not change what the app received
  (verified on the headset: with the patch mounted the log still showed
  `onStereoModeInfo mode: 2`). 0.3.0 patches both computation sites
  (`cinc ne -> cinc eq` at `libstagefright.so:0x1bbb20` and `libaplayer.so:0x233e18`)
  and forces the value APlayer reports to the app to 1 (`libaplayer.so:0xf568c`,
  `ldr w3,[sp,#0x24] -> mov w3,#1`), so the app keeps `MULTIVIEW_LEFT_PRIMARY`.
- `service.sh`/`action.sh` now mount/unmount both libraries; status distinguishes
  ACTIVE / STOCK / PARTIAL. Stock status lines no longer contain the substring
  `ACTIVE` (the GXR manager's replacement check vetoes any line containing it, and
  `INACTIVE` contains it — 0.2.x output was incompatible with that check).
- Same GXR package contract as 0.2.2 (root + `bin/` layout, empty `disable` and
  `skip_mount`, `action.sh` + `auto.sh` + `module.prop` in `SHA256SUMS`).

### 0.2.2

- Package layout completed for the GXR manager's installed-lifecycle verification
  (v0.2.1 was rejected with "Unverified installed lifecycle script"): added the
  required `auto.sh` lifecycle entry (delegates to `action.sh`) and a host-side
  replay of the installer's validation (`tests/verify_package.py`). Same
  single-byte patch as 0.2.0/0.2.1.

### 0.2.1

- Package layout corrected for the GXR manager installer (v0.2.0 was rejected with
  "Unexpected payload directory"): the patched library moved from `payload/` to
  `bin/libstagefright.so`; added `install-info.txt`; `action.sh` now prints
  `GXR_UPDATE_STATE=STOCK` after a successful `off`/inactive `status` for the
  manager's replacement check. Same single-byte patch as 0.2.0.

### 0.2.0

- Test build (unpublished; rejected by the installer before activation). One-byte flip
  at `libstagefright.so` file offset `0x1bbb20` (`cinc w23, w8, ne` -> `cinc w23, w8, eq`)
  correcting the primary-eye computation: left-primary MV-HEVC files now report
  `primary-eye = 1` (left) instead of the inverted `2` (right). Gallery XR receives
  `extra = 1` via SemMediaPlayer (`MEDIA_INFO_STEREO_PRIMARY_EYE` = 10985) and keeps
  `MULTIVIEW_LEFT_PRIMARY` instead of switching to `MULTIVIEW_RIGHT_PRIMARY`, which
  crossed the eyes.
- Applied by `service.sh` as a file bind mount over `/system/lib64/libstagefright.so`
  (checksum-guarded, exact-build guarded); `action.sh status|on|off` for live
  apply/remove without reboot. Ships disabled; ships the `skip_mount` marker required
  by the KernelSU-Next/GXR installer.
- Source library sha256 `8db37c90c458e92faad1f0a6160d53e769da9759c6db14fb2cfedbcec1727490`,
  verified byte-equal on the target headset before patching.
- Superseded approach (0.1.0, never installed): rename of the `primary-eye` key
  string (`primary-eyx`) in the same library; replaced by the exact computation flip
  above, which keeps the event and reports the corrected value.
