# Galaxy XR Framebuffer10 - corrected camera color

## 0.5.1-experimental (2026-10-20)

- Correct premultiplied output encoding in all 128 node/QUAD variants, for every
  application: encode unassociated RGB and reapply alpha. Preserve opaque output,
  alpha, zero-alpha emissive behaviour and all original precision decorations.
  Arithmetic regression checks verify that a same-colour overlay no longer
  brightens its background. Visual confirmation is pending.
- Rebuilt under the same unreleased version by owner request (2026-10-20).
  Previous mask-only ZIP retained locally; use SHA-256 to distinguish builds.
- Fix 544 precompiled variants whose output-encode flag selected byte-identical
  non-encoding code: node, cube, equirect, distortionmesh_depth and depth_lsr.
  Encode final RGB once; preserve alpha, original arithmetic/precision decorations,
  resource reflection and all unflagged variants. Strip only internal member debug
  labels as needed for the fixed-size slots. All 544 pass SPIR-V validation.

- Move output sRGB encoding after hand-mask highlights and segmentation breakthrough
  attenuation in both distortionmesh uber shaders. Preserve alpha and stock flag-off
  behaviour. This fixes two additional slots; cross-layer blending is unchanged.
- Offline SPIR-V validation and canonical flag-off equivalence pass for both shaders.
  Visual validation on the headset is pending.

## 0.5.0-experimental (2026-10-19)

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
