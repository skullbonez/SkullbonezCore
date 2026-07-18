# DX12 Backend D6 Comment Audit

Date: 2026-07-18

Scope: D6 composition-root shrink and header diet

Guide: `Agentic/Reference/comment-style-guide.md`

Skill: `Agentic/Skills/comment-style-audit/skill.md`

## Reconciled Source Inventory

The inventory is the complete source-bearing set reported by
`git diff --name-only` after the D6 implementation. Twelve files are in scope:

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`

Checked: 12 / 12. Deferred: 0. Unchecked: 0.

## Findings

- Every touched file retains its complete Purpose, Summary, Glossary,
  Invariants, and Related learning header. The device header and implementation
  now describe the complete presentation epoch rather than the older partial-
  extraction boundary.
- `Dx12RenderDevice` locally teaches that depth creation prepares an unpublished
  candidate, replacement transfers its COM reference, and extent generation
  advances only after complete publication. The shutdown order keeps the depth
  surface inside the device epoch.
- `Dx12PipelineOwner` identifies clear color and depth as desired output state
  paired with its current render-target recipe. Backend setters no longer hide
  that state in the composition root.
- `Dx12RaytracingOwner` records the single-live-handle invariant and transfers
  the registry handle out before resource shutdown, so the texture registry
  cannot retain a stale reflection identity.
- The upload-capacity comment remains next to `Dx12FrameOwner::FRAME_COUNT` and
  states both the 64 MiB two-frame reservation and the no-growth overflow rule.
- Backend lifecycle and resize sites retain nearby publication, fence, device-
  loss, and terminal-drain comments. Mechanical direct-owner substitutions in
  dynamic geometry, readback, resources, profiler, and texture units introduce
  no new policy, lifetime, units, or hazard requiring additional prose.
- Deleted wrappers carried no unique invariant after their operations moved to
  the device, frame, pipeline, texture, descriptor, or raytracing owner.

## Result

PASS. Every touched source-bearing file was inspected against the guide, the
required local teaching content is present, and no file is deferred.
