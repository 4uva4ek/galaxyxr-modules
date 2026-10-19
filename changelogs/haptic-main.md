# haptic-main

## Unreleased

## 0.5.7 - 2026-10-19

- Native routing and tuning are unchanged; this release reduces the module's own load.
- Faster switch: all controller service threads are interrupted together and then collected,
  instead of one polling sleep per thread while the others stay stopped. A switch to the state
  that is already in memory no longer stops the service at all.
- Lighter worker: while the patch is live it only reads `/proc` with shell builtins every
  5 seconds. No getprop, pidof, file hashing or `sync`; the read-only health check runs
  30 seconds after activation and then every 5 minutes, without rehashing files verified
  before the write. haptic-main now ships its own `autostart.sh`; the shared one is unchanged.
- Sudden Android restart (zygote/system_server restart without the KernelSU notice) no longer
  disables the module: when system_server restarted together with the controller service,
  the patch is applied again once the service and its JNI library are back (up to 120 s).
  A controller service exit alone, a restart within 60 seconds of the patch or more than
  3 such restarts per worker still stop autostart and set the disable marker.
- Module author is `4uva4ek`.
- Validation: layout/contract checks, package and installer replay, host tests of the worker
  (idle steady state, checkbox restore, service crash, sudden restart, restart loop limits).
  Installed disabled on the headset over 0.5.6 with verified stock restoration and no restart
  of the controller service; then checked on the headset by the maintainer.

## 0.5.6

- Embed the stable per-module update feed in the package.
- Document source selection, ZIP installation and disabled replacement in the updated GXR Manager.
- Native code and runtime scripts unchanged from 0.5.5; no new tracking or haptic behavior is claimed.

Previous runtime validation applies to unchanged patch behavior; this packaging update adds no new on-device behavior claim. Native object throwing and full cold-boot behavior are not established by package tests.

The old module must stop and restore successfully before replacement. The replacement stays **DISABLED**. Enable separately when intended; no automatic restart is requested by installation. Action reports runtime status and recovery.

[Release notes](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/haptic-main/v0.5.6)

Previous public version: 0.5.5.
