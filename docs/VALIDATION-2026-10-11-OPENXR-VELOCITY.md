# OpenXR velocity correction 0.1.0

## Scope

Two release packages contain the same native helper and correction payload.
Manual has no startup hook. Auto uses only KernelSU's service stage, waits for
Android boot, and discovers third-party main app processes loading the pinned
runtime. Package name and UID are verified against Package Manager. There is no
Steam Link-only activation filter. System processes are refused independently
by the native helper.

Only Steam Link 2.0.20 has user motion validation. The user reported smooth motion
without the earlier jerks with the runtime coordinate-only prototype. The broader
automatic package uses the identical 1112-byte correction payload. Other apps'
motion quality, throws, app restart and full reboot remain unvalidated.

## Checks performed

- Binary-derived six-pose geometry matched 26,784 recorded controller input poses.
- Actual C++ host tests: 6,000 randomized cases; maximum vector component error
  approximately 1.08e-5. Invalid inputs, disabled operation, unchanged poses and
  offset origin velocity are covered.
- Both ZIP checksums, file manifests, variant separation, identical helpers,
  ARM64 ELF identity, no early-boot hooks, and blocked standard installer checked.
- Android shell syntax and installed SHA256SUMS checked.
- Native discovery found the running third-party OpenXR app. Direct targeting of
  the Samsung tracking system service was rejected without modification.
- Automatic activation in the existing Steam Link process passed, followed by a
  30-second `CORRECTED` health check. `action.sh off` stopped the watcher and
  restored `STOCK`; automatic startup then reapplied the same payload.
- The former Steam Link-specific correction and extrapolation autostart were
  disabled to avoid conflicting transformations/prediction.
- The watcher subsequently discovered Chronolink and applied the patch automatically
  (`CORRECTED` readback), without adding its package to an allowlist. This confirms
  discovery/activation, not its motion or throwing behavior.

## Faults caught and fixed during preparation

Android Binder shell commands rejected inherited stdin from the watcher's
root-only target-list file. The package-query child now uses `/dev/null` for
stdin/stderr and a pipe for stdout. This was checked with redirected stdin before
installation. The worker latched disabled on the failure rather than retrying.

The separate extrapolation module's saved auto preference was still enabled;
the new guard rejected activation until it was explicitly disabled. The failed
worker restored the current velocity patch to stock. Corrected auto activation
and restoration were then checked again.

No standard KernelSU installer was invoked. Deployment copied reviewed artifacts
to a new module directory, retaining replaced preparation packages separately.
No original runtime/APK/service, metadata startup, firmware or partition files
were changed. No new root acquisition mechanism was installed.

## Remaining limits

Autostart requires KernelSU to dispatch `service.sh`. A full reboot was not
performed. A successful memory check is not an objective accuracy measurement.
Apps with their own velocity compensation need separate checks. All unsupported
firmware and modified runtime functions remain rejected. This version does not
combine coordinate correction with the separate prediction module.
