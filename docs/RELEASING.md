# Independent module releases

1. Update `version` and `versionCode` in the selected module's `module.prop`.
2. Update its README and compatibility/validation record.
3. Build from a clean Git checkout using `python tools/build.py NAME --ndk PATH`.
4. Run package checks and the required device checks. Distinguish build success,
   software activation/rollback, and user confirmation of the physical effect.
5. Create the tag `NAME/vVERSION` on the verified commit.
6. Create a GitHub Release for that tag and attach both `-manual.zip` and
   `-auto.zip`, each with its `.zip.sha256` file, from `dist/NAME/`.
   Keep the previous releases available. The two variants share one module ID
   and must not be installed together.

Do not commit build artifacts, APKs, dumps, signing keys, or device data.
Keep manual and automatic activation explicit in package names and documentation.
Automatic variants use standard KernelSU hooks and must retain failure latching;
do not alter root acquisition or system partitions. Supporting new firmware
requires fresh analysis and precise compatibility checks; do not remove existing
hash checks to accept an unknown build.
Releases are published manually. A push does not automatically publish a release.
