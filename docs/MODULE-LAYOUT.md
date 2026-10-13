# Module source layout

Module author: **4uva4ek**. Use this author in every project module's metadata
and documentation. Preserve third-party and vendor attribution and notices.

This layout applies to public modules, local experiments and new modules.
It describes local sources; preparing metadata does not publish or install them.

```text
modules/NAME/
  README.md              Purpose, author, build command, safety and validation
  native/                Module implementation, when applicable
  module/
    module.prop          Stable ID, version, author, description, updateJson
    ...                  Reviewed lifecycle scripts and payload sources
  build.json or build.py Documented build entry point, shared or dedicated
changelogs/NAME.md        Released changes and a separate Unreleased section
dist/NAME/               Ignored build outputs; never source or published feeds
```

Start metadata from [module.prop.example](../templates/module.prop.example).
Replace NAME, MODULE_ID and the descriptive placeholders. Keep the module ID
stable across updates. Use UTF-8 without BOM and LF line endings. Builders must
copy the source metadata, including author and updateJson, into the package.

Every module reserves its own canonical URL:
`https://raw.githubusercontent.com/4uva4ek/galaxyxr-modules/main/updates/NAME.json`.
For an unpublished or retired module this is only a reserved address: it may
return 404 and does not mean updates are available. Do not create fake feed JSON,
download links or hashes to make an unfinished channel look live.

`modules.json` and `updates/NAME.json` describe reviewed public distribution.
They are separate from the local source inventory. Add a module to public
distribution only after explicit publication authorization and lifecycle review.
Prereleases do not advance the stable feed. Retired modules and local vendor
payloads remain excluded. See [UPDATES.md](UPDATES.md).

Record local metadata edits as Unreleased. Before creating a replacement release,
increase its version and versionCode; never overwrite an existing release ZIP.
Changing sources does not update old ZIPs or their validation receipts. Rebuild
and validate the next artifact before deployment or publication.

Run from the repository root before building or publishing:

```sh
python tools/check_module_layout.py
python tools/check_catalogue.py
python tools/check_module_contract.py
```

The first check includes local and ignored experimental module folders. The
second also validates the published catalogue and feeds. The third verifies the
package and report contract of every tracked module (markers, lifecycle scripts,
shell syntax, ENABLED/DISABLED labels, a removal path that does not need Android
services) and builds shell-only modules; the `Check module contract` workflow
runs all three on every branch and pull request. None accesses a headset or
network. Keep automatic-only, disabled packaging, English Action
status/recovery reporting and the existing safety and rollback guards. Never
copy activation scripts from a template without reviewing the new module's scope.
