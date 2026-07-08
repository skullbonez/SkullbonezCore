# Shadow Edge Quality - Progress

## Status

Plan drafted. No source or shader implementation has started.

## Checklist

| ID | Item | Status | Notes |
|----|------|--------|-------|
| `P0.1` | Inspect current shadow-map generation and receiver sampling | Done | Found broad terrain map, tight object map, and square point-sampled PCF. |
| `P0.2` | Write implementation plan | Done | See `Agentic/Plans/In_Progress/shadow-edge-quality-plan.md`. |
| `P0.3` | Pick acceptance scenes and capture current artifacts | Pending | Needs current game capture plus shadow-depth previews. |
| `P1.1` | Thread tight object shadow data into terrain receiver inputs | Pending | Must preserve broad terrain map behavior and descriptor hygiene. |
| `P1.2` | Add second terrain shadow binding or atlas path | Pending | Do not reuse t4; it is the object material table slot. |
| `P1.3` | Combine broad and tight visibility in terrain shader | Pending | Terrain should use the darker valid visibility result. |
| `P2.1` | Replace square PCF with stable Poisson PCF | Pending | Keep terrain and instanced object shader implementations equivalent. |
| `P2.2` | Evaluate contact-hardening PCSS | Pending | Only after Poisson PCF is stable and affordable. |
| `P3.1` | Add light-space texel snapping for tight maps | Pending | Reduces crawling during camera orbit/zoom. |
| `P3.2` | Add/tune quality presets | Pending | High/Ultra presets should be explicit scene/config choices. |
| `P4.1` | Decide whether cascades are needed | Pending | Depends on S1-S3 visual evidence. |
| `P5.1` | Run PR-gate validation for implementation | Pending | Expected: `tools\validate_dx12_renderer.bat`; add `tools\validate_perf.bat` if sample cost rises. |

## Investigation Notes

- `ShadowPass::BuildTerrainFrameData` centers the broad terrain shadow map over
  terrain bounds, making screenshots stable but spreading texels over a large
  area.
- `ShadowPass::BuildObjectFrameData` already builds a tighter nearby-object map
  centered on the render look target.
- `TerrainPass::Render` currently receives one `ShadowFrameData` pointer, so
  terrain cannot combine broad terrain shadows with the tight object map yet.
- `lit_textured.hlsl` and `lit_textured_instanced.hlsl` both use the same
  square manual PCF shape. Any filter change should keep them visually aligned.
- DX12 currently exposes a point-clamp shadow sampler for manual PCF. Hardware
  comparison sampling is an option, but must be checked against the existing
  depth SRV format and root signature.

## Validation State

No validation has been run for this documentation-only planning update.
Implementation will require DX12 renderer validation before commit/PR.

## Open Questions

- Should terrain keep receiving broad terrain self-shadows, or should the first
  slice prioritize tight object-on-terrain shadows even if broad self-shadowing
  remains unchanged?
- Is the target primarily gameplay quality, screenshot/cinematic quality, or a
  selectable quality tier with different performance budgets?
- Are we willing to expand the texture-slot ABI beyond t0..t4 now, or should
  the first implementation use an atlas to avoid a root-signature expansion?
