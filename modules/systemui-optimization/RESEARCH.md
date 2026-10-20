# SystemUI follow-up research, 2026-10-20

The tested combined 0.3.0 implementation is checkpointed at `d5032a1`.
Its package remains installed disabled. Do not substitute an untested candidate
for this baseline.

## Repeatable bench workflow

User preference: cover the wear sensor with cloth, then use root ADB
`input keyevent KEYCODE_WAKEUP` and `input keyevent KEYCODE_SLEEP` for bounded tests.
Verify actual rendering/CPU and power state before accepting a sample.
Without the cover, wake was reversed after 104 ms with reason `xr_doff`.
With the cover, Awake and active SystemUI rendering persisted. Sleep entered
Dozing with reason `sleep_button`; fully Asleep was not established.
No settings, calibration, or sensor injection were changed.
Visual, gaze and head-motion validation still requires wearing the headset.

The off-body sensor is `stk_stk3a5x Offbody Sensor Wakeup`, handle `0x156`,
type 34. It is not an evdev input device. Local `services.jar` contains
`XrShellCommand` with `offbody true|false` and mode-4 HAL-bypass replay injection;
this alternative was only inspected, not activated.

## Profile with both fixes

Evidence: `E:\GalaxyXR\diagnostics\systemui-optimization-20261020\combined-profile-01`.
`test_live.py --profile` records 8-second, 99 Hz user-CPU call graphs in all
three phases. Profiling adds overhead; cloth/static scenes differ from worn
headset scenes and have no valid gaze/head interaction.

Process CPU: 55.51 -> 56.36 -> 59.77% of one core. This trial does not repeat
the earlier 69.27 -> 57.81 -> 66.33% saving. Retain both observations.
Call-order errors again disappear during the patched interval and return after
restoration. The 17 during-log errors end at 15:04:19.230 (whole-second log-start
boundary); none follow through 15:04:31.616. PID 4342 is unchanged.
Native restoration and installed/startup DISABLED status were checked afterward.

Patched profile: 367 samples, zero lost, about 3.707 seconds of sampled user CPU
over 8 seconds. Inclusive sample shares overlap and must not be added:

| Work | Inclusive share |
| --- | ---: |
| SpfMain thread | 79.56% |
| FEngine thread | 16.35% |
| Flatbuffer attribute validation | 16.35% |
| Scene mirroring | 14.99% |
| API-level validation (within attribute validation) | 9.81% |
| Recursive node-active update | 5.45% |

## Concrete next lead: panel activity churn

`libImpress-jni-vulkan.so` `NodeController::UpdateActiveRecursive` at `0x12f0678`
already compares old/new active bits and skips notifications/children when
unchanged (`b.eq` at `0x12f06a0`). Adding another identical no-op guard cannot
solve the observed cost. `NotifyActiveForEntity` at `0x12e2944` scans component
pools and performs entity lookups before notifying enabled components.

Of 20 sampled recursive-active stacks:

- 13 originate from `SystemPanelManager::ProcessPanelState` at
  `libsysui_jni_soong_vulkan.so+0x4ce92dc`. Disassembly shows `SetEnabled(false)`
  when the panel state byte at offset `0x20` is zero.
- 5 originate from `NodeStateManager::MirrorDefaultState` at
  `libspaceflinger_jni_vulkan.so+0x4d15490`. Disassembly shows `SetEnabled` using
  `HasVisibleContentCombination()` for the relevant node state.
- 2 originate from gaze updates, which must retain their behavior.

Some panel-state stacks descend through `PanelRenderController::NotifyActive`
into `MultiSurfaceRenderer::ResetMaterial`. This is actual downstream work,
not just a repeated setter that immediately returns.

Hypothesis: different owners alternately enable/disable the same panel within
a frame, producing unnecessary component/material churn. NOT proven: sampled
stacks do not identify the node, so the two callers might operate on different
nodes with legitimate transitions.

Next validation must correlate node identity, old/new activity, caller and frame
before changing either owner. Do not suppress notifications globally or skip
hidden-panel state blindly. Preserve actual visibility changes, ownership,
reparenting, gaze/input and system alerts.

Attribute metadata caching remains another lead, but requires establishing
schema lifetime and API-level invalidation. Earlier narrow schema string/number
fast paths did not demonstrate a CPU win and remain excluded from the module.

## Follow-up: panel churn confirmed by node identity

`node-trace-02` uses an isolated tracefs instance and a temporary uprobe on
`NodeController::SetEnabled`. All diagnostic events and the instance were
removed afterward. No original library files were patched.

The 2.004-second capture retained all 28,855 events, covering 162 nodes.
Twelve identical node addresses alternate between:

