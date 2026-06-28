# Carmack Global Service Lifetime Plan

Date: 2026-06-28
Status: In progress
Impact area: runtime ownership, rendering, assets, input/window, scene system,
tooling, tests
Validation note: plan-only edits require no validation. PR-bound implementation
should choose the narrowest gate from `AGENTS.md`; broad lifetime changes usually
require `tools\validate_full.bat`.

## Completed Slices

- [x] 2026-06-28: Routed authored and generated scene camera setup through the
  runtime-owned camera service passed in the scene setup context instead of
  reacquiring `CameraCollection::Instance()`. `SceneAuthoredSetup` and
  `SceneGeneratedSetup` now assert that their borrowed camera pointer is present
  before mutating scene cameras, preserving the existing camera insertion and
  terrain-bound behavior while making scene load ownership explicit. The
  runtime-boundary ratchet removed the `SceneAuthoredSetup.cpp` and
  `SceneGeneratedSetup.cpp` `CameraCollection::Instance()` allowlist rows,
  leaving only camera bootstrap/owner definitions as counted singleton debt.
  Rubber-duck review intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed in 4.82s
  (`TestOutput\validation\agent_logs\camera_scene_context_runtime_boundaries.log`),
  `tools\validate_format.bat` passed in 7.31s
  (`TestOutput\validation\agent_logs\camera_scene_context_validate_format.log`),
  `tools\validate_fast.bat` passed in 256.23s
  (`TestOutput\validation\agent_logs\camera_scene_context_validate_fast.log`),
  `tools\validate_full.bat` passed in 30.13s
  (`TestOutput\validation\agent_logs\camera_scene_context_validate_full.log`),
  and `git diff --check` passed.
- [x] 2026-06-28: Added a narrow HWND-bound lifecycle for Win32 callback-fed
  input accumulators. `Input::BindCallbackBridge()` arms wheel/raw mouse queues
  for the active `HWND`, WndProc passes the originating `HWND` into accumulator
  calls, `Input::UnbindCallbackBridge()` clears queued state during cleanup
  before backend/window class teardown, and Debug assertions catch double-bind,
  unbound unbind, wrong-window unbind, and raw-input registration before the
  bridge is bound. Rubber-duck review by Mencius found two blocking lifecycle
  assertion/gating holes, one non-blocking startup-order check, and one blocking
  stale-plan completion issue; all were fixed. Runtime input/window behavior
  change; final `tools\validate_full.bat` passed in 140.80s and focused
  `tools\validate_interaction_clicks.bat` passed in 5.74s before commit.
- [x] 2026-06-28: Documented the Win32 input callback bridge accumulators in
  `Input.cpp` without changing behavior. The file now separates callback-fed
  wheel/raw mouse queues from cursor policy and scripted automation override
  state, names `WndProc` as the callback writer, records the UI and camera
  consumers/reset paths, and leaves bind/unbind lifecycle work open. Rubber-duck
  review by Kepler found two blocking stale/incorrect comment claims and two
  non-blocking wording issues; all were fixed. Comment-only source/documentation
  slice; no repository validation required.
- [x] 2026-06-28: Added debug assertions for `EngineContext` borrowed runtime
  bindings before they are used. `EngineContext::Bind()` now asserts that the
  complete Run-owned service graph is present, both `Bindings()` accessors
  assert before handing out borrowed pointers, and
  `RuntimeViewModelBuilder::Build()` asserts before its release-safe default
  fallback can mask an unbound context. Rubber-duck review by Faraday found one
  blocking bypass through the view-model builder and one non-blocking startup
  false-positive check; the bypass was fixed before validation. Final
  `tools\validate_full.bat` passed with runtime boundaries clean, Profile/Debug
  builds at 0 warnings/errors, DX12 InfoQueue errors at 0 with matching
  screenshots, and byte-exact `physics_regression_solver.csv`.
- [x] 2026-06-28: Documented the current startup bind order and shutdown/backend
  resource release order. The lifetime-order section now records how `Init.cpp`
  owns worker/window/backend creation, how `Run::Run()` binds `EngineContext`,
  how `Run::Initialise()` binds window/renderer/title, texture and active asset
  bridges, built-in assets, terrain, skybox, UI text, cameras, and first scene
  load, and how `Run::~Run` flushes GPU work, releases backend-owned runtime
  resources, unbinds the active asset bridge, then returns to `Init.cpp` for
  worker/backend/window/COM teardown. Rubber-duck review by Sagan found three
  blocking documentation inaccuracies and one non-blocking ordering note; all
  were fixed before commit. Documentation-only slice; no repository validation
  required.
- [x] 2026-06-28: Routed editor placement's building asset-library lookup
  through explicit borrowed `AssetSystem` context instead of
  `ActiveAssetSystem()`. Placement preview, overlay ghost tracing, placement
  preflight, and placement commit now receive `m_systems.assets` through editor
  context structs; the process-static parsed building catalog remains read-only
  after first load. The runtime-boundary ratchet removed the
  `RunEditorPlacementAssets.inl` `ActiveAssetSystem()` allowlist row, lowering
  the remaining active-asset compatibility surface to the asset-system helper
  declarations/definitions only. Rubber-duck review by Pauli found no blockers,
  two non-blocking residual notes, and three missing-evidence reminders: the
  one-shot parsed building catalog cache is intentionally preserved, unrelated
  user-owned dirty docs remain unstaged, and evidence was completed before
  commit. Comment-style audit inspected the touched source-bearing files.
  Final evidence: `python tools\check_runtime_boundaries.py`,
  `tools\validate_fast.bat`, and `tools\validate_full.bat` all passed; logs are
  under `TestOutput\validation\agent_logs\editor_asset_context_*`.
- [x] 2026-06-28: Routed scene/style parsing asset-library lookup through an
  explicit borrowed `AssetSystem` instead of `ActiveAssetSystem()`. Runtime
  scene loads, live-style reloads, cinematic style browser loads, and demo hero
  style loading now pass `m_systems.assets` into `TestScene` parser overloads;
  tools or standalone parser callers can still use the old path-convention
  fallback by omitting the registry. The runtime-boundary ratchet removed the
  `TestSceneParser.cpp` `ActiveAssetSystem()` allowlist row. That slice left
  editor placement's static building-library cache as separate follow-up debt,
  which the editor placement slice above now closes. Rubber-duck review by Harvey
  found one blocking documentation mismatch: the current-count snapshot still
  claimed `ActiveAssetSystem()` count `4` and still listed
  `TestSceneParser.cpp`. The snapshot now records count `3` and removes the
  parser row. Final evidence: `tools\check_runtime_boundaries.py`,
  `tools\validate_fast.bat`, and `tools\validate_full.bat` all passed; logs are
  under `TestOutput\validation\agent_logs\scene_parser_asset_context_*`.
