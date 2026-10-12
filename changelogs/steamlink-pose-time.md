# Steam Link Pose Time

Author: **4uva4ek**.

## Unreleased

No pending public changes.

## 0.1.2 ? 2026-10-13

- First public automatic-only package; installs disabled.
- Forward the full consumed controller slot epoch through the native vrlink time field, with no mean subtraction or timer.
- Preserve the manually tested payload; require Steam Link 2.0.20 and Velocity Frame.
- Include app lifecycle guards, restoration, English Action reporting and Manager off/status update contract.
- Build from user-supplied pinned libraries; no vendor libraries in sources or assets.
- Known limitation: rare guarded packets retain stock timing. Physical latency, native throwing and enabled automatic app-relaunch/reboot remain unvalidated.

## Local history (not public releases)

0.1.1: installed disabled after user approval of full-epoch RAM experiment.
0.1.0: centered-epoch preparation, never installed.
