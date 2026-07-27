# Ceremonial Aggregate Elimination

Date: 2026-07-26
Status: IN PROGRESS — CA0 through CA3 closed on 2026-07-27; all 35
authority-free couriers are gone and CA4 is binding.
Originally drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 4 of the Architecture Follow-Up Campaign
Round 5. Starts after `governance-shape-to-judgment-conversion` closes. 4/5
phases complete.
Impact area: `Runtime/Interaction/OperatorCommandApplier.h`,
`Runtime/Scene/SceneRuntime*.h`, `Runtime/Scene/SceneAuthoredSetup.h`,
`Runtime/Scene/SceneGeneratedSetup.h`, `Runtime/Editor/*`,
`Runtime/Diagnostics/*`, `Runtime/Input/*`, `Runtime/Capture/*`,
`Assets/AssetSystem.h`
Owner: runtime
Priority: Medium — no behavior bug, but this is the visible residue of a
governance rule that policed spelling. Every one of these types is read by an
agent as precedent for writing the next one.

## Problem And Evidence (measured 2026-07-26)

`concrete-parameter-bag-elimination` PB0-PB7 closed 30 repair rows on
2026-07-26 and did good work. It did not census the small end. A fresh
whole-tree pass over `SkullbonezSource/` matched 99 aggregate parameter types
(`*Context|*Input|*Inputs|*Params|*Args|*Request|*Facts|*Operands`), of which
six carry exactly one member and fourteen carry two or three.

The clearest offenders, none of which appear anywhere in the PB0 census:

**One member.**

| Type | Site | Sole member |
|---|---|---|
| `TornadoUICommandContext` | `Runtime/Interaction/OperatorCommandApplier.h:78` | `SceneWorld& world` |
| `PhysicsSleepPolicyUICommandContext` | `:87` | `SceneWorld& world` |
| `SceneRuntimeCreateContext` | `Runtime/Scene/SceneRuntimeCreate.h:35` | `SceneController& controller` |
| `SceneAuthoredCameraContext` | `Runtime/Scene/SceneAuthoredSetup.h:64` | — |
| `SceneGeneratedCameraContext` | `Runtime/Scene/SceneGeneratedSetup.h:68` | — |
| `AssetContext` | `Assets/AssetSystem.h:190` | — |

The first two are *distinct types wrapping the same single reference*,
distinguished only by name and comment. `SceneRuntimeCreateContext` is passed
**by value** into `CreateSceneFromUI( SceneRuntimeCreateContext, const char* )`
(`SceneRuntimeCreate.h:40`), a signature that is strictly worse than
`CreateSceneFromUI( SceneController&, const char* )`: it is longer to write, it
adds a type to read, and it shortens nothing.

**Two or three members**, same character: `PhysicsFrictionUICommandContext`
(`OperatorCommandApplier.h:95`), `RenderDeviceUICommandContext` (`:212`),
`SceneFixedStepUICommandContext` (`:221`), `SceneSimpleRagdollAppendContext`
(`SceneAuthoredSetup.h:76`), `SceneAuthoredModelContext` (`:69`),
`SceneRuntimeUiOptionsContext` (`SceneRuntimeUiOptions.h:57`),
`EditorGizmoContext` (`Runtime/Editor/EditorTools.h:140`),
`RuntimeAfterUiDismissInput` (`Runtime/Input/InputRouter.h:129`).

**Four to seven members** where the aggregate is a union of one operation's
needs rather than an owned invariant: `SceneRuntimeStyleContext`
(`Runtime/Scene/SceneRuntimeStyle.h:48`, seven unrelated references),
`RuntimePresentationUICommandContext` (`OperatorCommandApplier.h:104`, seven),
`CinematicUICommandContext` (`:119`), `RunSimulationUICommandContext` (`:172`),
`EditorPlacementPreviewContext` (`EditorTools.h:99`),
`EditorObjectPlacementContext` (`:107`), `EditorInteractionPreviewContext`
(`Runtime/Editor/EditorOverlayTools.h:74`), `EditorToolOverlayTraceContext`
(`:98`), `RuntimeCaptureSceneContext` (`Runtime/Capture/CaptureSystem.h:74`),
`DiagnosticsKeyboardShortcutContext` and
`DiagnosticsUIKeyboardShortcutContext` (`Runtime/Diagnostics/DiagnosticsRuntime.h:62,77`),
`RuntimePerfTickContext` (`Runtime/Diagnostics/RuntimeDiagnostics.h:153`),
`RuntimeCameraInputFrameContext` (`Runtime/Input/InputController.h:267`),
`SceneGeneratedModelContext` (`SceneGeneratedSetup.h:73`),
`SceneGeneratedPopulationRequest` (`:81`), `ReplayPastRootRebuildContext`
(`Runtime/Prediction/ReplayPredictionPublicationOperations.h:50`),
`RuntimeViewModelContext` (`Runtime/UI/RuntimeViewModel.h:62`).