- [x] 2026-06-28: Added an explicit renderer-global file-classification fence
  to `tools\check_runtime_boundaries.py`. The existing global-service ratchet
  still preserves current counts, but direct `Gfx()` and `GfxRayTracing()` calls
  now also require a named compatibility-location classification such as
  runtime composition root, backend accessor definition, UI compatibility, or
  render helper compatibility. Synthetic tests prove count allowances alone do
  not approve a new unclassified `Gfx()` or `GfxRayTracing()` file. This
  resolves the renderer-specific bootstrap/compatibility guardrail while the
  broader per-site classification of all remaining globals stays open under
  Inventory.
  Comment-style audit: inspected `tools\check_runtime_boundaries.py`; the
  learning header now names the renderer-global classification fence and the
  new allowlist comment explains the invariant.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors
  (`TestOutput\validation\agent_logs\renderer_global_classification_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed
  (`TestOutput\validation\agent_logs\renderer_global_classification_validate_fast.log`).
- [x] 2026-06-28: Added a counted global-service access ratchet to
  `tools\check_runtime_boundaries.py`. The guardrail ignores comments and string
  literals, preserves the current compatibility surface as counted debt, and
  fails on new or grown calls to `Cfg()`, `Gfx()`, `GfxRayTracing()`,
  `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, named service
  singletons, generic non-named `Class::Instance()` access, `pInstance`
  singleton storage, and mutable `g_*` process globals. This is a new-code
  ratchet only; per-site bootstrap/shutdown/OS-bridge classification and source
  migration remain open. Validation: `python tools\check_runtime_boundaries.py`
  passed in 4.81s
  (`TestOutput\validation\agent_logs\global_service_guardrail_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 18.01s
  (`TestOutput\validation\agent_logs\global_service_guardrail_validate_fast.log`).
  Comment-style audit: inspected the touched tool script
  `tools\check_runtime_boundaries.py` against
  `Agentic\Reference\comment-style-guide.md`; the learning header remains
  present, and the new ratchet/allowlist comments state the invariant and
  limits of the guardrail.
- [x] 2026-06-28: Extended runtime render services so renderer-facing frame code
  can borrow an explicit render command capability instead of reaching through
  `Gfx()` for these frame operations.
  `RuntimeRenderer::RenderFrame()` now clears through the borrowed command
  context and checks the captured render-ready flag; `RunRender.cpp` direct
  `Gfx()` calls fell from 2 to 1, and the guardrail allowlist was lowered to
  that new count. Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the new context
  fields have a `Lifetime:` note for the borrowed command facet and no ownership
  moved out of `Run`.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.69s
  (`TestOutput\validation\agent_logs\runtime_render_services_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 23.60s
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.81s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.30s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_full.log`).
  Rubber-duck review: McClintock found no code blockers after the context was
  narrowed to the command capability. The review's stale-validation blocker was
  resolved by rerunning DX12 and full validation after the final source edit,
  and the broad borrowed-context checklist item remains open for future
  non-render contexts.
- [x] 2026-06-28: Threaded the borrowed render command capability into
  `RenderFrameContext` and switched the runtime render-pass texture-slot hygiene
  helpers away from direct `Gfx()` access. This lowers `RunPasses.cpp` direct
  `Gfx()` debt from 98 to 96 and the plan's total counted `Gfx()` surface from
  293 to 291. The broader render-pass/global-service migration remains open
  because the same file still has many compatibility-facade calls for viewport,
  clear, depth/blend/cull, draw state, resource creation, and DXR decisions.
  Comment-style audit: inspected `RuntimeRenderPasses.h`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the command pointer
  is lifetime-annotated and asserted before helper use.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.78s
  (`TestOutput\validation\agent_logs\render_frame_command_context_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.35s
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.01s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.25s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_full.log`).
  Rubber-duck review: Hubble found no blockers; the reviewer called out the
  nullable/assert-only command pointer and counted-ratchet limits as residual
  risks to keep visible in later slices.
- [x] 2026-06-28: Routed generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of direct
  `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 96 to 88
  and the plan's total counted `Gfx()` surface from 291 to 283. The broad
  normal-path render-pass migration remains open because other state,
  viewport, clear, resource, dynamic geometry, and DXR paths still reach the
  compatibility facade.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.81s
  (`TestOutput\validation\agent_logs\sky_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.45s
  (`TestOutput\validation\agent_logs\sky_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.68s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\sky_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.45s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\sky_command_state_validate_full.log`).
  Rubber-duck review: Hilbert found no blockers. Non-blocking note: the sky
  pass still restores depth write from the depth-test snapshot, which matches
  the pre-existing behavior but remains state-contract debt for a later command
  state cleanup.
- [x] 2026-06-28: Routed `WaterPass::Render()` depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of direct
  `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 88 to 76
  and the plan's total counted `Gfx()` surface from 283 to 271. The broad
  render-pass/global-service migration remains open for shadow, reflection,
  tornado, volumetric, tonemap, viewport, clear, resource creation, and DXR
  paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.84s
  (`TestOutput\validation\agent_logs\water_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.55s
  (`TestOutput\validation\agent_logs\water_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.55s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\water_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.22s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\water_command_state_validate_full.log`).
  Rubber-duck review: Dewey found no blockers. Non-blocking note: the
  `Gfx()` guardrail remains a ceiling ratchet, not semantic same-file
  classification, so future slices still need focused review.
- [x] 2026-06-28: Routed `TornadoVisualPass::Render()` depth/blend/cull state
  and transient colored-triangle draw through `RenderFrameContext`'s borrowed
  `IRenderCommandContext` instead of direct `Gfx()` calls. This lowers
  `RunPasses.cpp` direct `Gfx()` debt from 76 to 60 and the plan's total
  counted `Gfx()` surface from 271 to 255. The broad render-pass/global-service
  migration remains open for shadow, reflection, volumetric, tonemap, viewport,
  clear, resource creation, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.72s
  (`TestOutput\validation\agent_logs\tornado_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.34s
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.58s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.89s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_full.log`).
  Rubber-duck review: Hume found no blockers. Non-blocking note: the DX12
  validation suite is broad renderer coverage rather than tornado-scene-specific
  visual proof, so this slice is recorded as a command-context/global-access
  ratchet with full renderer safety-net evidence.
- [x] 2026-06-28: Routed `ShadowPass::RenderShadowMap()` viewport/clear,
  depth/blend/cull state, polygon offset, and final viewport restore through
  its existing `IRenderCommandContext` argument instead of direct `Gfx()` calls.
  This lowers `RunPasses.cpp` direct `Gfx()` debt from 60 to 44 and the plan's
  total counted `Gfx()` surface from 255 to 239. The broad
  render-pass/global-service migration remains open for shadow target creation,
  reflection, volumetric, tonemap, resource creation, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current shadow-map state restore contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.78s
  (`TestOutput\validation\agent_logs\shadow_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.36s
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.02s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.80s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_full.log`).
  Rubber-duck review: Huygens found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy shadow state restore semantics rather than
  fixing depth-write/cull restore debt, the viewport restore relies on window
  and backend dimensions staying in lockstep, and the DX12 validation suite is
  broad renderer coverage rather than a shadow-scene-specific visual proof.
- [x] 2026-06-28: Routed `VolumetricPass::Render()` viewport and depth/blend
  state through `RenderFrameContext`'s borrowed `IRenderCommandContext` instead
  of direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from
  44 to 32 and the plan's total counted `Gfx()` surface from 239 to 227. The
  broad render-pass/global-service migration remains open for the fullscreen
  quad helper, resource creation, reflection, tonemap, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current screen-space state restore
  contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.89s
  (`TestOutput\validation\agent_logs\volumetric_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.71s
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.08s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.87s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_full.log`).
  Rubber-duck review: Meitner found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy depth-write restore semantics, the viewport
  restore relies on window and backend dimensions staying in lockstep, and the
  DX12 validation suite is broad renderer coverage rather than
  volumetric-scene-specific visual proof.
- [x] 2026-06-28: Routed `TonemapPass::Render()` viewport and depth/blend state
  through `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of
  direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 32
  to 21 and the plan's total counted `Gfx()` surface from 227 to 216. The broad
  render-pass/global-service migration remains open for the fullscreen quad
  helper, resource creation, reflection, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current screen-space state restore
  contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.00s
  (`TestOutput\validation\agent_logs\tonemap_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.77s
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.39s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.77s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_full.log`).
  Rubber-duck review: Gibbs found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy depth-write restore semantics, the viewport
  restore relies on window and backend dimensions staying in lockstep, and the
  DX12 validation suite is broad renderer coverage rather than
  tonemap-scene-specific visual proof.
- [x] 2026-06-28: Routed `SceneTargetPass::Begin()` HDR target viewport and
  clear calls through `RenderFrameContext`'s borrowed `IRenderCommandContext`
  instead of direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()`
  debt from 21 to 19 and the plan's total counted `Gfx()` surface from 216 to
  214. The broad render-pass/global-service migration remains open for the
  fullscreen quad helper, resource creation, reflection, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current HDR scene-target invariant.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.98s
  (`TestOutput\validation\agent_logs\scene_target_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.71s
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.44s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.01s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_full.log`).
  Rubber-duck review: Raman found no blockers. Non-blocking note: the `Gfx()`
  guardrail remains a count ratchet rather than semantic per-call tracking, so
  future slices still need focused diff review.
- [x] 2026-06-28: Routed the shared `DrawFullscreenQuad()` dynamic-geometry draw
  helper through an explicit borrowed `IRenderCommandContext&` instead of
  direct `Gfx()` access. This lowers `RunPasses.cpp` direct `Gfx()` debt from 19
  to 18 and the plan's total counted `Gfx()` surface from 214 to 213. The broad
  render-pass/global-service migration remains open for fullscreen quad
  resource creation/destruction, framebuffer creation, reflection, and DXR
  paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses existing borrowed command
  contexts at the cinematic sky, volumetric-light, and tonemap call sites.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.85s
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.99s
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.26s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.49s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_full.log`);
  `tools\validate_perf.bat` completed in 22.48s with exit 0
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_perf.log`).
  Perf review note: the DX12 comparison was skipped for machine mismatch, and
  the script reported physics-bench warnings outside this render-helper slice.
  Rubber-duck review: Bohr found no blockers. Non-blocking notes: perf evidence
  is advisory/noisy on this machine, and the `Gfx()` guardrail remains
  count-based rather than semantic per-call tracking.
- [x] 2026-06-28: Routed planar reflection viewport, clear, clip-plane
  enable/disable, and final viewport restore through `RenderFrameContext`'s
  borrowed `IRenderCommandContext` instead of direct `Gfx()` calls. This lowers
  `RunPasses.cpp` direct `Gfx()` debt from 18 to 13 and the plan's total
  counted `Gfx()` surface from 213 to 208. The broad render-pass/global-service
  migration remains open for reflection capability queries, DXR dispatch,
  framebuffer creation, size queries, and fullscreen quad resource lifetime.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current planar reflection state contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.11s
  (`TestOutput\validation\agent_logs\reflection_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.90s
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.38s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.97s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_full.log`).
  Rubber-duck review: Nietzsche found no blockers and verified no stale
  allowlist mismatch: actual total `Gfx()` count 208, allowlist total 208, and
  `RunPasses.cpp` actual/allowlist 13/13. Non-blocking note: the `Gfx()`
  guardrail remains count-based rather than semantic per-call tracking.
- [x] 2026-06-28: Routed the remaining `RunPasses.cpp` direct `Gfx()` access
  through borrowed render services: framebuffer and fullscreen dynamic
  vertex-buffer lifetime now use `IRenderResourceFactory`, reflection capability
  checks use `IRenderDiagnostics`, and render-target sizes come from sampled
  runtime window dimensions. The teardown path passes a nullable resource
  factory from the composition root so resource handles reset safely even after
  failed backend initialization. This lowers `RunPasses.cpp` direct `Gfx()` debt
  from 13 to 0 and the plan's total counted `Gfx()` surface from 208 to 196.
  `GfxRayTracing()` remains open as a separate narrow DXR capability accessor.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RuntimeRenderer.h`, `Run.cpp`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; all touched
  source-bearing files keep learning headers, and new comments document borrowed
  service lifetime, frame-only diagnostics, and shutdown-without-backend
  behavior.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.87s
  (`TestOutput\validation\agent_logs\render_resource_services_runtime_boundaries_rerun.log`);
  `tools\validate_fast.bat` passed in 104.57s
  (`TestOutput\validation\agent_logs\render_resource_services_validate_fast_final.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.71s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_resource_services_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.13s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_resource_services_validate_full.log`).
  Rubber-duck review: an in-session rubber-duck pass found one blocking teardown
  hazard where backend resource release could have called `Gfx()` when no
  backend was live; the nullable factory handoff fixed it before validation.
  Residual non-blocking notes: the guardrail is still a counted ratchet rather
  than a semantic bootstrap/normal-path classifier, and broader service
  classification remains open.
- [x] 2026-06-28: Split runtime render-pass resource creation onto a dedicated
  `RenderResourceContext` so `RenderFrameContext` no longer carries the
  resource-factory service through ordinary draw methods. This does not lower
  the counted global-service surface because the previous slice already removed
  `RunPasses.cpp` direct `Gfx()` calls, but it narrows when the borrowed factory
  is visible: `RuntimeRenderer` builds the resource context for
  `EnsureGpuResources()` calls and ensures shadow targets before
  `ShadowPass::Render()`. Broader service classification remains open because
  `RuntimeRenderServices` still carries the factory to the renderer owner.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RuntimeRenderer.h`, `RunPasses.cpp`, and
  `RunRender.cpp`; learning headers remain present, and the new resource
  context is named in local glossaries and lifetime comments.
  Validation:
  focused `tools\validate_build.bat Profile` passed in 48.25s with 0 warnings
  and 0 errors
  (`TestOutput\validation\agent_logs\render_resource_context_validate_build_profile.log`);
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.73s
  (`TestOutput\validation\agent_logs\render_resource_context_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed on rerun in 101.63s after scoped header
  alignment fixes
  (`TestOutput\validation\agent_logs\render_resource_context_validate_fast_rerun.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.73s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_resource_context_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.52s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_resource_context_validate_full.log`);
  `tools\validate_perf.bat` completed in 22.13s
  (`TestOutput\validation\agent_logs\render_resource_context_validate_perf.log`).
  Perf warnings are recorded: DX12 perf comparison was skipped due a
  machine-label mismatch, and physics_bench reported 9 warning failures
  including `Frame.avg +55.4%` and `Frame/Input.avg +72.5%`; this slice did not
  touch physics/input code, but the warning-bearing output is not claimed as a
  clean perf pass.
- [x] 2026-06-28: Routed the remaining `RunPasses.cpp` direct
  `GfxRayTracing()` access through a borrowed nullable `IRenderRayTracing*`
  supplied by `Run::Render()`. This keeps normal reflection pass code on the
  explicit frame context while preserving the composition root as the place that
  samples global raytracing readiness. The counted global-service surface is
  unchanged: `GfxRayTracing()` remains at 5 because this slice moved the call
  from pass code to `RunRender.cpp` rather than deleting the process-global
  accessor. Other `GfxRayTracing()` callers and broader per-site global
  classification remain open.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RunPasses.cpp`, `RunRender.cpp`, and
  `tools\check_runtime_boundaries.py`; touched files retain learning headers,
  and the new DXR comments include local glossary definitions and lifetime
  notes.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.87s
  (`TestOutput\validation\agent_logs\render_dxr_capability_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 103.96s
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.64s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.76s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_full.log`).
  Rubber-duck review found no blockers; residual debt is the unchanged global
  count and remaining non-composition `GfxRayTracing()` callers.
- [x] 2026-06-28: Narrowed the frame loop's renderer service lifetime by
  sampling `Gfx()` once per frame turn and using borrowed
  `IRenderDiagnostics` and `IRenderDeviceLifecycle` facets for draw-call reset,
  optional pipeline drain, UI draw-call accounting, and present. This lowers
  `RunFrame.cpp` direct `Gfx()` debt from 4 to 1 and the plan's total counted
  `Gfx()` surface from 193 to 190 while keeping the composition-root-style
  frame loop as the only service sampling point for these frame operations.
  The checker allowlist now ratchets `RunFrame.cpp` to one renderer-service
  sample. Comment-style audit: inspected `RunFrame.cpp` and
  `tools\check_runtime_boundaries.py`; `RunFrame.cpp` retains its learning
  header and the new lifetime comment names the borrowed renderer facets.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.79s
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_runtime_boundaries.log`);
  `tools\validate_format.bat` passed in 7.22s
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_validate_format.log`);
  `git diff --check` passed in 0.13s
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_diff_check.log`);
  `tools\validate_full.bat` passed in 36.16s, including DX12 validation with 0
  InfoQueue errors, matching screenshots, standalone physics smoke, and
  byte-exact `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_validate_full.log`).
  No rubber-duck review was run for this slice per current user instruction.

