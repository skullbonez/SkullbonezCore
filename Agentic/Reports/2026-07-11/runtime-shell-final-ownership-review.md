# Runtime Shell Final Ownership Review

Date: 2026-07-11

Scope: runtime-shell-decomposition F1-F3, current working source and project metadata

Method: deliberately separate read-only challenge using CodeGraph, focused source inspection,
deletion searches, allocation-diff inspection, and two review passes as required after the first
pass found blockers.

## Outcome

The first pass disproved closure in four places:

1. `RuntimeRenderer` retained mutable debug, timer, camera, editor, tool, and replay owners.
2. Renderer presentation exposed whole-object mutation and UI could traverse framebuffer owners.
3. `UiTextPassState` mixed a broad read projection with mutable timer/UI authority.
4. Debug replay automation crossed the shell through `ReplayProbeWorld`, a whole-scene fixture.

All four findings were fixed before the repeat review. The repeat review found zero remaining
god-object, replacement shared-state hub, Run backpointer, forwarding facade, or compatibility-
smuggling findings in runtime-shell scope.

Renderer-internal RenderGraph callbacks still use stack-local typed payloads through the graph's
current synchronous `void*` ABI. They retain no `Run` or cross-frame context and are owned by the
render-backend-decomposition plan's typed-node migration; they are not runtime-shell authority.

## Final `Run` Method Inventory

| Method | Permitted shell responsibility |
|---|---|
| constructor / destructor | construct, wire, and shut down concrete owners |
| `ApplyStartupOverrides`, `Initialise`, `RunSceneLoadOnly` | startup sequencing and startup result propagation |
| `LastSceneLoadResult` | startup result reporting |
| `Execute` | OS message pump, top-level frame order, final application result |
| `Render`, `UpdateLogic`, `TickPhysics`, `AfterPhysicsStep` | top-level frame ordering only; decisions remain in concrete owners |
| `TickScreenshots`, `TickAutoCycle`, `TickSceneAdvance` | sequence capture/camera/scene owner results into frame control |

No other `Run::*` method definition exists in any tracked `Run*.cpp`.

## Final `Run` Field Inventory

- Process borrows: `Window`, `WorkerPool`, `EngineConfig`, active render-backend capability view.
- Startup/result values: last scene result, skip-execute flag, launch options, startup defaults,
  application-exit latch.
- Concrete owners: assets, scene, render defaults, diagnostics/timers, input/interaction/automation,
  camera/attach/simulation, replay/audio/live-style/UI/debug controls, stress/tools, visualizers,
  and renderer.

No field is a multi-domain `*Internal`, `*Context`, `*Services`, `*Bindings`, compatibility bag,
callback pack, host pointer, or raw replacement storage shelf. SceneController owns scene-lifetime
world/camera/entity/model/physics state. RuntimeRenderer owns textures, sky, pass resources,
presentation policy, graph scratch, and pass-local state.

## Substitute-Hub Surfaces Inspected

- `RuntimeRenderer`: retains render-domain resources only. Debug/timer/tool/replay facts arrive as
  per-call values or synchronous borrows. Presentation mutations use named commands.
- `RuntimeRenderServices` / `FrameEntryContext`: synchronous render-call contracts; not stored.
- `UiTextPassState`: read-only frame projection. The only writable owners, timer rolling state and
  UI drawing, are explicit `UiTextPassInputs` borrows. Render previews are value records.
- `ReplayRestoreTransaction` and topology owners: named replay-domain restore contracts used by
  production and Debug probes; `ReplayProbeWorld` is deleted.
- `InteractionAutomationController`: retains only parsed actions, bounded report data, and injected
  device state. Runtime owners are explicit per-call borrows; there is no Run backpointer/context.
- stress functions: explicit synchronous borrows, no retained context record.

## Runtime Files Above 1,500 Lines

The reconciled tracked inventory contains these remaining large files. None is
shared `Run` state; each has a cohesive owner and a named owning plan or review
boundary.

| Lines | File family | Cohesion / named follow-up owner |
|---:|---|---|
| 4,772 / 3,753 / 3,086 / 3,051 / 2,546 / 1,564 | replay tools/runtime/recorder/probes/artifact/header | ReplayRuntime's completed replay architecture plan records the distinct recorder, artifact, probe, and workspace owners; future replay changes stay there |
| 3,445 | `Runtime/Init.cpp` | cold platform/startup parsing and process construction; validation-gate V3/V4 owns automation/CI entry changes |
| 2,519 | interaction automation | cohesive cold script parse/execute/assert/report harness retaining only its own data; interaction-state-machine owns later interaction-harness changes |
| 2,362 / 2,346 | runtime render passes/renderer | render pass implementations and render composition respectively; render-backend-decomposition owns the typed graph/pass split |
| 2,132 | contact audio | cohesive ContactAudioService implementation; runtime UI/control cleanup owns its control/view surface |
| 2,075 / 2,068 | editor placement/tools | registered-asset placement and tool orchestration; runtime UI/control cleanup owns the editor control split |
| 1,501 | scene load implementation | one cold SceneController load transaction; physics-authority and interaction lifecycle plans own their remaining operands |

The line count is not closure evidence by itself; this table records why each
large surface does not retain unrelated application authority.

## Deletion And Safety Proofs

Focused searches returned zero production/project hits for `RunInternal.h`, `RunRuntimeSettings`,
`ReplayProbeWorld`, `RunRenderPassResources`, mutable `PresentationSettings`, renderer pass-resource
accessors, removed stress/automation contexts, removed Run automation forwarders, or `Run*`/`Run&`
backpointers in the audited owners. `git diff --check` passed. The added-code allocation scan found
no heap/growth API; the only matched added line was a fixed value field in the reset snapshot.

Final validation: `tools\validate_fast.bat` passed in 26.9s, all five interaction
scenarios passed in 15.1s, and `tools\validate_full.bat` passed in 64.3s with the
mandatory CPU umbrella, zero-warning Profile/Debug builds, DX12 InfoQueue errors
= 0 and matching screenshots, standalone physics/handle smoke, and the 44,401-line
varied physics baseline byte-exactly.
