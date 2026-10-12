# consumePose: retained-record replay and prepared candidate

The candidate is the existing Controller Extrapolation 0.4.2 gate patch combined
with the current OpenXR Velocity Frame correction. No additional timing offset,
filter or prediction algorithm is introduced. This investigation adds executable
regression evidence; it does not change the native patch or claim a live fix.

## Mechanism

In the pinned runtime, `Accessor::consumePose` receives the requested monotonic
timestamp through a pointer. With one selected sample and Accessor+0x10 false,
the branch at `0x1b3860` bypasses prediction. It copies the sample relation and
returns the selected sample's timestamp at `0x1b3918`. Downstream relation data
does not carry that timestamp to Steam Link's outgoing packet.

The existing four-byte change replaces `cbz w10,0x1b38d0` with `nop`. The native
function then computes signed `(requested - sample) / 1e9`, updates position and
orientation, and returns the requested timestamp. The device/input checks, sample
selection, equality case and two-sample branch remain intact. Velocity Frame
must supply base-frame linear and input-local angular velocity before prediction,
and base-frame angular velocity in the returned relation.

This is a change to runtime code in the app's private memory. It is not a HAL,
firmware or original library-file change. The package targets the ordinary
`com.valvesoftware.steamlinkvr` process; it does not target the diagnostic logger
clone. No installation, activation, restart or device access was performed here.

## Evidence

Input: archived `capture-20261012T115107Z`, the existing 30-second common window.
The test reconstructs the selected native relation from previously validated
native/API matches, then executes the complete pinned ARM64 consumePose function,
native extrapolation math and current compiled velocity payload in Unicorn.
Only atomic, clock, logging and libm boundaries are provided by the host.
An independent quaternion/vector oracle checks the output.

- 5,051 matched fresh states, each run with all four combinations of velocity
  correction and extrapolation: 20,204 executions.
- All fresh-state requested times precede their published native labels. With the
  gate closed, returned minus requested time is 15.522167–34.118442 ms,
  median 27.159129 ms. Opening the gate makes the difference exactly zero.
- 16,035 matched repeated API states also execute correctly with both patches.
  Requested minus sample time ranges from -31.989661 to -1.524242 ms.
- Twenty synthetic endpoint/server-flag cases cover -40 ms to +40 ms, including
  exact equality. Twenty two-sample cases verify the unchanged interpolation
  path, including requests outside the pair. Two missing-input cases preserve
  the caller's time/output and report failure.
- Producer slots, callee-saved registers and stack are preserved. Maximum
  component difference from the independent numerical oracle is below 8.22e-6;
  this compares arithmetic, not physical tracking accuracy.

Reproduction from this repository (Python with NumPy and Unicorn 2.1.4):

```text
python modules/controller-extrapolation/tests/replay_runtime.py --runtime <pinned-libopenxr_android.so> --capture <archived-capture-directory> --output <report.json>
```

The capture directory must contain the original locate log/manifest and retained
`left/right-native.npz` and `left/right-locate-matches.npz` analysis products.
Velocity payload defaults to `dist/openxr-velocity-frame`; its runtime hash must
match. Report records the exact payload SHA-256. This test never contacts a device.

## Prepared artifact and limits

Local rebuild: `dist/controller-extrapolation/gxr-controller-extrapolation-0.4.2-auto.zip`.
SHA-256: `3b50e7005df8d4aef40c94747869420353aa98a5c1a994b85d026f41953a4d16`.
Automatic-only package, supplied disabled. Package checksum/ARM64/startup checks
and the worker-timeout restoration test passed. Existing native behavior and
version are unchanged; this is a validated rebuild, not a new algorithm release.

The earlier position-only continuity model reduced right-hand braking median
handoff residual from 3.141821 to 0.814814 mm when using published native labels.
That supports this candidate but is not a physical error measurement.

The replay reconstructs sample selection; it does not reproduce concurrent bank
updates or prove which branch executed on every captured invocation. The native
label is not independently established as the physical pose epoch. Therefore
27.159 ms must not be subtracted as a fixed correction. Live motion quality,
tracking-loss/reacquisition behavior, overshoot and native throwing remain
unvalidated. Do not enable a module or collect new measurements without the
user's next instruction; current preparation is complete offline.
