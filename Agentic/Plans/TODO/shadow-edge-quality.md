# Shadow Edge Quality

Date: 2026-07-10 (reconciled)
Status: Planned — 0/5 phases complete; initial inventory is preparation, not
implementation completion
Impact area: DX12 renderer, shaders, shadow configuration, visual baselines
Owner: shadow rendering

## Problem And Target

Player/object shadows show visible block steps. Terrain receives only the broad
shadow frame and current shaders use a square point-sampled PCF kernel. Target:
readable caster silhouettes, soft stable edges, no acne/peter-panning, and no
crawling during orbit/zoom.

## Dependencies

- Complete `dx12-failure-propagation.md` command-state work before root-signature
  or resource-lifetime changes.
- Coordinate binding ownership with `render-backend-decomposition.md`.
- Use the CPU/full umbrella once `validation-gate-integrity.md` lands.

## Phases

- [ ] S0. Commit objective baseline scenes/captures: silhouette, terrain
  receiver, and motion stress, including depth previews and settings.
- [ ] S1. Feed the tight object map into terrain receivers through an explicit
  second binding; do not steal the object-material slot. Clear disabled bindings
  and name lifetime in shadow resources.
- [ ] S2. Replace square PCF with stable 12–16 tap Poisson filtering. Consider
  comparison sampling only after confirming format/root-signature compatibility.
- [ ] S3. Add texel snapping, retune bias, and measured High/Ultra presets only
  after filter stability and GPU cost are proven.
- [ ] S4. Decide whether cascades/clipmaps remain necessary; implementation is a
  new plan only if S1-S3 cannot meet the broad-terrain acceptance scenes.

## Acceptance

- No visible stair steps at normal gameplay distance.
- Silhouette shape survives soft filtering.
- No crawling under the committed motion sequence.
- No new acne/peter-panning in committed scenes.
- GPU cost stays within an explicitly recorded perf budget.

## Validation

Renderer gate per slice; DX12 architecture tests for binding changes; perf gate
when sample count/hot shader work changes; intentional baselines only after
human visual acceptance of the target scenes.
