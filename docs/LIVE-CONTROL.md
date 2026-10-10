# Checkbox behavior and scope

The proposed custom KernelSU live-toggle integration was dropped at the user's
request. This release does not require it and does not register its opt-in flag.
The internal `live-control.sh` is called by explicit module commands only.

The KernelSU checkbox is a saved permission, not proof of an applied patch.
An automatic worker respects `disable`; when it is already running it observes
a disabled checkbox and attempts live rollback. A stopped worker does not start
merely because an older Manager removes the marker. Use `action.sh on` explicitly
or the ordinary KernelSU service stage. Action reports state and instructions.

Existing fail-closed guards can still create `disable` on activation/health
failure. Automatic checkbox refresh is not promised; refresh the Manager list.
No KernelSU patch is shipped solely to synchronize this UI. The previous separate
`autostart/enabled` file is now only a compatibility mirror, not a second preference.

Read [Action report](ACTION-REPORT.md) and [recovery](AUTOSTART.md).
