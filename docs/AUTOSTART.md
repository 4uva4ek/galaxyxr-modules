# Automatic packages

Each module ships an automatic ZIP. Replace module directories cleanly when
upgrading so obsolete startup hooks cannot remain. The normal installer is blocked
because the tested Samsung installer can modify metadata startup files.
Use the reviewed deployment instructions in each module README.

Action reports state and instructions for each module; it never toggles a patch.
Use `sh /data/adb/modules/MODULE_ID/action.sh on` to activate or retry explicitly.
For auto this starts its guarded worker. Use `action.sh off` to stop and restore.
The report prints complete commands for its own path.

Auto uses the standard KernelSU `service.sh` stage, waits for completed Android
boot and a supported target. A running worker observes a disabled checkbox and
attempts rollback. On old KernelSU, checking an idle module does not immediately
launch its worker; use the explicit command. With live-control support the
checkbox invokes guarded start/stop directly. Use KernelSU GXR live-control for immediate checkbox activation.

The checkbox is the saved permission. Haptic/extrapolation's legacy
`autostart/enabled` is only a compatibility mirror. Pending activation and failure
records remain persistent to prevent unsafe automatic retries after a crash.
Before applying, the worker saves pending state. It clears it only after the same
process survives 30 seconds and the patch passes verification. A service restart,
early app exit or failed check stops the worker. Recovery must be explicit.

A full reboot clears memory patches; disable auto first to prevent reapplication.
Do not force-kill a patch operation. If restoration fails, inspect the reported
error; close the target app or use a full reboot with the module disabled.
No system files, firmware, root acquisition, SELinux or metadata startup files
are changed by the module scripts. Files in the module directory persist; runtime
logs and locks use /dev tmpfs. Full reboot has not been tested for this release.

Do not enable controller-extrapolation together with openxr-velocity-frame.
