# Module restart lifecycle candidates

Haptics 0.5.3 and OpenXR Velocity Frame 0.3.2 address worker lifecycle failures.
Both candidates were installed **disabled** after the user's explicit request.
Old versions were disabled/restored and preserved outside the active module
directory. No new activation or soft reboot has been performed for this change.
They do not modify the KernelSU application.
The new Velocity helper's read-only `list` command was checked on the headset:
exit 0 in 0.156 seconds, no eligible app currently loaded. Its separately staged
test file was removed afterward; no target memory or module preference changed.

## Evidence and limits

The headset audit found Haptics blocked by a persistent unfinished-activation
marker from an earlier boot, with its native code currently STOCK. Velocity's
worker log showed a failed PackageManager query and a failed cleanup query.
The same read-only native discovery succeeded once Android was ready. Both
checkboxes had subsequently been enabled while their workers remained absent.
Checking a module alone does not launch its worker in standard KernelSU GXR.

The old scripts reproducibly disabled themselves in local simulations: Haptics
on unfinished activation / service replacement, Velocity when boot readiness
changed after its one-time startup check. The exact time of Velocity's recorded
failure is unavailable; the restart mechanism is supported by code and reproduced
locally, rather than proven for every reported random disablement.

## Changes

- A standard `emulated-soft-reboot.sh` notification creates only a tmpfs notice
  for an existing worker. It neither starts a worker nor clears failure markers.
  No live-checkbox extension, early-boot hook or blocking pre-stop patching.
- Workers continue checking Android readiness. An announced restart permits the
  disappearance of the previous private process mapping; surviving processes
  remain under health checks. Unannounced crashes retain failure containment.
- Velocity package discovery has a bounded native query and return code 3 only
  before target memory access. Workers wait through temporary unavailability;
  real patch errors still disable. The stale errno suffix `(Success)` is removed.
- Velocity tracks whether restoration is actually needed, reports restoration
  failure as an error, and does not silently drop a still-living patched process.
- Action remains a status report. If its worker has stopped, it also shows the
  last worker messages, explicitly labelled as possibly historical.

## Validation and rollout

Run `tools/test_autostart.py`, `tools/test_restart_lifecycle.py` and the POSIX
`modules/openxr-velocity-frame/test_package_query.py`. The latter compiles the
production query implementation and tests success, empty/large responses,
command failure and two timeout cases. Rebuild and verify both disabled ZIPs;
run the existing velocity math, compatibility and ARM64 prediction tests.

The new lifecycle requires a real headset soft-restart test before being called
validated on-device. KernelSU GXR 1.0.0 and the installed 1.0.1-test1 source both
provide the standard notification stage; no new Manager build is required.

Validation completed: 13 existing worker tests, 10 new lifecycle/action/hook tests,
six compiled production package-query scenarios, 6,000 native math cases,
10,096 rejected corruptions and 1,728 emulated ARM64 prediction cases. Both ZIPs
passed disabled-package, integrity and ARM64 checks. Haptics' native binary is
byte-for-byte unchanged from 0.5.2. New Velocity discovery passed on the headset;
module activation and reboot behavior remain untested there.

Device backup: `/data/adb/gxr-module-backups/restart-fix-20261011-053-032`.
Local receipts and pre-update archives are under
`E:/GalaxyXR/diagnostics/module-lifecycle-20261011`. Final checks: both checkboxes
OFF, no workers, Haptics STOCK, no eligible OpenXR app process, same kernel boot
and ksud. Other modules' saved preferences and properties are unchanged.

Keep the installed originals and failure evidence. Before an authorized update,
disable and restore an enabled old module, then deploy the new package disabled
in a fresh directory. Do not enable the replacement or execute a soft reboot
without a separate request. Do not use the normal module installer on this
Samsung setup: it may rewrite metadata startup files; `customize.sh` still aborts.
An old unfinished-activation marker is not automatically forgiven by this fix.

Only reviewed module files and preferences persist in `/data`; notification and
worker state are in `/dev`. Native target changes remain private memory patches.
Power cycling clears those memory patches, but an enabled automatic module may
apply again at a later service stage. Disable before relying on reboot as rollback.
