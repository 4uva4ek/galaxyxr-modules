# OpenXR QUALITY 0.1.1 and Controller Whitening 0.1.7

These automatic-only packages install disabled. Author: **4uva4ek**.

QUALITY is a first public experimental release. It holds the stock vendor
HEAD_TRACKING QUALITY request during visible OpenXR sessions, with session,
thermal, firmware/ABI, late-load and failure guards. Its native implementation
is unchanged from the locally validated 0.1.0; 0.1.1 adds the Manager's off/status
update interface and waits for verified restoration before replacement.
The normal Action button remains a status/recovery report.

Whitening 0.1.7 recognizes only the exact paired IMU wait-gate mutation during
full-code verification and restoration. The whitening correction and lifecycle
remain unchanged. Partial or unknown modifications still fail validation.
This does not publish, install or reactivate IMU Time.

## Validation

- Build from an isolated checkout of current main using pinned Android NDK
  28.2.13676358, warnings as errors; disabled automatic package and hash checks.
- Module layout, catalogue and feed validation; shell syntax checks.
- QUALITY Action/update contract tests cover inactive/live/error reports,
  delayed stop, orphan restore, restoration failure and timeout. Safe-update
  state is emitted only on successful native confirmation of no controller
  or owned override. The native binary and unchanged startup/gate behavior
  reuse their existing validation evidence.
- Whitening reuses retained native compatibility, activation/restoration and
  lifecycle evidence from local 0.1.5/0.1.7; release binary equality is checked.

Retained local evidence: `diagnostics/quality-whitening-release-20261013/`,
`diagnostics/imu-time-module-20261012/compatibility-restored.json`, and
`diagnostics/tracking-reassessment-20261012/quality-session/` in the development
workspace. Those device receipts are not part of the public payload.

## Limits and migration

QUALITY force/release and an asleep lifecycle were tested on the pinned device.
Visible-app minimize/restore, sustained camera rate, subjective improvement,
active disable and full power-cycle validation remain unconfirmed for this
module. Status validates controller identity and bookkeeping; it does not
independently read the vendor force latch. Native throwing remains unvalidated.

Existing unpublished QUALITY 0.1.0 needs reviewed direct migration because its
Action script lacks the Manager contract. Stop/restore and verify the old helper
before replacement, preserve `state/` guards, and install disabled; see its
module README. A new installation and subsequent 0.1.1 replacements use the
generic GXR lifecycle-contract installer in KernelSU GXR 1.0.6 or later.

Release preparation does not enable a module or perform a soft/full reboot.
