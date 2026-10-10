# Action button report

Action invokes a English status report for all three modules, in both variants.
It reports status without toggling, stopping, restarting or applying a patch.
A failed status read does not change the KernelSU checkbox.

The report explains the saved permission separately from live state:
`ENABLED` means the patch is applied; `DISABLED` means it is not applied.
Waiting is a reason attached to DISABLED. Failed verification is reported as
unconfirmed rather than inventing a successful state. The checkbox is shown as ON/OFF.

The report prints commands using the actual installed module path:
- `action.sh on`: explicit activation; for auto, restart the worker and retry.
- `action.sh off`: stop automatic application and restore live code.
- `action.sh status`: raw state for diagnostics.

With KernelSU GXR live-control, the checkbox immediately starts or stops the
module. After fixing an error, enable it again; if already checked but inactive,
toggle off/on. The report also provides root-shell fallback commands for older
Managers. Action itself remains a read-only status request. No button is renamed.
