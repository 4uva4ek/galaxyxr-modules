# SystemUI Optimization

Author: **4uva4ek**

Experimental private-memory optimizations for the pinned Galaxy XR SystemUI
library. No published release or proven broad performance improvement yet.
Original device libraries are never overwritten.

## Candidates

The default 0.4.0-experimental implementation combines three fixes.
It removes two redundant `xrWaitSwapchainImage` calls
from `XrVulkanPlatform::acquire`. Its inner `XrVulkanSwapChainImageHandler::acquire`
already acquires and waits on the selected color/depth images. The outer waits
violate call order. Only the two outer BL instructions become NOPs; primary
acquire/wait, BeginFrame failures, rendering and release remain unchanged.
The saved profile attributes all six sampled error stacks to the outer wait.
It also redirects depth release/submission to the depth swapchain selected by
`SwitchSwapchainLayers`, retaining the earlier promising depth candidate.
Both function signatures and all four wait/depth instruction sites are checked
together. That component stops all threads once and rolls back its write set
on failure. The third component coalesces redundant hidden-panel enables until
the explicit panel owner decides visibility for the current frame.
It retains content gating and falls back for unknown/stale owners.

The coordinator validates both components before activation, applies each under
its own all-thread guard, and restores both if activation fails. Status and
activation reject mixed states; `off` restores known partial states as well.
This is a recoverable two-stage activation, not one simultaneous write across
all three libraries. A full power cycle clears every private-memory change.

The separate `GXR_SCHEMA_CANDIDATE` implementation specializes two calls inside `ApiLevelValidator`:
the existing 12-byte `requires_api` prefix comparison, and parsing one/two ASCII
decimal digits. Other numbers and short keys tail-call the original functions.
The API-level comparison, FlatBuffers validation, status/error construction,
scene scheduling and pose/input processing remain unchanged. No results are
cached across commands. The key fast path relies on the existing verified
FlatBuffers String layout immediately preceding the key data.

The separate `GXR_DEPTH_CANDIDATE` implementation corrects depth release and
submission to use the swapchain selected by `SwitchSwapchainLayers`. It is
retained as a promising candidate at the user's request. It does not fix the
observed repeated `xrWaitSwapchainImage` error in the measured scene.
Standalone candidates must not be layered onto the combined module;
unknown/partial/mixed code is rejected.

Pinned library: `/system_ext/lib64/libImpress-jni-vulkan.so`,
SHA256 `ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42`.
The shell also pins libspaceflinger_jni_vulkan.so to
`7c72d2bd2284223877d5d5701fb69206fd2a202fa4867650b606c9245ff36fc5`
and libsysui_jni_soong_vulkan.so to
`d2060072c6046354d470a5e4949f762160a7d89f20310a8652e57c67805c0863`.
It verifies all three hashes before automatic activation. The native helper
also checks the process name, file size, full function bytes, mapping layout,
and unused padding before attaching and again before writing.

All threads are briefly stopped with the existing bounded ptrace guard.
Writes use private COW mappings with readback and rollback. Threads inside the
modified function/padding cause refusal. The panel callbacks call existing
NodeHandle/Node methods on their original render thread; no remote thread
injection, mounts, priority changes or persistent properties are used.

## Current evidence (2026-10-20)

- Worn-headset baseline: 838 user CPU samples / 15 seconds, no samples lost.
  About 56.5% of one core in sampled user time; a separate process measurement
  including kernel time measured 60.7%.
- Inclusive costs: schema attribute validation 12.89%, API validator 8.71%,
  scene mirroring 12.53% of sampled user CPU. These overlap and must not be added.
- Depth candidate active A/B/A: 80.21% -> 71.96% -> 84.97% of one core.
  Promising association, not yet repeated under a controlled identical scene.
  Call-order errors remained near 72/second. Original code was restored.
- One earlier depth trial was invalid because the headset was idle; its
  sleep-count timer was replaced with a monotonic deadline and low timer slack.
  Host cleanup confirmed restoration. Idle trials now refuse activation.
- Schema candidate active A/B/A: 61.71% -> 62.82% -> 72.89% of one core.
  No clear improvement against the preceding baseline; retained separately,
  not included in the default module. Original code was restored.
- Duplicate-wait fix active A/B/A: 68.73% -> 78.45% -> 84.71% of one core.
  No demonstrated CPU saving. Before/after logs contain 804/747 call-order errors;
  during contains 23 stale boundary entries ending at 14:52:53.309, then none
  through 14:53:05.763. Logging starts at a whole-second timestamp, so the boundary
  includes entries from before activation. Error removal is confirmed; errors
  return after restoration. SystemUI PID 4342 remained unchanged.
- Combined 0.3.0 packaged binary active A/B/A: 69.27% -> 57.81% -> 66.33%
  of one core, about 13–17% less CPU than adjacent stock windows. Call-order
  errors stop after the activation boundary (last stale entry 14:57:24.754,
  then none through 14:57:37.074) and return after rollback. PID 4342 unchanged;
  all four instruction sites and padding restored. One short trial, not proof
  of sustained savings across scenes. Installed 0.3.0 disabled, checksums and
  native DISABLED status verified; no activation or restart performed.
- Local ARM64 emulation covers normal/foveated/absent depth, argument clobbers,
  and 697 schema fast/fallback cases.
  The wait test executes the actual outer and inner ARM64 functions against a
  swapchain state model, preserving one acquire/wait per selected image and
  the existing BeginFrame failure return in all three depth modes.

Evidence: `E:\GalaxyXR\diagnostics\systemui-optimization-20261020`.
See [RESEARCH.md](RESEARCH.md) for the subsequent cloth-covered bench profile,
its non-repeated CPU saving, and the panel-activity investigation.

