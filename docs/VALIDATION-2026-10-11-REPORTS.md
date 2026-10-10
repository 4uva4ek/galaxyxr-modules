# Action reports release validation, 2026-10-11

Versions: Haptic MAIN 0.4.0, Controller Extrapolation 0.3.0,
OpenXR Velocity Frame 0.2.0. Both manual and automatic archives are built.

- Package hashes, complete manifest coverage, ARM64 ELF, identical native helper
  between variants and lack of early-boot hooks passed for all six archives.
- Shared worker lifecycle tests cover failures, restart restrictions, pending
  recovery, disabled state, identity guards and busy status-reader retry.
- All staged shell scripts passed Android `sh -n` and package checksum verification.
- Manual Action reports ran on device for all three modules. Runtime correction
  completed a manual CORRECTED -> STOCK cycle before automatic installation.
- Auto packages installed using a reviewed copy/swap script, preserving previous
  module directories under /data/local/tmp. The standard installer was not run.
- OpenXR auto reported CORRECTED for Steam Link PID 13367 after installation.
  Installed Action reports and checksums succeeded for all three modules.
- Haptic MAIN and extrapolation remain disabled and STOCK, preserving their actual
  pre-update behavior. The formerly misleading checked-but-inactive state was
  migrated to an explicit disabled marker. Extrapolation conflicts with correction.
- A status/startup lock race was found during initial deployment; the worker
  safely restored STOCK. Final worker treats exit 2 as busy and retries, with
  health reads serialized against Action. The corrected installation succeeded.

KernelSU APK, ksud, kernel, system libraries and startup metadata are unchanged.
No reboot or subjective motion/vibration retest was performed in this update.
ADB went offline after successful deployment; the final 30-second health-check
readback was not yet confirmed at the time of this record.