## Current Counted Global-Service Surface

This snapshot is the guardrail allowlist as of 2026-06-28. Counts are taken from
source-bearing files under `SkullbonezSource/` after stripping comments and
string literals.

| Pattern | Current count |
|---------|---------------|
| `Cfg()` | 239 |
| `Gfx()` | 190 |
| `GfxRayTracing()` | 4 |
| `ActiveAssetSystem()` | 2 |
| `CreateShaderFromActiveAssets()` | 16 |
| `TextureCollection::Instance()` | 3 |
| `CameraCollection::Instance()` | 4 |
| `Window::Instance()` | 8 |
| `SkyBox::Instance()` | 2 |
| `WorkerPool::Instance()` | 18 |
| `Profiler::Instance()` | 27 |
| Generic non-named `Class::Instance()` | 7 |
| `pInstance` | 20 |
| Mutable `g_*` process global | 86 |

## Current Counted Global-Service Allowlist Classification

Grouped by source file. Each `label=count` entry corresponds to one counted
allowlist row in `tools\check_runtime_boundaries.py`; the classification names
why that current debt exists and which migration bucket should own it.

| File | Counted labels | Classification | Owner / migration note |
|------|----------------|----------------|------------------------|
| `SkullbonezSource/Assets/AssetSystem.cpp` | `ActiveAssetSystem()=1`, `CreateShaderFromActiveAssets()=1`, `Gfx()=2`, `g_*=5` | asset lookup | Asset system singleton, source-record storage, and shader creation should move behind an explicit asset/render context. |
| `SkullbonezSource/Assets/AssetSystem.h` | `ActiveAssetSystem()=1`, `CreateShaderFromActiveAssets()=1` | asset lookup | Header exposes the current asset lookup helpers; future callers should borrow an asset context. |
| `SkullbonezSource/Assets/TextureCollection.cpp` | `Gfx()=3`, `TextureCollection::Instance()=1` | asset lookup | Texture lifetime remains singleton-backed and renderer-coupled until a runtime-owned texture service exists. |
| `SkullbonezSource/Core/Common.h` | `Cfg()=2`, `EngineConfig::Instance()=1` | bootstrap | Convenience config accessor shim; keep as legacy debt while runtime config snapshots replace normal-path reads. |
| `SkullbonezSource/Core/Config.cpp` | `EngineConfig::Instance()=1` | bootstrap | Config owner singleton implementation. |
| `SkullbonezSource/Core/LockOrderValidator.cpp` | `LockOrderValidator::Instance()=5`, `g_*=12` | diagnostics | Global lock-order diagnostics state. |
| `SkullbonezSource/Core/PlatformProfiler.cpp` | `g_*=12` | diagnostics | Platform profiler marker bridge and process-local telemetry. |
| `SkullbonezSource/Core/Profiler.cpp` | `Gfx()=9`, `Profiler::Instance()=2` | diagnostics | Profiler UI/marker diagnostics still sample renderer and singleton state. |
| `SkullbonezSource/Core/Profiler.h` | `Profiler::Instance()=11` | diagnostics | Profiler accessor surface. |
| `SkullbonezSource/Core/WorkerPool.cpp` | `WorkerPool::Instance()=2`, `g_*=8` | normal runtime path | Worker service singleton and queue state; should become a borrowed worker service before more runtime use grows. |
| `SkullbonezSource/GameObjects/GameModel.cpp` | `Cfg()=10` | normal runtime path | Model defaults still read global config. |
| `SkullbonezSource/Physics/BoundingSphere.cpp` | `Cfg()=1` | normal runtime path | Physics helper still reads global config. |
| `SkullbonezSource/Physics/Debug/BroadphaseVisualizer.cpp` | `Gfx()=2` | diagnostics | Physics debug visualizer renderer access. |
| `SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp` | `CreateShaderFromActiveAssets()=1`, `Gfx()=14` | diagnostics | Collision visualizer shader/render access. |
| `SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp` | `Gfx()=2` | diagnostics | Physics line visualizer renderer access. |
| `SkullbonezSource/Physics/PersistentContactSolver.cpp` | `Cfg()=26` | normal runtime path | Solver parameters still read global config; physics context should own the snapshot. |
| `SkullbonezSource/Physics/PhysicsWorld.cpp` | `Cfg()=18`, `WorkerPool::Instance()=6` | normal runtime path | Physics world still borrows config/worker service globally. |
| `SkullbonezSource/Physics/RigidBody.cpp` | `Cfg()=3` | normal runtime path | Rigid-body defaults still read global config. |
| `SkullbonezSource/Physics/TornadoField.cpp` | `Gfx()=2` | test/tool | Tornado visual/debug draw helper still uses global renderer access. |
| `SkullbonezSource/Rendering/GameModelRenderer.cpp` | `Cfg()=3`, `WorkerPool::Instance()=2` | render pass | Renderer worker/config access should come from render services. |
| `SkullbonezSource/Rendering/Helper.cpp` | `Cfg()=2`, `Gfx()=34`, `CreateShaderFromActiveAssets()=2` | render pass | Shared renderer helpers remain wide-backend and asset-factory debt. |
| `SkullbonezSource/Rendering/IRenderBackend.cpp` | `Gfx()=2`, `GfxRayTracing()=1` | render pass | Backend facade compatibility; capability interfaces should replace wide access. |
| `SkullbonezSource/Rendering/IRenderBackend.h` | `Gfx()=3`, `GfxRayTracing()=1` | render pass | Backend facade header still exposes compatibility helpers. |
| `SkullbonezSource/Rendering/Shadow.h` | `Gfx()=2` | render pass | Shadow helper still reaches global backend. |
| `SkullbonezSource/Rendering/Text.cpp` | `Cfg()=2`, `Gfx()=33`, `CreateShaderFromActiveAssets()=3` | render pass | Text rendering should receive config, renderer, and shader services explicitly. |
| `SkullbonezSource/Runtime/Camera.cpp` | `Cfg()=22` | normal runtime path | Camera behavior still reads global config. |
| `SkullbonezSource/Runtime/CameraCollection.cpp` | `CameraCollection::Instance()=1`, `Cfg()=1`, `pInstance=6` | normal runtime path | Legacy camera singleton and config access. |
| `SkullbonezSource/Runtime/CameraCollection.h` | `pInstance=1` | normal runtime path | Legacy camera singleton storage declaration. |
| `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp` | `CreateShaderFromActiveAssets()=1`, `Gfx()=18` | test/tool | Launcher/editor visual feedback should borrow render services. |
| `SkullbonezSource/Runtime/Editor/LauncherTools.cpp` | `Cfg()=3` | test/tool | Launcher tool tuning still reads global config. |
| `SkullbonezSource/Runtime/Editor/RunEditorTracer.inl` | `Gfx()=1` | test/tool | Editor tracer draw path should borrow render command/context services. |
| `SkullbonezSource/Runtime/Init.cpp` | `Cfg()=16`, `Window::Instance()=1`, `WorkerPool::Instance()=3`, `g_*=6` | bootstrap | Startup command-line/config/window/worker binding surface. |
| `SkullbonezSource/Runtime/Input.cpp` | `Window::Instance()=3`, `g_*=43` | OS callback bridge | Win32 input accumulators and focus/window bridge. |
| `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp` | `Cfg()=4` | render pass | Render host still reads config instead of receiving a render/runtime config snapshot. |
| `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.inl` | `Cfg()=2` | normal runtime path | Replay cause-tree UI/tool tuning still reads global config. |
| `SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl` | `WorkerPool::Instance()=2` | normal runtime path | Replay prediction should borrow worker services from runtime context. |
| `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.inl` | `Cfg()=2` | normal runtime path | Replay scrubber tuning still reads global config. |
| `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl` | `Cfg()=2` | normal runtime path | Replay velocity-edit tuning still reads global config. |
| `SkullbonezSource/Runtime/Run.cpp` | `CameraCollection::Instance()=1`, `Cfg()=8`, `Gfx()=8`, `Profiler::Instance()=1`, `SkyBox::Instance()=1`, `TextureCollection::Instance()=1`, `Window::Instance()=1` | normal runtime path | Composition-root compatibility; migrate to bound services before normal paths call globals directly. |
| `SkullbonezSource/Runtime/RunFrame.cpp` | `Cfg()=7`, `Gfx()=1`, `Profiler::Instance()=3` | normal runtime path | Frame loop now samples the renderer once per frame turn before borrowing narrow lifecycle and diagnostics facets; config and profiler globals remain. |
| `SkullbonezSource/Runtime/RunInput.cpp` | `Cfg()=22`, `Gfx()=4` | normal runtime path | Input/tool routing still reads config and renderer state globally. |
| `SkullbonezSource/Runtime/RunInteractionAutomation.cpp` | `Cfg()=2` | test/tool | Interaction automation tuning still reads global config. |
| `SkullbonezSource/Runtime/RunLiveStyle.cpp` | `Cfg()=1` | normal runtime path | Live style path still reads config globally. |
| `SkullbonezSource/Runtime/RunPasses.cpp` | `Cfg()=6` | render pass | Pass code still reads global config after renderer access migration. |
| `SkullbonezSource/Runtime/RunRender.cpp` | `Cfg()=3`, `Gfx()=1`, `GfxRayTracing()=1` | render pass | Render composition root still samples renderer services before passing borrowed capabilities. |
| `SkullbonezSource/Runtime/RunStress.cpp` | `Cfg()=1`, `Gfx()=2` | test/tool | Stress harness still reads config/renderer globally. |
| `SkullbonezSource/Runtime/RunUiTextPass.cpp` | `Cfg()=1`, `Profiler::Instance()=2`, `WorkerPool::Instance()=1` | render pass | UI text pass still mixes profiler, worker, and config globals; renderer and DXR capability access now come from caller-supplied inputs. |
| `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp` | `Profiler::Instance()=2` | diagnostics | Runtime diagnostics still samples profiler singleton. |
| `SkullbonezSource/Runtime/RuntimeTuning.cpp` | `Cfg()=1`, `WorkerPool::Instance()=1` | normal runtime path | Runtime tuning still reads config/worker globals. |
| `SkullbonezSource/Runtime/Scene/RunScene.cpp` | `Cfg()=18`, `Gfx()=9`, `GfxRayTracing()=1`, `WorkerPool::Instance()=1` | normal runtime path | Scene load/reset still borrows config, renderer, DXR, and worker services globally. |
| `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` | `CameraCollection::Instance()=1` | normal runtime path | Authored scene setup still reaches camera singleton. |
| `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp` | `CameraCollection::Instance()=1` | normal runtime path | Generated scene setup still reaches camera singleton. |
| `SkullbonezSource/Runtime/Window.cpp` | `Cfg()=6`, `Gfx()=1`, `Window::Instance()=3`, `pInstance=4` | OS callback bridge | Window singleton and resize/message integration bridge. |
| `SkullbonezSource/Runtime/Window.h` | `pInstance=1` | OS callback bridge | Window singleton storage declaration. |
| `SkullbonezSource/UI/UI.cpp` | `CreateShaderFromActiveAssets()=1`, `Gfx()=16` | render pass | UI rendering still reaches shader factory and renderer globally. |
| `SkullbonezSource/UI/UIBackdropBlur.cpp` | `CreateShaderFromActiveAssets()=1`, `Gfx()=14` | render pass | Backdrop blur render/capture path still reaches globals. |
| `SkullbonezSource/UI/UITabProfiler.cpp` | `Gfx()=1`, `Profiler::Instance()=6` | diagnostics | Profiler UI tab still samples renderer/profiler globals. |
| `SkullbonezSource/World/SkyBox.cpp` | `Cfg()=2`, `CreateShaderFromActiveAssets()=1`, `Gfx()=1`, `SkyBox::Instance()=1`, `TextureCollection::Instance()=1`, `pInstance=7` | render pass | Skybox lifetime/render resources remain singleton and texture-service debt. |
| `SkullbonezSource/World/SkyBox.h` | `pInstance=1` | render pass | Skybox singleton storage declaration. |
| `SkullbonezSource/World/Terrain.cpp` | `Cfg()=22`, `CreateShaderFromActiveAssets()=2`, `Gfx()=2` | render pass | Terrain runtime/render setup still reads config and shader/backend globals. |
| `SkullbonezSource/World/WorldEnvironment.cpp` | `Cfg()=20`, `CreateShaderFromActiveAssets()=2`, `Gfx()=3` | normal runtime path | World/fluid environment still mixes config and render resource globals. |

