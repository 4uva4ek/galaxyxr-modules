# openxr-velocity-frame

## 0.3.5

- Embed the stable per-module update feed in the package.
- Document source selection, ZIP installation and disabled replacement in the updated GXR Manager.
- Keep watching after verified third-party app exit/crash, zombies and PID reuse, including early exits.
- Pin PID/start identity through activation and health checks; distinguish verified pre-write process exit from real errors.
- Clear the pending marker after successful native readback or a verified no-write result; retain containment for write, integrity and restore errors.
- Preserve correction math, firmware hashes, compatibility and 30-second health checks.

Local regression tests and read-only native checks support the lifecycle change. Enabled automatic app-exit/relaunch on the headset is not yet validated. Native object throwing and full cold-boot behavior are not established by package tests.

The old module must stop and restore successfully before replacement. The replacement stays **DISABLED**. Enable separately when intended; no automatic restart is requested by installation. Action reports runtime status and recovery.

[Release notes](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/openxr-velocity-frame/v0.3.5)

Previous public version: 0.3.3.

## Deprecated - 2026-10-18

- Standalone component superseded by `gxr_steamlink_suite`; existing release ZIPs remain immutable.
