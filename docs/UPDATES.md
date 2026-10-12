# KernelSU update catalogue

In the update-aware Manager, open the module repository and choose **Sources**.
Paste `https://github.com/4uva4ek/galaxyxr-modules`, select the desired modules,
then choose **Install selected**. The Galaxy XR source is included by default.
Repository selection is saved in the Manager's app preferences.

The root `modules.json` (schemaVersion 1) lists `id`, `name`, `description` and
`updateJson` for each public module. Its feeds provide the current download URLs,
versions and hashes. Other public GitHub repositories can publish the same
catalogue. Without it, the Manager lists ZIP assets from their latest stable
release per `module/vVERSION` tag line (or the latest stable release for ordinary
version tags). Private repositories/authentication are not supported.

Selected downloads are checked as module ZIPs before the installation batch
starts. Catalogue identity/version and any provided SHA-256 must match; duplicate
module IDs and already-installed/newer versions are rejected. A download failure
does not start a partial batch. Installation itself is sequential, not an atomic
transaction across several modules; a failure can leave earlier modules installed.

Each public module has an independent stable feed in `updates/NAME.json`.
`module.prop` points to it with `updateJson`. The JSON describes the **published
ZIP**, not the current source checkout: unpublished development versions never
advance a feed. `versionCode` must increase independently for each module.

The initial catalogue was verified against the five public release ZIPs and their
SHA-256 sidecars on 2026-10-12. Framebuffer10 remains source-only. New local
experiments are not implicitly approved for distribution; add them to the
explicit allowlist only after their release and safe update lifecycle have been
reviewed. HAL Cache Bypass 0.1.0 is separately published as an experimental
prerelease, but is not in this catalogue: the current GXR Manager installer
does not support its ID. OpenXR Quality remains unpublished.

## Release process

1. Build and validate the module locally as described in RELEASING.md. Keep the
   disabled marker and all compatibility, restoration and failure guards.
2. Update `changelogs/NAME.md`, then publish the reviewed `NAME/vVERSION` release
   with its `gxr-NAME-VERSION-auto.zip` and `.zip.sha256` assets.
3. `update-feed.yml` downloads the published assets, checks identity, version,
   archive paths, disabled state and every payload checksum, then commits the
   feed to `main`. It never runs package scripts. Drafts and prereleases do not
   advance stable. A failed publication job leaves the previous feed intact.
4. The workflow may also be dispatched for an existing reviewed stable tag.
   GitHub Actions needs permission to write contents to `main`; branch protection
   must permit the bot or promotion must be committed through the normal review
   process. Failed pushes do not publish partial feeds.

Local equivalent:

```sh
python tools/update_feed.py --tag haptic-main/v0.5.5 --zip /path/to/gxr-haptic-main-0.5.5-auto.zip
python tools/test_update_feed.py
```

Keep published release assets immutable. Do not replace a ZIP under an existing
version: create a higher versionCode. SHA-256 detects corruption; it is not a
publisher signature. A repository or release-account compromise remains outside
this integrity check.

## Headset installation

The update-aware KernelSU GXR Manager checks these feeds even when the module is
OFF. It also knows the feed IDs of older public modules without `updateJson`.
Checking is performed when the module list requests an update check; this is not
a background auto-install service or a catalogue of uninstalled modules.

Use the corresponding update-aware GXR build. Older Managers can discover the
feed but cannot install these packages through their ordinary installer.
Keep `customize.sh`'s refusal in place for those installers.

The new GXR installer path verifies the ZIP, stops/restores the installed module,
checks runtime status, keeps an inert backup under `/data/adb/gxr-updates`, and
installs the replacement **DISABLED**. Same-boot failure guards are copied, not
cleared. It never runs the downloaded install scripts or rewrites preinit/root
metadata. Enabling and soft restart remain separate deliberate operations.

If restoration or verification fails, the old module remains disabled and the
replacement is not committed. Use Action to inspect recovery instructions. An
interruption between directory renames can leave the module absent; the disabled
old directory remains in the reported transaction's `old/`. Nothing in the
backup directory starts automatically. Recovery requires inspecting that
transaction, not automatically retrying it at boot.

Local checks and an APK build do not establish on-headset update correctness.
First live update testing and installation of the new Manager require a separate
device deployment request. No headset changes are part of catalogue preparation.