## Existing Service Lifetime Owners

This 2026-06-28 inventory is the preferred reuse surface before adding any new
service context. The important split is ownership versus borrowing: `Run` owns
most process-lifetime systems, while contexts such as `EngineContext` and
`RuntimeRenderHost` should remain borrowed views over those owners.

| Owner/view | Owns lifetime? | Current service boundary | Use before adding |
|------------|----------------|--------------------------|-------------------|
| `Run` (`SkullbonezSource/Runtime/Run.h`) | Yes | Process composition root for scene, diagnostics, systems, input, interaction, simulation, replay, UI, tools, world, model collection, command queue, render host, and renderer. | Top-level bind order, shutdown order, and remaining composition-root sampling of globals such as renderer capabilities. |
| `EngineContext` (`SkullbonezSource/Runtime/EngineContext.h`) | No, borrowed view | Binds pointers to `SceneController`, `SimulationController`, capture/diagnostics controllers, command queue, systems, runtime settings, input, camera/debug state, world, and models. | Runtime extraction slices that need a declared boundary without directly reaching through `Run` members. |
| `RuntimeRenderHost` (`SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`) | Owns render scratch only; borrows services | Groups render-facing runtime, world, scene, replay overlay, tool overlay, UI, and diagnostics views; owns DXR reflection transform scratch. | Render-pass dependencies that would otherwise call `Run`, `Gfx()`, or unrelated singleton services. |
| `RuntimeTools` (`SkullbonezSource/Runtime/Tools/RuntimeTools.h`) | Yes, for transient tool state | Owns launcher/raycast state, launcher laser, mouse pickup state, editor placement state, and editor tracer feedback. | Launcher, editor, mouse-pickup, and transient overlay state before adding more tool fields to `Run`. |
| `DiagnosticsRuntime` (`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`) | Yes | Owns capture controller, diagnostics controller, perf log state, main-memory dump path/cache, UI stress state, and Debug-only physics diagnostics helpers. | Capture, perf, SkullScope, memory dump, and validation artifact state before reaching through global diagnostics or ad hoc file paths. |
| `SceneController` (`SkullbonezSource/Runtime/Scene/SceneController.h`) | Yes | Owns scene runtime, scene queue/index bookkeeping, browser state, and scene UI overrides. | Scene queue/runtime state and scene UI policy before adding scene fields to `Run` or accessing globals from scene code. |
| `RuntimeRenderer` (`SkullbonezSource/Runtime/Render/RuntimeRenderer.h`) | Yes | Owns runtime render pass objects and frame render ordering. | Render pass sequencing and pass-owned resources before growing `Run::Render()` as a service locator. |
| `RuntimeCommandQueue` (`SkullbonezSource/Runtime/RuntimeCommandQueue.h`) | Yes | Owns deferred runtime/tool command intent drained at frame boundaries. | Cross-system UI/tool/runtime mutations before adding process globals or synchronous callback state. |
| `SimulationController` (`SkullbonezSource/Runtime/SimulationController.h`) | Yes | Owns fixed-step accumulator and simulation tick policy. | Simulation timing and fixed-step state before passing raw timing globals through physics/runtime callers. |

