# KernelSU update catalogue

Each public module has an independent stable feed in `updates/NAME.json`.
`module.prop` points to it with `updateJson`. The JSON describes the **published
ZIP**, not the current source checkout: unpublished development versions never
advance a feed. `versionCode` must increase independently for each module.

The initial catalogue was verified against the five public release ZIPs and their
SHA-256 sidecars on 2026-10-12. Framebuffer10 remains source-only. New local
experiments, including HAL Cache Bypass and OpenXR Quality, are not implicitly
approved for distribution; add them to the explicit allowlist only after their
release and safe update lifecycle have been reviewed.

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
