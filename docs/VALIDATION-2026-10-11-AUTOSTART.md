# Manual/automatic variants: validation on 2026-10-11

Packages: Haptic MAIN 0.3.0 and Controller Extrapolation 0.2.0, each built as
separate manual and automatic ZIPs. Both variants use the same module ID and
native patcher. KernelSU source changes proposed during development were reverted;
the automatic packages use only the standard `service.sh` lifecycle hook.

## Host checks

- Eleven lifecycle tests passed, including first-run initialization, preserving
  disabled state, failed activation, interrupted-attempt blocking, process identity,
  manual override, target restart handling, and waiting for an unloaded runtime.
- Both ARM64 builds passed with NDK 28.2.13676358 and compiler warnings as errors.
- All four ZIPs passed entry allowlists, checksums, matching native patcher,
  ARM64 ELF, mode labeling, and startup-script exclusion for the manual variant.

## Headset checks and deployment

- Automatic payloads were first extracted into tmpfs `/dev` for temporary tests.
  Android shell syntax and payload checksums passed. Native binaries matched the
  previously installed manual versions byte for byte.
- Haptic auto applied MAIN in controller-service PID 3302. The process survived
  the observation window; status readback passed and the pending marker cleared.
  TracerPid was 0. Returning to STOCK with the original manual module passed.
- For both temporary modules, deliberately withholding their own NOTICE file
  caused checksum failure. Auto disabled itself and a later `service.sh` invocation
  did not reenable it. Test files were restored; temporary workers were stopped.
- Automatic modules were placed under `/data/adb/modules/` without invoking the
  KernelSU installer. Original manual directories were moved intact to
  `/data/adb/gxr-manual-backup-20261011/`. No root acquisition settings, KernelSU
  binaries, firmware, system files, or metadata startup files were modified.
- Both `service.sh` scripts were invoked explicitly for this root session.
  Installed Haptic MAIN reported MAIN and cleared its pending marker after the
  observation window. Both automatic settings remained enabled.
  This does not establish that the current temporary-root flow invokes KernelSU's
  service stage after a future reboot. No full reboot was performed.
- System APK/library SHA-256 hashes remained unchanged; SELinux was Enforcing.
  Steam Link had not loaded OpenXR during the waiting-state check; successful
  automatic extrapolation in an active XR session remains to be checked.

## Installed automatic ZIP hashes

```text
c9bb1e54c6a84d608a7adefd785fbc1c8511c763da2a45c2f2a8e9cdfeccdf51  gxr-haptic-main-0.3.0-auto.zip
1bc94a4b93faeb59b09c7860966a198b8ec155954c0c6b075488a75dd8234029  gxr-controller-extrapolation-0.2.0-auto.zip
```

Disable auto before using reboot as rollback. The backup directories are outside
KernelSU's module scan directory and do not start automatically.