Rubber-duck review: Banach found no blockers and confirmed that all six
checklist owners are present. Non-blocking clarity feedback narrowed the
`RuntimeRenderHost` lifetime wording so the table cannot be read as ownership
of borrowed runtime services.

## Problem Statement

The Carmack-test verdict flagged remaining globals and singletons as a serious
encapsulation risk. Some globals are legitimate OS callback bridges, but normal
runtime/render/asset/scene paths still use process-global service access where
explicit lifetime and ownership would be easier to reason about.

## Goal

Replace normal-path global service access with explicit service contexts,
borrowed interfaces, or composition-root wiring. Keep unavoidable OS callback
bridges tiny, named, and fenced.

## Success Bar

- New runtime, render, physics, scene, asset, UI, and diagnostics code does not
  call process-global service accessors.
- Existing global access is either removed, isolated to bootstrap/shutdown, or
  documented as an OS callback bridge.
- Service lifetime order is explicit in `Run`, `EngineContext`, or narrower
  subsystem contexts.
- Guardrails reject new normal-path global access.

## Related Plans

- `Agentic/Plans/global-service-context-plan.md` is the active umbrella plan for
  removing normal-path global service access. Use this Carmack plan as the
  final acceptance checklist for the encapsulation bar.
- `Agentic/Plans/IN PROGRESS/carmack-render-backend-capability-plan.md` owns renderer
  capability access and `Gfx()` migration in renderer-facing code. This plan
  owns the broader service-lifetime and callback-bridge rules.
