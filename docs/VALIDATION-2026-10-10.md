# Haptic MAIN 0.2.0 — validation on October 10, 2026

Device: Samsung Galaxy XR SM-I610, Android 14/API 34, temporary KernelSU root.
The original files and their exact SHA-256 hashes are listed in the module README.

- MAIN → STOCK → MAIN through KernelSU's standard Action command: successful.
- The user confirmed vibration in the grips of both controllers in Steam Link
  2.0.20 with the HAL pose/haptics layers and their bridges removed.
- The controller service did not restart. TracerPid was 0 after switching.
- System library and APK hashes were unchanged. SELinux remained Enforcing.
- No startup scripts, rc files, system.prop, sepolicy.rule, mount substitutions,
  or background daemon are included.
- Checked ZIP integrity, every packaged file's hash, ARM64 ELF format, shell
  syntax, and exclusion of unrelated components from the package.

A full reboot was not performed during this session. Restoration after reboot
follows from modifying only private process memory and having no boot-time
reactivation. Long gaming sessions and other firmware builds have not been validated.

The earlier Frida-based 0.1.0 attempt failed with a transport error before JavaScript
execution. The exact underlying system failure was not established. Version 0.2.0
removes Frida entirely and does not relax SELinux to load an agent. Materials from
the 0.1.0 attempt are not included in this repository.

## Standalone repository validation

A clean Git clone built successfully with NDK 28.2.13676358 without the previous
workspace, device dumps, or downloaded dependencies. The ZIP from the working
copy matched the ZIP from the clean clone:
`ed4fb90f51e14b2285a6895a596d2e4a5fa82b9bfd362e9440feb16519cea12d`.
The binary and runtime scripts match the headset-tested 0.2.0 files byte for byte.
The complete ZIP hash differs from the earlier package because ZIP metadata is
now fixed; functional files were unchanged.
