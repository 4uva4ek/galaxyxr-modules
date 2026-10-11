# Standard startup control cleanup, 2026-10-11

Scope: Haptic MAIN 0.5.2, Controller Extrapolation 0.4.1 and OpenXR Velocity
Frame 0.3.1 retain their existing native changes and safety guards. This cleanup
removes the custom Manager checkbox hook, its property, display-name suffix and
packaging. Explicit `action.sh on` routes directly to `auto.sh enable`; Action
without arguments remains a status report. Only standard `service.sh` starts an
enabled module automatically. Automatic archives ship disabled.

Host validation passed:

- 13 existing shared lifecycle tests, including failure quarantine, interrupted
  activation, disabled modules, identity checks, healthy haptic status and restart
  restrictions.
- Three new test groups covering all three modules: explicit start dispatch without
  the removed wrapper; default Action preserving disable/pending/failure/lease
  markers while reporting status; source/package startup declarations.
- The existing extrapolation worker-timeout test still verifies native restoration
  is attempted and the failure remains explicit and disabled.

The 16 shared test groups passed in 7.828 seconds using local Git Bash and mocked
Android boundaries. The separate timeout test passed. Windows sandbox temporary
file restrictions prevented the initial run; the same host-only tests passed
outside that sandbox. These checks did not access a headset, start a real patch,
perform a soft reboot or establish new subjective tracking/haptic results.

The release build also confirmed that all five native helper hashes match the
previously installed versions: no native fix, tuning or safety improvement was
reverted by this control cleanup. Archive contents, descriptions, disabled state
and checksums remain separate release checks. Historical native validation remains
applicable only to the binaries and conditions it tested.
