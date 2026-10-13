# CI module builds

`.github/workflows/build-modules.yml` builds each module whose own files, or the
shared `tools/` files its package embeds, changed in a push or pull request.
No module files changed: no build jobs. `workflow_dispatch` builds every
buildable module.

Buildable: haptic-main, controller-extrapolation, controller-timestamp-precision,
controller-whitening, hal-cache-bypass, ota-control.

Never built by CI, reported with the reason in the run summary:

- framebuffer10 - deprecated, pinned vendor payload not in the repository.
- openxr-velocity-frame, steamlink-pose-time - need user-supplied proprietary
  runtime/scene libraries; RELEASING.md forbids publishing vendor bytes.

Each build job installs Android NDK 28.2.13676358, runs the module's documented
build command, verifies the ZIP (`tools/verify_built.py`, plus the module's own
`tests/verify_package.py` when present), and uploads `<artifact>.zip` with its
`.zip.sha256` as a job artifact. Artifacts are build previews only; releases
stay manual per RELEASING.md.
