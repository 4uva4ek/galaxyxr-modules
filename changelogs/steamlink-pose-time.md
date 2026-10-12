# Steam Link Pose Time

Author: **4uva4ek**.

## Unreleased

No pending public changes.

## 0.1.3 — 2026-10-13

- Resolve identical pose bytes republished under a newer slot label by the sample with the latest receipt (the `GetControllerState` call that built the packet) instead of passing the packet through with stock timing.
- Measured cause in `capture-20261013-011222`: 44 of 10 796 packets per hand carried stock timing, 41 of them identical to an earlier matched pose; they produced four of the five largest API backsteps (28–42 mm at ~1.5 m/s). Packets without any matching sample still pass through.
- Treat unknown or changing Velocity Frame consumer code as a bounded wait (30 s per app process, state confirmed by a second read) instead of a fatal same-boot failure: the worker raced Velocity Frame's activation writes and disabled itself after an app restart. Only an active Controller Extrapolation gate or a persistently unknown code still fails closed.
- Status counter `relabelled` replaces `ambiguous`; control structure, guards, geometry, velocities, lifecycle scripts and the Manager off/status contract are unchanged.
- Live motion re-check of the 0.1.3 payload is pending. Installation and publication do not authorize enablement or reboot.

## 0.1.2 — 2026-10-13

- First public automatic-only package; installs disabled.
- Forward the full consumed controller slot epoch through the native vrlink time field, with no mean subtraction or timer.
- Preserve the manually tested payload; require Steam Link 2.0.20 and Velocity Frame.
- Include app lifecycle guards, restoration, English Action reporting and Manager off/status update contract.
- Build from user-supplied pinned libraries; no vendor libraries in sources or assets.
- Known limitation: rare guarded packets retain stock timing. Physical latency, native throwing and enabled automatic app-relaunch/reboot remain unvalidated.

## Local history (not public releases)

0.1.1: installed disabled after user approval of full-epoch RAM experiment.
0.1.0: centered-epoch preparation, never installed.