- `libspaceflinger_jni_vulkan.so+0x4d15494`: disabled -> enabled, 145 times each.
- `libsysui_jni_soong_vulkan.so+0x4ce92e0`: enabled -> disabled, 145 times each.

That is 1,740 full on/off cycles, or 3,480 real state changes across these panels.
Pairs occur within roughly one millisecond and recur at frame cadence.
Affected names: MIRROR_SYSTEM_ALERT, MIRROR_SYSTEM_DIALOG, MIRROR_TOAST_AREA,
MIRROR_APPLICATION_OVERLAY, MIRROR_ASSISTANT, MIRROR_VOLUME_BAR_DIALOG,
MIRROR_LIVE_CAPTION_OVERLAY, MIRROR_STATUS_BAR_SUB_PANEL,
MIRROR_STATUS_BAR_ADDITIONAL, MIRROR_KEYGUARD_DIALOG,
MIRROR_ACCESSIBILITY_OVERLAY and MIRROR_LOCKSCREEN.
The trace also confirms 18,850 unchanged calls from MirrorDefaultState.
Trace overhead means this capture must not be used as a CPU benchmark.

This establishes the repeated same-node transitions, but not permission to
remove either owner's updates unconditionally. The visible branch of
ProcessPanelState delegates through a virtual layout/processing call at
`0x4ce96f4`; it does not simply perform a symmetric SetEnabled(true) here.
Before removing generic mirroring, determine how the visible branch re-enables
nodes, including hidden -> visible transitions and creation/reparenting.
In particular, a MIRROR_ name-prefix skip could hide critical system panels.
At that checkpoint no churn-removal patch had been applied or packaged.

## Follow-up: repeated API metadata confirmed

`attributes-trace-01` traces ApiLevelValidator's metadata vector argument.
The buffer retained 131,217 of 781,548 written events, so it overflowed.
Its retained subset contains 47 unique attribute vectors and 18 validator
addresses; the most reused vectors each occur around 7,600 times. This is
evidence of repetition, not a reliable complete frequency measurement.

Read-only memory decoding of the top four vectors shows a single attribute:
`requires_api = "1"`. They reside in the same writable Scudo secondary heap
mapping, not immutable library data. Caching by raw address alone is unsafe
without schema allocation lifetime/replacement tracking.

Candidate designs:

1. Cache parsed schema requirements with explicit schema/API-version ownership
   and invalidation; continue verifying command buffers and API compatibility.
2. Alternatively, specialize the complete common single-attribute case, checking
   current bytes and API level every call and retaining the original fallback.
   This avoids a stale pointer cache. It must preserve the vendor's exact string,
   numeric parsing and error behavior; earlier small parser fast paths alone did
   not show a win.

These are the strongest identified follow-up directions. Existing patched
profile shares (16.35% attribute validation, 5.45% recursive active update) are
rough opportunity bounds, not promised savings or additive percentages.
The tested 0.3.0 module remains unchanged and disabled; headset returned to sleep.

## Panel prototype debugging and device validation, 2026-10-20

Sources: `native/panel_payload.c`, `native/panels.cpp`, generated
`native/panel_code.h`, `prepare_panels.py`. This remains a standalone native
candidate under this module, not part of the automatic 0.3.0 package.

The first candidate used the address returned by NodeHandle::operator-> as a
stable key. `panel-test-02/test.txt` disproved that assumption: all 36 owners
returned the same temporary Node address with different entity identifiers.
The mirror and owner callbacks therefore never matched. No enable operations
were deferred in tests 01/02. The corrected key is NodeController pointer plus
the current entity identifier, read from each current temporary Node proxy.
No temporary Node pointer survives a callback.

The candidate tracks the previous frame's explicit owners. A mirror enable
of an already disabled owned node is deferred until that frame's owner decision.
A visible owner flushes the deferred enable; a hidden owner retains its original
disable branch. Content gating, unknown nodes, ownership gaps, other threads and
capacity overflow use stock behavior. There is no name-prefix suppression.
If a previous owner unexpectedly disappears, stock mirroring resumes next frame;
arbitrary owner disappearance/reparenting and every system dialog type have not
been validated. Do not promote the candidate on the strength of these tests.

Hooks preserve the original PACIASP landing instruction, replay the displaced
frame prologue, and preserve GP/SIMD/NZCV at the mid-function owner hook.
Code and state occupy verified zero padding in private memory mappings only.
Application/restoration freezes all target threads, checks original/patched
bytes and in-flight callbacks, verifies readback, and rolls back failed writes.
The native test timer restores independently of the host. No original device
file, startup setting or module enablement is changed.

`panel-test-03` deferred 17,292 enables across 1,442 frames: 12 hidden panels
per complete frame. `panel-ui-01/idle` retained every event and showed zero
real enable transitions across those 12 panels over 145 frames, versus the
previous 1,740 on/off cycles over the same number of frames.