- `Agentic/Plans/runtime-static-allocation-policy-plan.md` owns dynamic
  allocation policy for any new context storage introduced during this cleanup.

## Implementation Checklist

### Inventory

- [x] Run `rg "Gfx\\(|GfxRayTracing\\(|Cfg\\(|ActiveAssetSystem\\(|CreateShaderFromActiveAssets\\(|::Instance\\(|pInstance|g_[A-Za-z_]" SkullbonezSource`.
- [x] Classify each current counted allowlist row as `bootstrap`, `shutdown`,
  `OS callback bridge`, `normal runtime path`, `render pass`, `asset lookup`,
  `diagnostics`, or `test/tool`.
  - [x] 2026-06-28 current global-service classification table groups every
    counted allowlist row by source file and assigns each row to one migration
    bucket.
- [ ] Classify each individual source hit as `bootstrap`, `shutdown`,
  `OS callback bridge`, `normal runtime path`, `render pass`, `asset lookup`,
  `diagnostics`, or `test/tool`.
- [x] Record the current allowlist in this plan before changing source.
- [x] Identify service lifetime owners already available in `Run`,
  `EngineContext`, `RuntimeRenderHost`, `RuntimeTools`, `DiagnosticsRuntime`, and
  `SceneController`.

### Service Context Shape

- [ ] Define or extend an `EngineServices` or equivalent context for process
  services that must be shared.
- [x] Define or extend a `RenderServices`/`RenderContext` for renderer-facing
  services instead of direct `Gfx()` calls.
- [ ] Define or extend an `AssetContext` for asset lookup and source records
  instead of `ActiveAssetSystem()`.
- [ ] Define or extend an `InputEventBuffer` or input bridge for Win32 callback
  accumulators.
- [ ] Define or extend a `WindowService` or explicit window reference for window
  queries and title/resize behavior.
- [ ] Keep contexts borrowed and lifetime-annotated; do not create a new global
  service locator under a nicer name.

### Remove Normal-Path Globals

- [ ] Route render pass backend access through render capability/context
  arguments.
- [x] Route render pass texture-slot hygiene helpers through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route water depth/blend state through `RenderFrameContext`'s borrowed
  command context instead of direct `Gfx()` calls.
- [x] Route tornado visual depth/blend/cull state and transient triangle draw
  through `RenderFrameContext`'s borrowed command context instead of direct
  `Gfx()` calls.
- [x] Route shadow-map viewport/clear/depth/blend/cull/polygon-offset state
  through its existing command context argument instead of direct `Gfx()` calls.
- [x] Route volumetric pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route tonemap pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route HDR scene-target viewport/clear through `RenderFrameContext`'s
  borrowed command context instead of direct `Gfx()` calls.
- [x] Route the shared fullscreen quad dynamic draw helper through a borrowed
  command context instead of direct `Gfx()` calls.
- [x] Route planar reflection viewport/clear/clip-plane state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route `RunPasses.cpp` framebuffer creation, fullscreen dynamic
  vertex-buffer lifetime, render-target size queries, and reflection capability
  queries through borrowed render services instead of direct `Gfx()` calls.
