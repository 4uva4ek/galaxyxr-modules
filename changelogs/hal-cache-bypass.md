# HAL Cache Bypass

Author: **4uva4ek**.

## 0.1.1

- Publish in the Sources catalogue and stable update feed.
- Set author to `4uva4ek` and include the canonical `updateJson` in the package.
- Keep the native executable and all lifecycle scripts unchanged from 0.1.0.
- Install disabled through a GXR Manager with the generic GXR lifecycle-contract
  installer. Older fixed-allowlist Managers do not support this module.

The release-channel change adds no new claim about tracking quality or hardware
validation. The exact compatibility checks and all recovery guards remain.

## 0.1.0


Initial experimental prerelease for Samsung Galaxy XR SM-I610 with the exact
controller library hash documented in the module README.

- Bypass the normal cached-pose reuse branch in the controller service's private
  memory. Recompute through the native predictor without adding a timestamp
  offset, pose correction, query horizon or application hook.
- Require actual KernelSU late-load mode and a completed, stable coordinated
  soft-restart generation. No early-boot activation or system-file replacement.
- Retain process identity and executable-byte checks, all-thread patch guards,
  an independent restoration guardian, verified disable, and same-boot failure
  containment. A confirmed cold boot expires old guards without changing the
  KernelSU checkbox preference.
- Ship only an automatic ZIP with an empty disable marker and SHA-256 sidecar.
  Action reports verified runtime state and recovery instructions.

The current GXR Manager installer does not support this module ID. Standard
installation is blocked; reviewed direct deployment is required. This release
does not join the stable Sources catalogue or update feeds.

The native binary matches the previously device-tested build. Local gate and
lifecycle tests pass. This is not a claim of universally smoother tracking,
removal of every predictor cache, sustained 360 Hz source poses, or validated
native throwing. See [validation and limits](../docs/HAL-CACHE-BYPASS-2026-10-12.md).

## Deprecated - 2026-10-18

- Standalone component superseded by `gxr_controller_suite`; existing release ZIPs remain immutable.
