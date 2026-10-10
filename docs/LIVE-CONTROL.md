# Opt-in KernelSU live checkbox control

Requires KernelSU GXR live-control Manager. Our modules declare
`gxrLiveControl=1` in module.prop and provide `live-control.sh enable|disable`.
The Manager's embedded ksud invokes this hook when its checkbox changes.
Modules without the flag keep the existing KernelSU path. Action is unchanged
and invokes the module's status report; no extra button is added.

Enable starts guarded activation immediately. Automatic variants wait for an
absent target and apply once it loads. Disable stops the worker and restores live
patches. Failure leaves the disable marker present; use Action for diagnostics,
resolve the cause and enable again. The Manager list may need refreshing for
failures detected by background health checks. A checkbox is permission to run;
the report confirms actual runtime state separately.

The hook path is validated, execution is bounded to 45 seconds, and these
memory-only modules skip metadata/preinit regeneration on toggles. All normal
firmware/process guards remain. No kernel, system library or root acquisition
change is part of this integration. Reboot is not rollback while auto is enabled.

Manual variants have no startup worker; after a process exit they require another
explicit activation. Automatic variants use the existing KernelSU service stage.
If temporary-root startup skips that stage, toggle the module off/on after root
is available. No extra boot-persistence mechanism is installed.

Older Managers ignore the opt-in flag; their checkboxes alone cannot start an idle
worker. Explicit `action.sh on|off` commands remain available as a fallback.
