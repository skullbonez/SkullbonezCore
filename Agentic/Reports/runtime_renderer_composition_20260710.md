# RuntimeRenderer Composition Ownership Evidence

Date: 2026-07-10
Branch: `engine-cleanup-10th-july`
Owning plan: `Agentic/Plans/TODO/runtime-shell-decomposition.md` A1-A2

## Boundary completed

`RuntimeRenderer` now owns render-pass order, pass instances, resource lifecycle
logging, backend release/rebuild sequencing, overlay submission, and texture
selection through the typed render frame. The old `RuntimeRendererBindings`,
render lifecycle/editor C hooks, `void*` callback user, and callback access to
`Run` private state are deleted.

The constructor receives the five named owner views:
`RenderWorldView`, `RenderSceneView`, `RenderReplayOverlayView`,
`RenderToolOverlayView`, and `RenderUiView`, plus the active backend
capability view. `RenderWorldView` names concrete assets, textures, cameras,
terrain, sky, window, pass resources, render policy, world, visualizers, and
diagnostics; it does not carry `RunSubsystemState` or another host bag.

`RuntimeTools::PrepareOverlayTrace` and
`ReplayRuntime::AppendOverlayTrace` own their bounded draw-record production.
`RuntimeRenderer::RenderFrameEntry` sequences those owners after model
preparation and replay visual overrides and before submission. Texture handle
lookup/selection is direct through `TextureCollection` in the typed frame; no
pass calls `Run` for a handle.

The former `RunPasses.cpp` and render-owner portion of `RunRender.cpp` are
now `Runtime/Render/RuntimeRenderPasses.cpp` and
`Runtime/Render/RuntimeRenderer.cpp`. The remaining 177-line
`RunRender.cpp` performs camera selection and one top-level renderer call.
The extracted files remain large because each is cohesive pass implementation
or pass-graph sequencing; their named next decomposition owner is
`Agentic/Plans/TODO/render-backend-decomposition.md`.

## Deletion proof

- `Run.cpp` and `Run.h` contain no render lifecycle/editor hook, callback
  user pointer, or `BuildRuntimeRendererBindings`.
- `Runtime/Render` contains no `Run*`, `Run&`, `RunSubsystemState`,
  `RuntimeRendererBindings`, `RenderRuntimeView`, or
  `RenderDiagnosticsView`.
- Renderer teardown borrows the unique sky owner directly; the obsolete raw
  `RunSubsystemState::skyBox` alias is deleted.
- Render-pass texture lookup uses `SelectTexture`/`GetTextureHandle`
  directly from the typed texture owner. No texture callback remains.
- Project and filter files register both extracted translation units under
  `Source Files\\Runtime\\Render`.

## Adversarial review

The first deliberately separate read-only pass found two blocking defects:

1. Overlay construction had moved before backend readiness/model preparation and
   replay launcher/model overrides, which could build scrub tracer records from
   live rather than recorded launcher state.
2. `RenderWorldView` carried the broad `RunSubsystemState` bag, recreating
   the old host surface under a new name.

Both were corrected. Owner record builders now run in the original
post-override slot, and RuntimeRenderer stores only explicit render/world owner
references. The required repeat adversarial pass found no remaining blocking or
non-blocking ownership defect. Internal typed RenderGraph callback payloads
remain renderer-local under their existing render-graph deletion condition;
none can access `Run`.

## Validation

The final source and allocation metadata passed:

- Profile x64 focused build: 12.3s, zero warnings/errors.
- Project/filter validation: 0 errors across 590 production project/filter
  items.
- `tools\\validate_dx12_arch_tests.bat`: 4.5s, passed.
- `tools\\validate_dx12_renderer.bat`: 42.6s, zero InfoQueue errors and all
  three committed screenshot baselines matched.
- `tools\\validate_full.bat`: 52.6s; doctest 121/121 and 2,640/2,640
  assertions, all CPU lanes, zero-warning builds, DX12 baselines, standalone
  physics smoke, and the 20,001-line byte-exact physics baseline passed.
- `tools\\validate_fast.bat`: 19.2s after moving the existing allocation
  allowlist rows to the extracted files.
- Allocation checker self-test plus repository audit: 7.3s, 303 files scanned,
  zero allowlist errors.

The first repository allocation audit intentionally failed on stale allowlist
paths for the two moved files. The rows were transferred without adding an
exception or changing capacity/phase semantics, then the required fast and
allocation gates passed.

## Comment audit

The touched-file comment-style audit covered 13 source-bearing files with
13 checked, 0 deferred, and no unchecked files. The owning checklist/evidence
path is this report plus A1-A2 in
`Agentic/Plans/TODO/runtime-shell-decomposition.md`.
