# Framebuffer10 0.2.3 startup repair

The shared cold-boot reset creates its supplied state directory for synchronization.
In 0.2.2 Framebuffer10 supplied the native session directory. Native startup correctly
rejects an existing directory without verified clean completion. This caused fresh
startup to manufacture an interrupted session and disable itself before activation.

Framebuffer10 now passes `/dev/gxr-framebuffer10.guard` to the unchanged reset helper.
The native controller retains exclusive ownership of `/dev/gxr-framebuffer10`.
Native code, shader payload, rollback and actual failed-session guards are unchanged.

Validation: 12 cold-reset tests and 9 framebuffer lifecycle/package checks pass.
The new integrated production-startup/reset test covers fresh, same-kernel-boot and
changed-kernel-boot entries and preserves rejection of actual incomplete sessions.
It demonstrably fails with the old call argument. OS/native effects are mocked;
this is not proof of actual 10-bit activation or complete headset lifecycle behavior.

Installed automatic-only 0.2.3 disabled after the user's repair request. Previous
disabled module and diagnosed empty session directory were archived. Full manifest,
OFF Action status and stock 8-bit preflight passed with unchanged boot ID and
SurfaceFlinger PID. No activation or restart performed. Evidence is under
`diagnostics/framebuffer10-startup-20261011` in the enclosing workspace.
