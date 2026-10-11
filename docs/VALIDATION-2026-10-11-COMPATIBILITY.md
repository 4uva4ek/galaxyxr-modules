# Controller module compatibility

Retirement update (2026-10-11): HAL Prediction +30 ms was removed at the user's
request after they reported good operation without it. References below record
historical tests, not an available or recommended module.

Velocity Frame 0.3.0 corrects the copied relation before the existing OpenXR
predictor. Linear input is world-space; angular input is a private stack copy in
the input pose's local axes. Returned angular velocity remains world-space.
Controller Extrapolation 0.4.0 recognizes this exact four-instruction correction
and payload. Partial/unknown code is rejected. Each restores only its own sites.
Actions share an OpenXR operation lock. Velocity refuses mutation while a stopped
thread PC, LR or live stack value references the consumer/payload.

Validation on 2026-10-11:

- 1,728 actual ARM64 runtime/payload prediction cases in Unicorn; interpolation,
  no prediction, both gate states, unknown controller type, no shared-buffer writes.
- 6,000 actual host C++ math cases, guards and 100 origin-shift identities.
- Exact validators reject 10,096 corrupted-byte cases and all partial hook sets.
- 13 shared lifecycle tests and a real-script worker-timeout restoration test.
- Manual native Steam Link test: correction on, extrapolation on, correction off
  while extrapolation stays on, correction on, extrapolation off while correction
  stays on, final correction off. All statuses matched. Original files unchanged.
- The user then closed Steam Link; Android recorded task removal, not a crash.
- Both automatic packages installed disabled after explicit authorization.
  Old packages preserved; no automatic worker or enabled lifecycle tested.

HAL Prediction +30 ms 0.1.1 targets a separate service/library and was left disabled.
Its native bytes are unchanged from prior validated artifacts. Its combined motion
quality, total prediction horizon, throws and a full reboot were not tested here.
Haptics MAIN 0.5.1 likewise retains its previously tested native artifact: MAIN,
step 2 frequency request and unchanged amplitude. Prior tests are historical;
publication is not a new subjective haptic validation.

Distribution update (2026-10-11): following the user's confirmation of good
operation, Controller Extrapolation 0.4.0 is now a regular release alongside
Velocity Frame and Haptics MAIN. HAL +30 ms has been retired. All new
archives are automatic only and contain a disable marker outside SHA256SUMS.
No KernelSU UI or new error-reporting changes are included.
