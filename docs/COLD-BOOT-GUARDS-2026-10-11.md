# Interruption guards follow the kernel boot

The user requested that all module interruption guards reset on cold boot,
without changing KernelSU checkbox preferences. A clean enabled module must
not disable itself just because its previous RAM session ended at power-off.

All six automatic modules now call `cold-boot-reset.sh` before their startup
guard checks. Before the first activation it records the kernel UUID from
`/proc/sys/kernel/random/boot_id` in `guard-boot-id`. A different valid UUID
expires the previous interruption evidence: Haptics/Extrapolation pending and
failure files, Velocity pending, Whitening/Timestamp leases and failures, and
Framebuffer session lease and failure. Kernel-owned `/dev` state already
disappears on a cold boot.

The reset never creates or removes `disable` or `remove`. Thus ON stays ON,
OFF stays OFF, and a module that disabled itself after an actual error is not
silently re-enabled. Standard KernelSU skips OFF modules: their stale guards
are physically cleared only when their next startup entry is intentionally
reached. There is no new early-boot hook or KernelSU change. On this headset,
startup still follows temporary root and the standard service-stage workflow.

Soft reboot keeps the same kernel UUID and cannot reset guards. Repeated
startup calls in the same boot preserve newly created failures. A tmpfs lock
serializes reset; invalid/missing boot provenance with existing legacy evidence,
unsafe symlinks, and unexpected lease-directory contents stop reset. An empty
fresh replacement records its first boot before any activation. Old untagged
evidence must not be copied into new packages; deploy a fresh disabled directory
only after the normal verified restoration and backup procedure.

This change expires interruption/retry guards, not integrity, compatibility,
ownership or restoration checks. No native implementation or shader was changed.
Action remains an English status report and explains cold/soft reboot behavior.

Versions: Haptics 0.5.4; Extrapolation 0.4.2; Velocity 0.3.3;
Whitening and Timestamp 0.1.2; Framebuffer 0.2.2-experimental.
All packages are automatic and contain `disable` at installation.

The initiating read-only audit found Haptics 0.5.3 STOCK with an old pending
marker and `Previous activation did not complete`. Whitening/Timestamp retained
historical interrupted-lease failures but were already active at inspection.
Framebuffer retained a lease from a different kernel boot and was disabled.
Velocity's worker was running; Extrapolation's disable marker predated this
incident. Original Haptics interruption timing/cause was not recovered.

Evidence and local validation receipts:
`E:/GalaxyXR/diagnostics/module-lifecycle-20261011/recurrence-audit.json` and
`cold-reset-validation/`. After the explicit user request, all six replacements were installed disabled.
Active old patches were restored and workers stopped first; old directories
were preserved under `/data/adb/gxr-module-backups/cold-reset-20261011-054-033`.
Installed checksums and OFF Action reports passed. Native status confirms stock,
and Framebuffer preflight confirms the 8-bit baseline. Boot, ksud and compositor
PID remained unchanged. No enablement, reboot or publication was performed.
Real cold/soft boot validation remains pending; the user will enable and test.
Deployment receipt: `diagnostics/module-lifecycle-20261011/cold-reset-deployment/receipt.json`.
