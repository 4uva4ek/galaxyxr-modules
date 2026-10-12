# Velocity 0.3.4 application lifecycle

The user explicitly requested that closing or crashing an OpenXR application must
not disable the Velocity worker, including during its first 30 seconds. This is
limited to third-party application processes. Haptic/system-service supervision,
KernelSU, correction math, compatibility checks and startup preferences are unchanged.

The observed 0.3.3 failure paths were an ordinary Android `remove task` exit during
the observation window and `Target disappeared before activation`. A separate logger
Steam Link crash at 02:23:27 was a SIGSEGV in `XrAppManager::BReadyToSuspend()` while
recreating NativeActivity. The crash cause is not established by this lifecycle fix.

Changes:

- Forget a verified dead, zombie or replaced PID/start identity and keep watching.
- Pin the discovered start time throughout native activation and health checks.
- Return a distinct no-write result for an exited target, including process read
  and attach races. Unreadable identity and incompatible live bytes remain errors.
- Clear `auto.pending` after successful native readback, or a verified no-write
  result. Interrupted transactions and write failures still retain the guard.
- Keep checking live patch integrity every 30 seconds. Do not launch apps,
  restart apps, bypass a failed write, or reactivate a disabled module.
- Action explains that app exits do not require cold boot and distinguishes
  ordinary waiting from reviewed recovery after an actual patch error.

Validation commands (local; no headset mutation):

```sh
python3 -B tools/test_restart_lifecycle.py
python3 -B tools/test_velocity_identity.py
python3 -B tools/test_autostart.py
python3 -B tools/test_cold_boot_reset.py
python3 -B tools/test_standard_control.py
```

The native identity test compiles the production parser and checks a real Linux
child while alive, zombie and reaped, plus PID reuse and malformed/read failures.
The shell tests execute production Actions/workers with controlled Android state.
They cover early crash, relaunch, PID reuse, discovery and health-check exit races,
and retained write, eligibility and restoration failures.

All 41 local test methods passed (12 restart, 1 native identity, 13 autostart,
12 cold-boot reset, 3 standard controls). The unchanged correction passed 6,000
C++ math cases, 100 origin-shift identities, 1,728 emulated ARM64 prediction cases
and 10,096 corrupted-byte rejections. Package integrity/provenance checks passed.

On the headset, the new helper identified logger Steam Link PID 21318/start 13944,
reported STOCK, and returned no-write result 4 for a mismatched start time and an
absent PID. Version 0.3.4 was installed disabled after verifying that the old 0.3.3
worker was absent and all discovered app patches were STOCK. The old module tree,
including its pending marker, was preserved outside the modules directory and in
a host archive. Action reports checkbox OFF and runtime DISABLED. No app launch,
activation, soft reboot or crash experiment was performed. Automatic recovery
after a real headset app exit still needs an explicitly enabled live test.

Evidence: `diagnostics/velocity-lifecycle-20261012` in the parent workspace.
ZIP SHA-256: `dd967b69e502447413bc978bbc1eca8fa0a250b2ae918308004fdff8b0b0f180`.

Build only the automatic ZIP and keep replacements disabled. Enablement and any
soft reboot still require a separate user request. A full reboot clears app memory,
but an enabled automatic module can apply again; disable it for rollback. No device
or physical tracking validation is implied by host tests. Native throwing is unvalidated.
