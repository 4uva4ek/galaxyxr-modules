# Galaxy XR Controller Suite

## 0.1.2 - 2026-10-18

- Move HAL-wide LogLevel=0 ownership to Controller Suite.
- Split app priority management into a visible-OpenXR profile mechanism; Steam Link is the initial supported profile.
- Restore priorities on background/sleep and exclude ambiguous Thread-9.
- Update both suites disabled before enabling. Soft-restart startup, HAL ownership and active Steam Link priorities verified on device.

## 0.1.1 - 2026-10-18

- First public suite release, consolidating the existing components under one activation preference and WebUI.
- Declare replaced modules for reviewed migration in KernelSU GXR 1.0.11 or later; install disabled.
- English/Russian WebUI follows the headset locale, with a manual language override.
- Preserve existing component tuning, compatibility checks and recovery ownership.

See the module README for components, migration and validation limits.