The repository's own Invariant Ownership Rule already condemns these: an
aggregate that "only carries data to shorten a signature is an authority-free
bag and remains banned." The rule was never applied at this size because the
enforcement instrument looked for wide signatures and named service bags.

## Goal

An aggregate under `SkullbonezSource/` exists because it owns an invariant its
header names and a test exercises, or because it is a genuine domain value with
independent meaning. Nothing exists to make a call site look tidier.

## Non-Goals

- No re-decision of PB0 `Explicit Retain Ruling` rows
  (`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md:82-99`)
  except the two behavior-free borrowed-member couriers reopened by the
  independent governance G4 review: `ShadowGraphInputs` and
  `ReflectionGraphInputs`.
  `RenderResourceContext`, `PrimitiveRenderContext`,
  `RuntimeRenderer::FrameEntryContext`, `ReplayOverlayStateView`,
  `ReplayWorkspaceFrameInput`, `ReplayWorldPointerInput`,
  `ReplayStartupLoadInput`, `ReplayPathPickInput`, `RuntimePickRequest`, the four
  Editor/Launcher pointer command values, the remaining eight multi-member
  `*GraphInputs`, and the six non-UI `*PassInputs` are ruled and carried forward
  untouched.
- No scope on `RuntimeRenderBackendView` or the four `RuntimeFrame*View`
  structs. Those are owned by `render-backend-service-bag-removal` and
  `runtime-frame-view-retirement` respectively; the owner scoped this plan to
  the small aggregates at registration.
- No scope on the `OperatorCommandApplier` *result* structs or its apply
  operations. `operator-command-invariant-ownership` owns the operation-level
  question; this plan deletes only the context inputs. The seam between the two
  plans is explicit: this plan may not restructure an apply operation, only
  change what it receives.
- No behavior change. No physics, render, replay, or UI output moves.
- No replacement aggregate under a different suffix. Deleting
  `SceneRuntimeStyleContext` and introducing `SceneRuntimeStyleOperands` is a
  closure failure.
- No parameter list may exceed the accepted 12-parameter ceiling. Where deleting
  an aggregate would breach it, the operation is decomposed instead — that is the
  signal the aggregate was concealing a wide operation.

## Phases

- [x] **CA0 — Census and rule every matched aggregate.**
  Produce the complete current-tip table for every mechanically signalled
  borrowed-member aggregate plus the legacy-suffix review context: type,
  file:line, member count, every construction site, every consumer site, whether
  the consumer destructures immediately, and a verdict of `remove`,
  `retain` (with the invariant it owns), or `retain-prior` (with the PB0 or GV1
  ruling that already covered it). Record for every `remove` row the resulting
  parameter count so ceiling breaches are known before implementation, not
  discovered during it. Seed and then update
  `tools/aggregate_ownership_rulings.json` from
  `governance-shape-to-judgment-conversion` G2. Acceptance: no row unruled; every
  `remove` row names its post-removal signature; the inventory tool reproduces
  the table exactly. Closed 2026-07-27: the suffix-independent inventory found
  1,207 data-bearing types and emitted 119 complete review rows (10 mechanically
  signalled couriers plus the legacy suffix context), with 35 `remove`, 62
  `retain`, 22 `retain-prior`, and zero unruled or stale rows. All 35 removal
  endpoints stay at or below 10 parameters; the return/field substitutions do
  not raise arity. Evidence:
  `../../Reports/2026-07-27/ceremonial-aggregate-elimination-ca0-summary.md`
  and the generated complete table
  `../../Reports/2026-07-27/ceremonial-aggregate-elimination-ca0-census.md`.

- [x] **CA1 — Remove the UI command context family.**
  Delete `TornadoUICommandContext`, `PhysicsSleepPolicyUICommandContext`,
  `PhysicsFrictionUICommandContext`, `RuntimePresentationUICommandContext`,
  `CinematicUICommandContext`, `RunSimulationUICommandContext`,
  `RenderDeviceUICommandContext`, and `SceneFixedStepUICommandContext` from
  `Runtime/Interaction/OperatorCommandApplier.h`. Each apply operation takes the
  real owners it uses. Where that breaches the ceiling, leave the operation
  wide-but-legal and record the row for
  `operator-command-invariant-ownership` rather than inventing a replacement
  type. Acceptance: `rg -n 'UICommandContext' SkullbonezSource SkullbonezTests`
  returns no rows; every UI-tab command still applies with identical observable
  effect; focused operator-command tests pass. Closed 2026-07-27: all eight
  context types were deleted, their operations now expose concrete owners
  directly, and the widest resulting signature has 10 parameters. The literal
  source/test search returns no rows, the inventory reports 1,199 candidates
  with all 111 review rows ruled, and the focused compile-time contract plus
  complete 416-case unit suite pass. Evidence:
  `../../Reports/2026-07-27/ceremonial-aggregate-elimination-ca1-ui-contexts.md`.

