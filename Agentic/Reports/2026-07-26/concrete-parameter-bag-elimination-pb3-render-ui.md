# Concrete Parameter-Bag Elimination PB3 Render And UI

Date: 2026-07-26
Implementation base: `d712e20b`
Branch: `nightrunner-25th-JUL-26`

## Result

PB3 passes. The render-frame, UI-text, Replay-overlay, and graph-callback
service bags are deleted. Render sequencing now passes concrete phase values
and node-local graph invocations while `RuntimeRenderModelFrameView` remains
the previously approved Scene-to-render publication.

The retired symbols have zero definitions/usages:

- `RenderFrameContext`;
- `UiTextPassInputs`;
- `ReplayOverlayRenderContext`;
- `BuildRenderFrameContext`;
- `UiTextPassState`;
- all 13 PB0-named `*GraphCallbackData` payloads.

The repository-wide threshold-13 inventory is empty.

## Concrete Render Boundaries

`RenderCameraLighting` carries only camera transforms, axes, and directional
light values. UI text is decomposed by responsibility into Scene/HUD,
interaction, frame, operator, Replay, and submission phases. RuntimeRenderer
retains its backend owners and supplies them directly when executing
`UiTextPass`; no backend service view crosses the composer boundary.

The graph ABI uses node-specific stack invocations that contain one concrete
pass owner plus only that node's values and synchronous resource borrows.
`CinematicPostGraphState` publishes the ordered volumetric-to-tonemap result
between those two invocations. `ReplayOverlayViewport` carries only viewport
dimensions.

The three repaired operations are below the ceiling:

- `UiDrawSubmission::SubmitWithPreviews`: 11 parameters;
- `UiDrawSubmission::SubmitCommands`: 11 parameters;
- `OperatorEditorFrameComposer::Render`: 9 parameters.

## Review Repair

The first ownership review rejected an intermediate
`RuntimeRenderBackendView` composer parameter because it merely moved the
service bag. The final implementation removes it: OperatorEditorFrameComposer
receives no backend owner, RuntimeRenderer retains all backend authority, and
UI diagnostics cross back as a detached three-field value snapshot.

The performance gate then exposed 146 allocations attributed to steady
gameplay. Symbol resolution identified cold `SceneLoadTransaction` navigation
and presentation copies opened from the frame boundary. Their owner now
establishes the existing SceneLoad allocation phase around those copies. The
rerun reports zero guarded allocations and no performance regression.

## Comment Audit

Touched-source inventory: 17 files checked, 17 compliant, 0 deferred.

The audit verified or corrected:

- render-pass ordering and graph-invocation lifetimes;
- concrete DX12 resource ownership at RuntimeRenderer;
- detached UI and Replay phase-value semantics;
- Scene-to-render publication ownership;
- cold scene-load allocation attribution;
- Replay overlay viewport and render-packet responsibilities.

Every touched source-bearing file has the required learning header and local
concept, reason, invariant, lifetime, or hazard comments where the code needs
them. No term needs human-approved wording.

## Static Proofs

The retired-symbol scan and all 13 callback-payload scans return no rows.
`tools/inventory_wide_signatures.py --threshold 13 --format json` returns
`[]`. Dependency validation passes, and both the lower-UI-to-Runtime and
downward-Replay include proofs return no rows.

The allocation-policy checker reports no allowlist errors. Introduced-line
review found no inheritance, interface, callback pack, replacement service
bag, retained host pointer, or new runtime allocation path.

## Validation

- focused Profile build: PASS;
- focused render/UI doctests: PASS, 37 cases / 506 assertions;
- DX12 architecture and UI-boundary tests: PASS;
- `tools\validate_dx12_renderer.bat`: PASS, zero DX12 validation errors and no
  baseline refresh;
- `tools\run_graphics_stress.bat 1`: PASS;
- `tools\validate_perf.bat`: PASS in 110.6 seconds after the allocation-phase
  repair, with zero guarded allocations and no regression;
- `tools\validate_replay_visual_fidelity.bat`: PASS in 409.0 seconds, one
  process/generation/presentation, 2,401 ticks, and all false-pass controls;
- `tools\validate_full.bat`: PASS in 336.2 seconds:
  - formatting and `Related:` paths;
  - 783/783 project filters;
  - dependency graph;
  - Profile/Automation/Debug builds;
  - mandatory CPU and coverage chain;
  - Automation, Replay, and prediction runtime lanes;
  - DX12 validation with zero errors and no baseline refresh;
  - 44,401-line physics regression byte-exact.
