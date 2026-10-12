# HAL Cache Bypass 0.1.1: Sources publication

Version 0.1.1 adds the canonical `updateJson`, author `4uva4ek`, and a higher
versionCode for the Sources catalogue. The native executable and lifecycle
scripts are byte-identical to the verified 0.1.0 release described below.
Only `module.prop` and its checksum change. The original 0.1.0 assets remain
immutable. No headset installation, activation or reboot is part of publication.

Use a GXR Manager with the generic GXR lifecycle-contract installer. Older
Managers with the fixed five-module allowlist cannot install this module.
The package retains its ordinary-installer refusal and empty disable marker.
Stable-channel publication is a distribution decision, not additional hardware
validation or a guarantee that all tracking corrections are eliminated.

## Historical 0.1.0 validation

Published scope: the independent cache-bypass module only. HAL360, HAL Native,
NativeCopy and OpenXR Quality are separate experiments and are not included.
The release contains project-owned native code and Android NDK runtime notices,
without vendor libraries, APKs, device dumps, credentials or calibration data.

### Compatibility and installation

The module checks SM-I610 and the exact `libsctrlclient.so` SHA-256
`36120385f25c61f5c2d832d2ac2ebf2179dea1ba8142beeb1001994b7f0d22c0`.
Activation also requires KernelSU's real late-load mode and a completed, stable
coordinated soft-restart generation. Unsupported or unavailable state is refused.
The ordinary predictor's special paths, limits, smoothing and filters remain.

Only the disabled automatic package is distributed. The GXR Manager reviewed at 0.1.0 publication
installer does not allow `gxr_hal_cache_bypass`; standard ZIP installation is
also deliberately blocked. Reviewed direct deployment is required. No Manager
change, installation, activation, service restart or headset write is part of
this publication. It is a prerelease outside the stable catalogue and feeds.

### Evidence

- NDK 28.2.13676358 ARM64 build uses warnings as errors. The release is built
  from a clean source snapshot and compared with the retained tested package.
- The native binary SHA-256 is
  `235b3258ec05ec4bb8b13bc1c995519e544ccc41226eba70002a159f0c78f56e`.
  The previously installed and tested ZIP SHA-256 is
  `26fb4368341f934382530c09b85a11f66300f90bd10ce57ef8a8654d908b6ec6`.
- Clean-snapshot release ZIP SHA-256:
  `ae7625d91a2a52cf48659a0f62bb7652d9204e9101592007a104cec9f25b19c8`.
  Its only differences from the retained ZIP are LF normalization of `NOTICE.txt`
  and the corresponding checksum entry. The executable and all lifecycle scripts
  are byte-identical to the tested package. Archive identity, safe paths, disabled
  markers and complete payload checksums pass the shared release validator.
- Prior on-headset owned-child selftest passed instruction patching, execution,
  preservation, restoration and thread detach without service operations.
- Retained deployment evidence confirms the previous patch was disabled and
  restored, the replacement installed disabled with every payload hash checked,
  and original system library hashes unchanged. Subsequent explicitly authorized
  activation reported a verified module-owned HAL cache bypass.
- Publication checks rerun the 15 shell gate scenarios and 12 lifecycle tests,
  covering cancellation, disable, ownership, health failure, identity reuse,
  interruption quarantine and restoration failure. Tests use mocked OS/native
  boundaries and do not access a headset. Initial sandboxed Git Bash launches
  were blocked by Windows; tests pass outside that sandbox.

### Limits

The user previously reported smoother motion during a combined cache-bypass and
temporary HAL-source test. That does not isolate the cache-only effect. Later
recordings still showed timing discrepancies and motion corrections. Recomputing
the normal cached branch does not guarantee the returned pose matches every
requested epoch, nor does it stop HAL from returning poses.

Native throwing, sustained 360 Hz source tracking, long-run reliability and a
complete failure-injection matrix on hardware are not validated. Publication
adds no new on-headset validation. Power removal clears private RAM changes;
disable the module for persistent rollback because a later authorized late-load
soft restart can reapply an enabled module. Never restart the controller service
as a recovery shortcut.