- [x] **CA2 — Remove the Scene setup and runtime context family.**
  Delete `SceneRuntimeCreateContext`, `SceneRuntimeUiOptionsContext`,
  `SceneRuntimeStyleContext`, `SceneAuthoredCameraContext`,
  `SceneAuthoredModelContext`, `SceneSimpleRagdollAppendContext`,
  `SceneGeneratedCameraContext`, `SceneGeneratedModelContext`, and
  `SceneGeneratedPopulationRequest`. `SceneRuntimeStyleContext`'s seven
  references belong to distinct owners; its three consumers
  (`ApplyCinematicModeFromBrowserIndex`, `ApplyLiveStyleScene`,
  `ApplyDemoHeroStyleOverride`) each use a subset and must take only that subset.
  Do not disturb the `SceneLoadTransaction` or
  `SceneGeneratedControlTransaction` established by GV2/GV3 — they are ruled
  invariant owners. Acceptance: the nine types are gone with no replacement
  aggregate; scene load, generated-demo population, live-style application, and
  cinematic mode selection are behaviourally identical; scene snapshot and
  lifecycle tests pass; physics CSV byte-exact. Closed 2026-07-27: all nine
  types were deleted, setup/style helpers now expose only their concrete
  operands, and generated population uses an eight-parameter endpoint with a
  domain enum rather than a replacement aggregate. The complete unit suite
  passes 416/416, the Profile application builds with zero errors, and
  `validate_physics.bat` passes without a baseline change. Evidence:
  `../../Reports/2026-07-27/ceremonial-aggregate-elimination-ca2-scene-contexts.md`.

- [x] **CA3 — Remove the Editor, Diagnostics, Input, Capture, Assets, Render,
  and Replay remainder.**
  Delete every remaining CA0 `remove` row: `EditorGizmoContext`,
  `EditorPlacementPreviewContext`, `EditorObjectPlacementContext`,
  `EditorInteractionPreviewContext`, `EditorToolOverlayTraceContext`,
  `RuntimeAfterUiDismissInput`, `RuntimeCameraInputFrameContext`,
  `DiagnosticsKeyboardShortcutContext`, `DiagnosticsUIKeyboardShortcutContext`,
  `RuntimePerfTickContext`, `RuntimeCaptureSceneContext`,
  `RuntimeViewModelContext`, `ReplayPastRootRebuildContext`, `AssetContext`,
  `ShadowGraphInputs`, `ReflectionGraphInputs`, `RenderReplayOverlayView`, and
  `ReplayTimelineCaptureResult`.
  Editor pointer *command* values ruled `retain` by PB0 stay. Acceptance: the
  CA0 `remove` set is empty at re-run; editor placement/gizmo/trace behavior,
  diagnostics hotkeys, camera input, capture, and asset resolution are unchanged;
  DX12 baselines unchanged.
  Closed 2026-07-27. All 18 rows were deleted without replacement; the
  inventory now reports 1,172 candidates, 84 ruled review rows, zero signalled
  rows, and zero unruled rows. The 416-case unit suite, DX12 renderer gate, and
  one-minute graphics stress run pass without a baseline update. Evidence:
  `../../Reports/2026-07-27/ceremonial-aggregate-elimination-ca3-remainder.md`.

- [ ] **CA4 — Reconcile, review, and hand off.**
  Re-run the CA0 census and the G2 inventory at final source. Every surviving
  aggregate must carry either a named owned invariant in its header or a
  `retain`/`retain-prior` ruling. Complete the comment audit for every touched
  file — headers that described the deleted types must be corrected in the same
  commit, per the Comment Quality Gate. Obtain one independent no-bag review
  asking specifically: did any deleted aggregate reappear under another suffix,
  and did any operation exceed the ceiling. Acceptance: review clear; inventory
  shows zero unruled rows; `validate_full.bat` passes with no physics, DX12,
  replay, or schema baseline change.

## Dependencies And Decisions

- Depends on `governance-shape-to-judgment-conversion` for the G1 rule text and
  the G2 inventory/ruling file.
- Feeds `operator-command-invariant-ownership`: CA1 hands it any operator-command
  operation left wide by context deletion.
- Owner decision ratified at registration: scope is the small aggregates only.
  The frame views, the render backend view, and the extraction scars are separate
  plans.

## Acceptance

- Every one-member aggregate under `SkullbonezSource/` is deleted or has a ruled
  invariant.
- No deleted aggregate is replaced by a renamed equivalent.
- No operation exceeds 12 parameters.
- Zero behavior change across physics, render, replay, UI, and scene load.

## Validation

- `tools\validate_tests.bat` — focused operator-command, scene lifecycle, editor,
  and diagnostics coverage.
- `tools\validate_physics.bat` — CA2 touches scene-load physics coordination.
- `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` —
  CA1/CA3 touch renderer command application.
- `tools\validate_full.bat` — `Runtime/*` changed; required at the closure gate.
