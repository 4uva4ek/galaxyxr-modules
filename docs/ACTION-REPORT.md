# Action button report

Action prints an English status report. It does not toggle, stop, restart or apply
a patch. A failed status read does not change the KernelSU checkbox.

The checkbox is the saved startup preference, separate from runtime state:
`ENABLED` means this module's patch is applied; `DISABLED` means it is not applied.
Waiting and error reasons explain why activation has not completed. Failed
verification is reported as unconfirmed, not as a successful state.

Checking a module does not immediately start an idle worker. Enabled modules start
at the next standard KernelSU `service.sh` stage. With the documented KernelSU GXR
1.0 temporary-root flow, that stage follows the soft reboot after root acquisition.
A running worker notices the disable marker and attempts restoration within its
polling cycle. Action verifies the actual result.

For explicit recovery after investigating an error, the report prints root-shell
commands using the actual installed module path:

- `action.sh on`: enable the saved preference and start or retry the guarded worker.
- `action.sh off`: disable automatic application, stop its worker and restore code.
- `action.sh status`: print raw diagnostic state.

These explicit commands remain available without changing the Action button's
status-only behavior. Interrupted activations and health failures remain blocked
until reviewed. Merely toggling the saved checkbox does not clear a pending lease.
If restoration fails, leave the module disabled and restart the affected app or
fully reboot the headset. A checked module may apply again after startup.
