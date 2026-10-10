# Action button report

Action invokes a Russian status report for all three modules, in both variants.
It reports status without toggling, stopping, restarting or applying a patch.
A failed status read does not change the KernelSU checkbox.

The report explains the saved permission separately from live state:
`MAIN`, `CORRECTED` or `EXTRAPOLATION` means applied; `STOCK` means unpatched;
`WAITING` means the target process or library is not ready.

The report prints commands using the actual installed module path:
- `action.sh on`: explicit activation; for auto, restart the worker and retry.
- `action.sh off`: stop automatic application and restore live code.
- `action.sh status`: raw state for diagnostics.

For a retry, enable the checkbox first and run the displayed `on` command in a
root terminal. Action itself does not retry. Recheck the result with Action.
Do not retry firmware mismatches or conflicting patches without resolving them.
Old KernelSU checkbox cycling alone does not immediately start a stopped worker.
No Manager APK or kernel change is required by this release.
