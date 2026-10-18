# Steam Link Pose Time

Author: **4uva4ek**.

## Unreleased

No pending public changes.

## 0.1.7 — 2026-10-13

First public release since 0.1.3. It ships, as one package, the content-epoch
wire time and the adjustable compensation developed in the local 0.1.4–0.1.6
steps listed below. The payload and lifecycle are those of 0.1.6; the version is
raised from the local 0.1.6 so the update-aware Manager offers it over an
installed build.

- Wire epoch = `slot label − compensation`, compensation **0–30 ms, default 15 ms**, set in the module's KernelSU WebUI. **28.82 ms** is the measured original label delay (the true content epoch); **0** is the raw slot label of 0.1.3.
- The compensation is stored in `compensation.conf` and applied live within about 2 s by the worker under a brief target freeze, without a reboot or a Steam Link restart.
- Matching, guards, geometry, velocities and the Manager off/status contract are unchanged from 0.1.3. The user confirmed the module works on the headset; per-value wire offsets other than the raw label are not independently measured.

## 0.1.6 — 2026-10-13 (local development, shipped in 0.1.7)

- Fix the live compensation update introduced in 0.1.5. The control-block write uses `PTRACE_POKETEXT`, which requires the worker to be the target's tracer; 0.1.5 wrote it from the health cycle without freezing the process, so the write failed with "Memory write denied", the worker treated it as fatal, disabled the module and left a `failed` marker — the setting never applied live and the module would not re-activate.
- The live update now freezes the target briefly (as `apply()` already does), re-verifies identity and confirms the control block is ours and enabled before poking the value. A failed update is non-fatal: the current value stays live, it is retried next cycle and is applied at the next activation anyway.
- No payload/ABI change from 0.1.5; `compensation.conf`, the WebUI, the `lead` helper, guards and the Manager contract are unchanged. Installed disabled over 0.1.5 with the stale `failed` marker from the 0.1.5 crash cleared. Not published.

## 0.1.5 — 2026-10-13 (local development, shipped in 0.1.7)

- Make the compensation a runtime setting: the wire field is `(slot_label - compensation - packet_header) / 1000` microseconds, compensation 0–30 ms, default **15 ms** (user's choice as a middle ground). `28.82 ms` is the measured original label lead (content epoch); `0` is the raw slot label of 0.1.3.
- Add a KernelSU WebUI (`webroot/index.html`): one numeric field with the limits, the default and the 28.82 ms meaning spelled out, presets 15 / 28.82 / 0, live status. The page only calls the module's own helper (`lead get|set <us>`) and `action.sh`; input is validated twice (page and helper).
- Store the preference in `compensation.conf` inside the module directory (written atomically by the helper only). The worker re-reads it every 2 s and writes the value into the app-private control block, so changes apply live without reboot; the payload clamps out-of-range values to the default. Health check verifies the range. Status prints `lead_us`; Action prints the compensation.
- Selftest covers default, original (28.82), raw (0) and out-of-range compensation on the same synthetic packet.
- Matching, guards, geometry, velocities and lifecycle are unchanged from 0.1.3/0.1.4. Installed disabled over 0.1.4; enablement and soft reboot require a separate request. Not published.

## 0.1.4 — 2026-10-13 (local development, shipped in 0.1.7)

- Forward the content epoch instead of the slot label: the wire field is now `(slot_label - 28.82 ms - packet_header) / 1000` microseconds. The 28.82 ms is the measured constant lead of the runtime's slot label over the HAL target whose pose the slot carries (2026-10-12 captures), not a learned mean or a tuned value.
- Motivation: with the raw label the PC believed each pose was ~29 ms fresher than it was, so SteamVR extrapolated ~29 ms less (median PC poseTimeOffset +39.4 ms versus +15.2 ms stock). The user reported steadier but lagging controllers.
- Expected: median PC poseTimeOffset near +10.6 ms, i.e. a horizon close to stock with per-packet slot jitter still accounted for. Some of the stock jerk tied to the longer horizon may return; this trade-off is not yet measured.
- Payload otherwise identical to 0.1.3 (latest-receipt matching, guards, geometry, velocities). Status prints `epoch=content`. Native selftest expectation updated to the new constant. Lifecycle scripts and the Manager off/status contract are unchanged.
- Installed disabled over 0.1.3 after restoring its live hooks; enablement and soft reboot require a separate request. Not published.

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

## Deprecated - 2026-10-18

- Standalone component superseded by `gxr_steamlink_suite`; existing release ZIPs remain immutable.
