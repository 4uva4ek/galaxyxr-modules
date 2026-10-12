# Installing public GXR modules

The five public module packages include an independent stable `updateJson` feed.
The repository's `modules.json` makes them discoverable as one source.

## Manager with the reviewed GXR installer

The source-enabled GXR Manager preview (1.0.6-updates-preview or a build containing
that implementation) supports both repository selection and a local module ZIP:

1. Open Modules, then the repository icon and Sources.
2. Add `https://github.com/4uva4ek/galaxyxr-modules` if it is not already listed.
3. Select the required modules and choose Install selected. Existing/current
   versions are marked Installed; newer versions are offered as updates.
4. The Manager downloads and checks all selected packages first. Installation
   then runs sequentially, stopping on error; it is not an atomic batch.

GXR installation validates package identity and checksums, disables and restores
an existing installation, and verifies runtime status before replacement. Extra
configuration and same-boot guards are preserved. The replacement stays DISABLED;
the previous directory is retained outside module discovery under
`/data/adb/gxr-updates/`. A failed restoration blocks replacement. An interrupted
directory swap can leave a module absent with its disabled old copy in the
reported transaction; inspect the backup before recovery.

This path does not execute downloaded installer scripts, regenerate startup/root
metadata or modify original system files. The package's `customize.sh` refusal is
retained to prevent older installers from taking their unsafe standard path.
KernelSU versions without this GXR installer cannot install these ZIPs normally;
use the module's documented reviewed direct-deployment procedure in that case.

Installation persists across reboot but does not authorize enablement or a soft
restart. Enable only as a separate decision after verification, then follow the
documented KernelSU service-stage workflow. Action reports status and recovery.
Checking a module is a preference, not proof of a live patch.

The preview was built and installed during local validation. Live replacement
through its new updater has not yet been tested on the headset; host tests are
not evidence that every historical installation can be restored. Do not mistake
successful package download or catalogue loading for live update validation.

## Other sources

Add a public GitHub repository URL or `owner/repository`. A `modules.json`
catalogue is preferred; without one the Manager offers stable Release ZIP assets.
Arbitrary source snapshots are not installable module packages. Private repository
authentication is not implemented. Install only packages from trusted publishers;
checksums detect corruption but do not authenticate a compromised publisher.
Non-GXR modules retain their normal KernelSU installation path.
