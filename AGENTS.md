# Galaxy XR: persistent device-safety rules

User instruction recorded on 2026-10-10. Applies to all work in this workspace involving the Galaxy XR headset.

- Highest priority: the headset must remain stable and bootable after a full reboot or power cycle. Achieving an experimental feature is secondary.
- Preserve original files on the headset. Do not edit, overwrite, or delete originals to implement a modification. Prefer temporary runtime substitutions (such as bind mounts with replacement files stored separately), in-memory changes, or other reversible methods consistent with these safety rules.
- Do not directly write, erase, flash, or patch firmware partitions, boot images, Verified Boot metadata, bootloaders, partition tables, low-level firmware, or device-specific calibration/identity storage. This includes both slots and logical partitions within super. Do not disable Verified Boot by modifying on-disk images.
- Temporary root is not a rollback mechanism for persistent writes. Treat changes to /data, module installers, and root scripts as potentially persistent and capable of breaking startup; inspect their effects before use. A “systemless” label is not sufficient evidence of safety.
- Keep experimental modifications out of automatic startup. Prefer manually activated changes that disappear on a full reboot. Do not enable root/module/patch autostart if doing so could reapply a failing modification after reboot.
- Keep DFRoot read-only partition protection enabled where available; do not rely on it as the sole safeguard.
- Before applying a headset modification, establish what it changes, whether any writes persist, and how a full reboot returns the headset to a working baseline. If that cannot be established, do not apply it; explain the uncertainty and find a safer approach.
- These rules concern modifications to the headset, not ordinary edits to local source code or build artifacts in this workspace.