- [x] Split runtime pass resource creation/rebuild calls onto
  `RenderResourceContext` so `RenderFrameContext` no longer carries the
  resource-factory service into draw methods.
- [x] Route reflection-pass DXR access through a borrowed nullable
  `IRenderRayTracing*` in `RenderFrameContext` instead of direct
  `GfxRayTracing()` access in `RunPasses.cpp`.
- [x] Route frame-loop draw-call reset, optional pipeline drain, UI draw-call
  accounting, and present through one frame-level renderer borrow plus narrow
  diagnostics/lifecycle facets instead of repeated direct `Gfx()` calls.
- [ ] Route shader and texture creation through an asset/render context passed
  from runtime-owned services.
- [x] Replace `ActiveAssetSystem()` in scene parsing and editor tools with an
  explicit asset context.
  - [x] 2026-06-28 scene/style parser calls now accept an explicit borrowed
    `AssetSystem` from runtime-owned services.
  - [x] 2026-06-28 editor placement preview, tracing, preflight, and commit now
    borrow `AssetSystem` from runtime-owned services.
- [ ] Replace `TextureCollection::Instance()` normal-path lookups with runtime
  owned texture service references.
- [ ] Replace `CameraCollection::Instance()` normal-path lookups with explicit
  camera service references.
  - [x] 2026-06-28 authored/generated scene setup now uses the
    `SceneAuthoredCameraContext` / `SceneGeneratedCameraContext` camera service
    supplied by `RunScene.cpp`, rather than reacquiring the camera singleton.
- [ ] Replace `Window::Instance()` normal-path lookups with explicit window
  service references after bootstrap.
- [ ] Replace `SkyBox::Instance()` with runtime/world-owned skybox lifetime or a
  scene-render resource owner.
- [ ] Keep config reads grouped through launch/runtime config context where
  possible; do not spread new `Cfg()` calls.

### OS Callback Bridges

- [ ] Keep Win32 input globals only behind a tiny bridge if callback signatures
  require process-static state.
- [x] Add comments naming who samples, resets, and owns each callback
  accumulator.
  - [x] 2026-06-28 `Input.cpp` now names `WndProc` as the wheel/raw mouse
    writer, `UIInput::CaptureSnapshot()`/`InGameUI::UpdateInput()` as the wheel
    consumer, `RunInput` camera mouse-look sampling as the raw-delta consumer,
    `InputController` focus/mouse-look reset helpers as reset paths, and
    separates cursor policy plus `RunInteractionAutomation` overrides from
    callback accumulator state.
- [x] Add an explicit bind/unbind lifecycle for callback bridge state.
  - [x] 2026-06-28 `Input::BindCallbackBridge()` and
    `Input::UnbindCallbackBridge()` now bind callback-fed wheel/raw mouse queues
    to the active `HWND`; WndProc accumulators ignore late or foreign-window
    callbacks.
- [x] Ensure callback bridge teardown cannot leave dangling service pointers.
  - [x] 2026-06-28 `CleanupWindow()` unbinds the input callback bridge while
    `window->m_sWindow` still names the Win32 window used by WndProc and before
    backend/window class teardown.
- [x] Add focused tests or debug assertions for callback bridge lifecycle.
  - [x] 2026-06-28 Debug assertions catch double-bind, unbound unbind,
    wrong-window unbind, and raw-input registration before the bridge is bound.

### Lifetime Order

- [x] Document startup bind order for renderer, assets, textures, window,
  cameras, input, diagnostics, and scene services.
- [x] Document shutdown unbind order and backend resource release order.
- [x] Add assertions that borrowed service pointers are bound before use.
  - [x] 2026-06-28 `EngineContext::Bind()`, both `Bindings()` accessors, and
    `RuntimeViewModelBuilder::Build()` now assert in Debug builds before
    incomplete borrowed runtime service bindings can be used or silently
    converted into a default presentation snapshot.
- [x] Add a local assertion before runtime render-pass texture-slot helpers use
  `RenderFrameContext::renderCommands`.
- [ ] Add assertions that services are unbound before destruction when callbacks
  can fire late.
- [ ] Keep `Run.h` as composition root wiring, not a bag of service-locator
  helpers.

#### Runtime Service Lifetime Order, 2026-06-28

Startup order:

| Order | Owner | Current bind / creation step | Notes |
|-------|-------|------------------------------|-------|
| 1 | `Init.cpp` / process entry | `CoInitializeEx`, command-line parsing, and config checks. | Startup can exit before worker/window/backend creation if arguments are invalid. |
| 2 | `WorkerPool::Instance()` | `WorkerPool::Instance().Initialise(Cfg().workerThreads)`. | Worker self-test runs only after the pool is initialized; self-test mode then shuts the pool down and exits before window/backend creation. |
| 3 | `Window::Instance()` | `Window::Instance()`, `CreateAppWindow(...)`, and `GetDC(...)`. | Win32 window/device context exist before DX12 backend init. |
| 4 | Renderer backend | `InitRenderBackend(window)` creates `RenderBackendDX12`, calls `backend->Init(...)`, then `SetGfxBackend(...)` and `SetGfxRayTracingBackend(...)`. | `SetGfxBackend` owns the backend; `GfxRayTracing()` is a borrowed alias cleared by `DestroyGfxBackend()`. |
| 5 | Window resize bridge | `window->HandleScreenResize()` after backend bind. | Backend exists when resize handling queries/rebuilds render state. |
| 6 | `Run` scoped runtime | `RunApp(window, args)` constructs `Run`; `Run` destructor runs before backend/window teardown. | `Init.cpp` explicitly scopes `Run` so runtime render resources release while DX12 is alive. |
| 7 | `EngineContext` borrowed runtime graph | `Run::Run()` calls `BindEngineContext()` before `Run::Initialise()`. | `EngineContext` borrows scene, simulation, capture, diagnostics, commands, subsystem state, runtime settings, input, camera/debug, world, and model storage owned by `Run`. |
| 8 | Runtime service aliases | `Run::Initialise()` binds `m_systems.window = Window::Instance()`, reads `Gfx().GetRendererName()`, and sets the window title. | Window and renderer are preconditions for `Run::Initialise()`. |
| 9 | Asset/texture bridge | `TextureCollection::Instance()`, `TextureCollection::BindAssetSystem(&m_systems.assets)`, `BindActiveAssetSystem(&m_systems.assets)`, then `RegisterBuiltInAssets()`. | Texture and shader compatibility helpers still borrow the runtime-owned `AssetSystem` through this bridge. |
| 10 | Initial render resource records | `RebuildRegisteredRenderResources()` resets render helper caches, re-registers built-in source records, and rebuilds textures. | Shader source records are registered through built-in assets; shader objects are still created lazily by callers such as UI/render helper paths. |
| 11 | Terrain/world/sky/UI/camera scene setup | Terrain is constructed from registered source asset path; skybox singleton is created/reset; world environment receives config and terrain bounds; UI text resources are ensured; camera singleton is bound; `LoadScene(0)` starts scene runtime. | Scene services are last because they depend on asset, terrain, render, world, and camera state. |

Shutdown order:

