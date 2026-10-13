# Automatic module releases

The **Build and release modules** workflow builds and publishes an approved
module when a push to `main` raises both fields in its source metadata:

```properties
# modules/NAME/module/module.prop
version=0.1.8
versionCode=9
```

The example follows Whitening 0.1.7 / code 8. Both values must increase;
changing only one fails the job. Update the module changelog and relevant
validation notes in the same push. Pushing a version bump to main is the
release decision, so complete any required manual/device validation first.
Source/description changes with unchanged versions do not publish anything.

Only modules already approved in `modules.json` are eligible. Deprecated,
source-only and local experiments are excluded. Adding a new module still
requires explicit distribution authorization and lifecycle/installer review.

## Pipeline

1. Compare the pushed `module.prop` files against the previous commit; select
   approved increases that have not already reached their stable feed.
2. Install Android NDK **28.2.13676358**, build from the pushed commit and run
   layout, package, hash, shell and relevant native/contract checks.
3. Upload only disabled automatic ZIPs, SHA-256 sidecars and the build manifest.
4. A separate job with repository write permission creates version tags and
   draft releases, verifies uploaded bytes, then publishes them. Existing tags
   and assets cannot be overwritten; a conflicting version fails closed.
5. Advance the update feeds after verifying the published archives. Feed jobs
   are serialized; a concurrent source push is rebased without force-pushing.

The publication job updates feeds itself because releases created with
`GITHUB_TOKEN` do not trigger another `release` workflow. Manual releases still
use the existing `update-feed.yml` workflow.

## Private build inputs

Extrapolation, Velocity Frame and Pose Time need the pinned OpenXR runtime;
Pose Time also needs the pinned Steam Link scene library. Compressed, encoded
inputs are stored once as repository Actions secrets, in numbered chunks:
`GXR_RUNTIME_01` through `27` and `GXR_SCENE_01` through `24`. They are restored
only for the selected modules, verified against the original full-file SHA-256,
and removed from the ephemeral runner. They are never source commits, logs,
public release assets or uploaded build artifacts. The build job has read-only
repository permission; the publication job has no input secrets.

Missing or mismatching inputs stop the build. Updating firmware support still
requires fresh analysis and precise compatibility checks; the workflow never
downloads arbitrary replacement libraries or weakens a hash guard. Input secrets
are available only to trusted main-branch builds, never to fork pull requests.

## Manual verification and recovery

Use **Actions → Build and release modules → Run workflow**, select `main`, and
choose a module or `all`. Leave **publish** unchecked for build/test-only runs.
This does not create tags, releases or feed changes. Enable publish only to
recover a reviewed pending version bump. Rerunning the original failed run can
resume a matching draft; mismatching tags/assets are retained and require review.

No workflow connects to a headset, enables modules or reboots it. Automatic
packages still install disabled. CI success verifies software/package checks;
it does not replace physical testing, establish tracking quality or validate
native throwing. Existing unpublished QUALITY 0.1.0 still needs the migration
described in its module README.