## Baseline capture

From the repository root:

```powershell
python modules/systemui-optimization/collect.py --serial HEADSET_IP:5555 --output E:\GalaxyXR\diagnostics\systemui-optimization-20261020\baseline-01
```

The output directory must not already exist. The collector checks the headset
model, records SystemUI memory, thread activity, service state and bounded logs,
then measures process CPU across 15 seconds and checks process identity.
All output is stored on the PC; no files or configuration are written on the
headset. Captures may contain private information and must not be published raw.

## Implementation gates

Identify the hot call chains and ownership of repeatedly updated XR nodes before
changing update scheduling. Preserve pose/input, system alerts and overlay wakeup.
Do not disable validation or suspend SystemUI/Home. Historical hidden-display
metrics do not establish that all scene work can stop.

Treat the libhwui ProgressBar animator crash independently from steady-state CPU.
Measure graphics memory separately from Java/native heaps.

Any future runtime patch must match the live binary identity, verify original
bytes, preserve original device files, restore on disable and not automatically
retry after failure in the same boot. Full power-cycle recovery must be
established before testing. Build only automatic packages, ship/install disabled,
and require separate authorization for activation.

## Build and validation

From the repository root, using Android NDK 29.0.14206865:

```powershell
python modules/systemui-optimization/prepare_compatibility.py --library PATH_TO_PINNED_LIBRARY
python modules/systemui-optimization/prepare_panels.py --libraries PATH_TO_PINNED_LIB_DIR --ndk PATH_TO_NDK
python modules/systemui-optimization/prepare_schema.py --library PATH_TO_PINNED_LIBRARY --ndk PATH_TO_NDK
python modules/systemui-optimization/tests/test_schema_fastpath.py dist/systemui-optimization/schema.bin
python modules/systemui-optimization/tests/test_depth_patch.py PATH_TO_PINNED_LIBRARY
python modules/systemui-optimization/tests/test_wait_fix.py PATH_TO_PINNED_LIBRARY
python tools/check_module_layout.py
python tools/check_catalogue.py
python tools/check_module_contract.py
python tools/build.py systemui-optimization --ndk PATH_TO_NDK
```

Vendor-derived compatibility headers are generated locally from the user's
hash-pinned libraries and excluded from the public source tree. Never upload
library dumps or these generated headers. Public release packages contain only
the helper and the small compatibility signatures it needs, not vendor libraries.

`prepare_schema.py` reproduces the committed payload/signature headers from
assembly and the hash-pinned library. It does not modify that library.
For the standalone schema-candidate test executable compile `native/main.cpp`
with `-DGXR_SCHEMA_CANDIDATE`; this is not a manual module package.
Use `-DGXR_DEPTH_CANDIDATE` to reproduce the retained depth candidate.
The automatic 0.4.0 package contains duplicate-wait, selected-depth and panel fixes.
Compile with `-DGXR_BASELINE_ONLY` to retain the earlier two-fix implementation.
The standalone depth binary and 0.1.0 package are retained as historical evidence.

`test_live.py --serial IP:5555 --binary PATH --output NEW_DIRECTORY` performs
an active-only A/B/A experiment, using a bounded 20-second native timer and
host cleanup. The inert upload is copied from `/data/local/tmp` to tmpfs `/dev`;
the upload is removed before testing, and the RAM executable after restoration.
No module is installed or enabled by this test. A host disconnect does not
remove the native timer; suspend or scheduling can delay any user-space timer,
and full power removal clears all private patches.

Automatic packages ship disabled. Startup waits for Android and the pinned
SystemUI mapping, applies once, and checks process identity/runtime state.
Disabling restores the old instructions. An unexpected process restart, error,
or interrupted worker blocks retries for the same kernel boot and disables the
module. Cold boot clears old interruption evidence without changing preference.
Action is status-only. Uninstall does not depend on Android services.
The installer-facing `action.sh off` explicitly restores; `action.sh status`
emits `GXR_UPDATE_STATE=STOCK` only after successful native stock verification
or confirmed absence of the process. Clicking Action remains status-only.

Do not enable or distribute a candidate on the strength of emulator tests alone.

## Panel churn prototype

`native/panels.cpp` removes the measured repeated enable/disable cycles of
12 hidden panels. It is included in the experimental 0.4.0 module and can still
be built separately for diagnosis. See `RESEARCH.md` for device measurements
and validation limits. The existing duplicate-wait/depth instructions remain
unchanged.

Generate its code with `prepare_panels.py --ndk PATH --libraries PINNED_LIB_DIR`,
then run `tests/test_panel_payload.py dist/systemui-optimization/panels`.
Compile `native/panels.cpp` with the NDK ARM64 Android 34 C++ compiler using
`-std=c++17 -O2 -static-libstdc++`, outputting
`dist/systemui-optimization/gxr-panels-candidate`.
Use `test_live.py` for bounded CPU A/B/A testing. Always wrap device testing
in Wake/Sleep try/finally; keep OLED asleep between tests.

`test_panels_ui.py` checks bounded native activation/restoration and captures
idle, volume, local notification and stock contrast-dialog traces. The
notification phase requires `tests/PanelUiProbe.java` compiled with android.jar,
converted with D8, and uploaded separately as
`/data/local/tmp/gxr-panel-ui-20261020.dex`; remove that inert helper afterward.
It uses an existing silent SystemUI channel and cancels its uniquely tagged
notification, with a timeout as fallback. No APK/channel/settings are installed.
`tests/summarize_panel_trace.py` summarizes actual old/new enable transitions.
An Activity launch or successful trace command alone is not a UI regression pass:
inspect the relevant panel's enable, sustained visibility and disable transitions.
