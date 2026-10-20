# Wi-Fi ADB

## Unreleased

No pending changes.

## 1.0.1 — 2026-10-20

- Wait for boot completion, Wi-Fi IPv4 and adbd without disabling the module.
- Suspend health failure counting while Wi-Fi is disconnected.
- Tolerate transient ADB restarts and retry up to three times per kernel boot.
- Report waiting reasons in Action; retain rollback and fail-closed handling.
- Host regression tests cover delayed prerequisites, reconnects and recovery.
  Cold-boot timing on hardware remains unverified.

## 1.0.0 — 2026-10-20

- Add automatic authenticated ADB TCP on port 5555.
- Restore the prior volatile property on disable, with bounded startup checks.
- Package disabled for the KernelSU GXR reviewed installer.
- Installation and installed checksums verified on Galaxy XR. Runtime activation
  and network connectivity remain untested on hardware.