Device UI evidence:

- `panel-ui-01/volume`: MIRROR_VOLUME_BAR_DIALOG changes 0->1 once and stays
  enabled; the following notification trace records its 1->0 timeout.
- `panel-ui-01/notification`: NOTIFICATION_PANEL and its window/surface nodes
  each change 0->1 once, remain enabled for approximately 4.8 seconds, then
  change 1->0. The local notification helper confirms POSTED/CANCELLED and
  also supplies an eight-second timeout. It creates no APK or channel.
- `panel-ui-04/dialog`: stock ContrastDialogActivity launches successfully;
  MIRROR_STATUS_BAR_SUB_PANEL changes 0->1 once and 1->0 after Back, with no
  setting selected. This exercises a second dialog panel under the patch.
- These are runtime state/lifecycle checks, not a human visual/input inspection
  or blanket validation of all alert, lockscreen and permission-dialog variants.

Negative diagnostic attempts are retained: `panel-ui-02` could not create a
custom app_process AlertDialog because Android rejected its unregistered
provider caller; only that helper exited, SystemUI PID remained 4342.
`panel-ui-03` long-press Power produced no dialog transition and is not a pass.
Both experiments restored the patch and put the headset back to sleep.

CPU measurements are percentages of one core, without tracing. Test 03 (before
removing unnecessary owner setter calls): 55.87 -> 56.03 -> 57.09.
Test 04 (final prototype): 56.97 -> 55.98 -> 58.27.
Test 05 (same final binary): 59.51 -> 56.06 -> 59.30.
Relative to the average of the flanking controls, the two final runs save
1.65 and 3.35 percentage points of one core (approximately 2.9% and 5.6%).
This is a small repeated improvement in these cloth-covered bench sessions,
not a guarantee across applications, gaze/head movement or other scene states.
Removing the observed churn is established separately from CPU savings.

Cloth remains on the wear sensor. Wake only for bounded live tests; send Sleep
in finally, including failures, and keep the headset asleep while coding.
The resulting power state is Dozing/sleep_button, not a verified full Asleep.

## Combined experimental 0.4.0

At the user's request, the automatic experimental package now includes the
retained wait/depth component and panel coalescing. Activation validates both
components first, applies each with its own freeze/readback transaction, and
restores both on failure. Known partial states cannot be activated or reported
as healthy; explicit off restores them independently.

`all-fixes-test-01`: 55.30 -> 53.32 -> 59.64% of one CPU core. The duplicate
wait error is absent during activation; original code/padding restore afterward.
`all-fixes-recovery-01` deliberately starts the old two-fix component alone and
the panel component alone, each under its own native timer. Combined status/on
reject both mixed states, combined off restores each, and repeated off is safe.

`all-fixes-ui-01` confirms zero hidden-panel transitions over 145 frames,
volume-dialog appearance/timeout, notification appearance/cancel and contrast
dialog appearance/Back. SystemUI PID remains 4342. All temporary patches are
restored and the headset returns to Dozing/sleep_button.

Lifecycle tests cover normal stop, target exit, activation/restoration failures,
interrupted startup, a mismatched additional library hash, installer-facing off
and refusal to emit STOCK after native verification failure. Package validation
checks every entry/hash, the exact tested ARM64 binary and the disabled marker.

The previous local 0.3.0 Action script lacks the installer's STOCK protocol.
Consequently its standard replacement attempt stops safely despite an
independent native DISABLED verification. This is a report-format incompatibility,
not evidence that a live patch remains. New 0.4.0 provides action off/status and
verified GXR_UPDATE_STATE=STOCK for subsequent standard updates.

Publication: `systemui-optimization/v0.4.0` is a prerelease listed only in
`modules-experimental.json`. Source commit `3ccf395`, verified feed `9621d17`.
The clean release build is byte-identical to the tested ZIP:
SHA256 `28fefa0d69a77478230fc1fd884a197258320b613b46e8e0a702d2024484b00b`.
GitHub's uploaded-asset digest matches. Catalogue, contract and build/release CI
passed; the stable-feed workflow correctly skipped this prerelease.

The known local 0.3.0 was subsequently migrated under the module-control lock
after checking its original package manifest and native stock state. The new
helper independently verified all three components before replacement. Old
files/state were preserved, 0.4.0 was installed OFF, and the new Action reported
DISABLED plus GXR_UPDATE_STATE=STOCK. The old directory was archived to the PC
and every archived payload hash verified before obsolete device backups and
test files were removed. No enable/restart occurred; final power state was
Dozing/sleep_button with SystemUI PID 4342 unchanged.
