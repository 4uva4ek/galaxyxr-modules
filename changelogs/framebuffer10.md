# Galaxy XR Framebuffer10 - corrected camera color

## 0.5.0-experimental (unreleased)

- Revived on 2026-10-19 by user decision; the 2026-10-13 deprecation is withdrawn.
- No vendor code in the package: the colour library is generated on the headset from
  its own stock `libvulkan_compositor.so` with a project edit script
  (`native/payload_patch.inc`), stock and result SHA-256 verified. The edit script and
  an equivalent PC build come from `payload-recipe/make_payload.py`.
- Colour: seven compositor shaders that ignored the framebuffer output-encode flag now
  encode sRGB correctly: camera passthrough (constants/uniforms), projection+camera,
  two projections+camera, AMRP reprojection (constants/uniforms) and two projections
  (quad-view inset). Fixes dark, oversaturated output and the Steam Link inset square
  and halo on 10-bit framebuffers.
- Checkbox changes apply at the KernelSU soft reboot; unchecking ends the session as a
  planned stop instead of a failure. `action.sh off` restores immediately.
- Restart hooks wait by elapsed time below KernelSU's per-hook limits.
- Guardian steady state uses only cheap checks (no periodic `dumpsys`, no fork).
- Requires KernelSU GXR 1.0.6 or later. Validated on the owner's headset.

## Earlier local versions

- 0.3.1-experimental: coordinated restart hooks with phase tracing; deprecated on
  2026-10-13 and never published.
- Module author set to `4uva4ek`; per-module update metadata standardized.
