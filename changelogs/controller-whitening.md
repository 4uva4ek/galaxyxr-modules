# controller-whitening

## 0.1.7

- Recognize the exact paired IMU wait gates during full-code validation, health
  checks and restoration. Unknown or partial mutations remain rejected.
- Preserve the whitening correction, calibration guards, shared transaction lock
  and existing lifecycle behavior. The retired IMU Time module is not included.
- Author: 4uva4ek. Automatic package only; installation and replacement stay disabled.

The binary matches the previously device-validated local 0.1.7. Local 0.1.5
introduced this compatibility, 0.1.6 temporarily removed it, and 0.1.7 restored
it at the user's request; 0.1.5 and 0.1.6 were not public releases.
See [release validation](../docs/RELEASE-QUALITY-WHITENING-2026-10-13.md).

## Unreleased

- Set module author to `4uva4ek` in source metadata; published archives and update feeds are unchanged.

## 0.1.4

- Embed the stable per-module update feed in the package.
- Document source selection, ZIP installation and disabled replacement in the updated GXR Manager.
- Native code and runtime scripts unchanged from 0.1.3; no new tracking or haptic behavior is claimed.

Previous runtime validation applies to unchanged patch behavior; this packaging update adds no new on-device behavior claim. Native object throwing and full cold-boot behavior are not established by package tests.

The old module must stop and restore successfully before replacement. The replacement stays **DISABLED**. Enable separately when intended; no automatic restart is requested by installation. Action reports runtime status and recovery.

[Release notes](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/controller-whitening/v0.1.4)

Previous public version: 0.1.3.
