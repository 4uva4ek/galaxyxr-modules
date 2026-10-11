# Automatic startup and recovery

Only automatic ZIPs are published. Each ships with a `disable` marker and must
remain disabled after installation or replacement until separately enabled.
Disable and restore an existing module before replacing its directory. The normal
installer is blocked because the tested Samsung installer can modify metadata
startup files; follow the reviewed deployment instructions in each module README.

The KernelSU checkbox is the only saved activation preference. Standard
`service.sh` starts an enabled module's worker, which waits for Android boot and a
supported target before guarded activation. No additional root-ready or early-boot
hook is installed. In the documented KernelSU GXR 1.0 flow, perform its soft reboot
after root acquisition to dispatch the normal service stage. The modules themselves
do not acquire root, request a soft reboot or modify root startup integration.

Checking an idle module does not launch its worker immediately. Unchecking a
running module makes its worker attempt restoration within its polling cycle.
Use Action to distinguish the saved preference from actual patch state.
Action reports status and recovery commands; it never starts or stops a patch.

Explicit recovery commands are `sh /data/adb/modules/MODULE_ID/action.sh on` to
enable and start/retry, and `action.sh off` to disable, stop and restore. Investigate
the reported failure before retrying. These commands are separate from the normal
Action button and do not depend on a Manager checkbox extension.

Failure containment remains part of automatic startup:

- Haptic/extrapolation keep a pending marker until the same target survives the
  observation window and passes verification. A service restart or early failure
  disables automatic retry. Their legacy `autostart/enabled` file is a compatibility
  mirror, not a separate preference.
- OpenXR Velocity Frame retains pending activation and per-app identity checks.
  Unknown or partial code and unsupported processes are rejected.
- Whitening/timestamp retain an active lease throughout the owned patch session.
  An interrupted lease quarantines a subsequent automatic start, including after
  reboot. The native guardian and ownership checks remain in force.

Disable before using a full reboot as rollback: reboot clears private memory, but
an enabled clean module can apply again at a later service stage. If restoration
fails, leave disabled and restart the target app or fully reboot. Do not force-kill
a patch operation. Runtime logs and locks use `/dev` tmpfs; module preferences,
failure records and leases persist in the module directory.

The modules do not write original system files, firmware, SELinux configuration or
metadata startup files. KernelSU's own installer and startup operations are
separate. Current Velocity Frame and Extrapolation releases support each other's
exact guarded patch states; unknown older hooks still require restoration first.