| Order | Owner | Current unbind / release step | Notes |
|-------|-------|-------------------------------|-------|
| 1 | `Run::~Run()` | If `Gfx()` is ready, flush GPU work before releasing runtime-owned render resources. | Prevents queued GPU work from reading resources while runtime owners destroy them. |
| 2 | `Run::ReleaseBackendOwnedRenderResources("shutdown_release")` | Releases world environment render resources, helper resources, game-model resources, collision visualizer, UI resources, runtime render-pass resources, profiler GPU queries, texture collection GPU textures, camera resources, skybox resources, and launcher laser resources. | World environment reset is followed by an immediate flush because it can record upload commands before later release steps. |
| 3 | Asset bridge | `Run::~Run()` calls `BindActiveAssetSystem(nullptr)`. | The active asset compatibility bridge must be unbound while runtime-owned `m_systems.assets` still exists. |
| 4 | Worker/backend/window/COM cleanup | After `RunApp()` returns, `WorkerPool::Instance().Shutdown()` runs, then `CleanupWindow(...)` calls `DestroyGfxBackend()`, releases the Win32 device context, restores fullscreen cursor/display state if needed, unregisters the window class, and calls `Window::Destroy()` to clear the singleton pointer. `CoUninitialize()` completes process teardown after cleanup returns. | `DestroyGfxBackend()` clears raytracing alias state before releasing the backend owner; current `Window::Destroy()` does not destroy the HWND, it clears `Window::pInstance`. |

### Guardrails

- [x] Extend `tools\check_runtime_boundaries.py` to block new normal-path
  `Gfx()`, `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, and
  singleton `Instance()` calls outside allowlisted bootstrap/bridge files.
  2026-06-28 note: implemented as a counted new-code ratchet for current source
  files, with an explicit file-classification fence for direct renderer globals;
  the broader per-site bootstrap/bridge classification is still open under
  Inventory.
- [x] Add counted allowlists for remaining globals and lower them after each
  migration slice.
- [x] Add synthetic checker tests that reject a new normal-path global service
  access.
- [x] Add a review checklist entry asking whether a new dependency should be a
  borrowed context instead of a global.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
  - [x] 2026-06-28 review-checklist entry slice was plan-only; no repository
    validation required.
  - [x] 2026-06-28 lifetime-order documentation slice was plan-only; no
    repository validation required.
- [x] For runtime-wide lifetime or startup/shutdown changes: run `tools\validate_full.bat`.
  - [x] 2026-06-28 EngineContext borrowed-binding assertion slice:
    `tools\validate_full.bat` passed in 34.11s; log:
    `TestOutput\validation\agent_logs\engine_context_assertions_validate_full.log`.
- [x] For renderer service access changes: run `tools\validate_dx12_renderer.bat`.
- [ ] For asset registration, scene asset loading, hull asset, or scene JSON
  behavior changes: run `tools\validate_full.bat`.
- [x] For input/window changes: run `tools\validate_full.bat`; add focused
  launch/click validation if interaction behavior changes.
  - [x] 2026-06-28 input callback bridge documentation slice was comment-only;
    no repository validation required. `git diff --check` passed and the scoped
    comment-style audit inspected `SkullbonezSource/Runtime/Input.cpp`.
  - [x] 2026-06-28 input callback bridge lifecycle slice:
    `tools\validate_full.bat` passed in 140.80s; log:
    `TestOutput\validation\agent_logs\input_callback_bridge_validate_full.log`.
    `tools\validate_interaction_clicks.bat` passed in 5.74s; log:
    `TestOutput\validation\agent_logs\input_callback_bridge_validate_interaction_clicks.log`.
- [x] For guardrail-tooling changes: run `python tools\check_runtime_boundaries.py`
  and `tools\validate_fast.bat`.
- [x] Quote validation output and log paths in the handoff.

## Independent Review Checklist

- [x] Ask a rubber-duck reviewer to distinguish legitimate callback bridges from
  avoidable service locators.
- [x] Ask the reviewer to inspect startup/shutdown lifetime order and borrowed
  pointer safety.
- [x] Ask the reviewer to search for new normal-path global access.
- [x] Ask whether any new dependency should be passed as an explicit borrowed
  context instead of reached through `Gfx()`, `ActiveAssetSystem()`, `Cfg()`,
  singleton `Instance()`, `pInstance`, or `g_*` process state.
- [x] Record review findings in a report or this plan.
- [x] Resolve blocking findings before committing PR-bound code.

Reviewer notes, 2026-06-28:

- Ampere blocked the first guardrail draft because it only watched named
  renderer/asset/window singleton access and missed plan-named `Cfg()`, generic
  `::Instance()`, `pInstance`, and `g_*` process globals.
- Ampere also flagged that file-level counts are a ratchet, not a substitute
  for classifying every remaining call as bootstrap, shutdown, OS callback
  bridge, or normal runtime debt.
- The guardrail was expanded to cover the missing patterns and string/comment
  false positives were removed. The broader per-site classification remains
  intentionally unchecked.
- Ampere re-checked the expanded guardrail and found no remaining blockers
  before commit. Residual non-blocking note: the `g_*` pattern counts references
  as well as declarations, which is conservative but acceptable for this
  new-code ratchet.
- Harvey blocked the scene parser asset-context slice until the current counted
  surface table matched the lowered `ActiveAssetSystem()` ratchet. The snapshot
  now removes `TestSceneParser.cpp` and records `ActiveAssetSystem()` count `3`.
- Pauli found no blocking defect in the editor placement asset-context slice.
  Non-blocking residuals: the parsed building catalog remains a one-shot
  process-static cache by design for this slice, and unrelated dirty docs stay
  user-owned/uncommitted. Pauli's missing-evidence reminders were cleared by the
  touched-source comment-style audit plus final logged runtime-boundary,
  `validate_fast`, and `validate_full` gates.
- Sagan blocked the lifetime-order documentation slice until it included
  `Run::Run()` / `EngineContext` bindings, stopped overclaiming shader resource
  creation in `RebuildRegisteredRenderResources()`, and corrected the window
  cleanup wording around `UnregisterClass(...)` and `Window::Destroy()`. The
  non-blocking worker self-test ordering note was also folded into the startup
  table.
- Faraday blocked the borrowed-binding assertion slice because
  `RuntimeViewModelBuilder::Build()` still returned a default snapshot before
  calling `EngineContext::Bindings()`, bypassing the new fail-fast path. The
  follow-up assertion in the view-model builder fixed that blocker; no normal
  startup/shutdown false positive remained in review.
- Kepler blocked the input callback bridge documentation slice because the first
  comment draft called cursor policy and automation override state callback
  accumulators, and misnamed the mouse-wheel consumer/reset path. Follow-up
  review confirmed those blockers were fixed; the final non-blocking invariant
  wording catch was also corrected before commit.
- Mencius blocked the input callback bridge lifecycle slice because the first
  code draft allowed same-window rebind/unbound unbind, let
  `RegisterRawMouseInput()` reset state for an unbound `HWND`, and then left the
  plan stale after code fixes. The final code gates callback accumulators on the
  bound `HWND`, tightens lifecycle assertions, and updates only the narrow
  callback lifecycle/debug-assertion items.

## Definition Of Done

- [ ] Normal runtime/render/asset/scene paths use explicit contexts or borrowed
  interfaces instead of process globals.
- [ ] Remaining globals are bootstrap-only, shutdown-only, or OS callback bridges
  with explicit lifecycle comments.
- [ ] Guardrails prevent new global service access from creeping back in.
- [ ] Required validation passes for the touched implementation areas.
