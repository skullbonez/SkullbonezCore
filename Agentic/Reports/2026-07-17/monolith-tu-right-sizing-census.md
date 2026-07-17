# Product Translation-Unit Right-Sizing Census And Rulings

Date: 2026-07-17
Branch: `nightrunner-17th-july`
Source inspected: `9499679f526f2f7549a99bc631d81ee4a9098374`
Plan: `Agentic/Plans/TODO/monolith-tu-right-sizing.md`
Status: N0 owner-ratified; N1 is released.

## Complete Current-Tip Inventory

The inventory was regenerated from tracked `SkullbonezSource/**/*.cpp` files,
not from the plan's historical seven-file list. Sixteen product translation
units are currently above 2,000 physical lines. The Automation-only controller
is still product/validation code and is included because it is compiled into
the `Automation|x64` product configuration.

| Lines | Translation unit | Responsibility evidence | Proposed N0 disposition |
|---:|---|---|---|
| 4,444 | `Runtime/Replay/ReplayPrediction.cpp` | One single-writer prediction owner; local future-tree, worker-build, publish-prefix, archive, reveal, and trajectory helpers all mutate or validate the same `RunReplayPredictionState`. | Retain cohesive under N6 unless implementation discovers a stateful sub-owner with no reach-back. Preserve prior R6 ruling and single-writer/published-prefix protocol. |
| 4,131 | `Runtime/InteractionAutomationController.cpp` | Lines 1-2822 contain parse/probe/report helpers; public entry points at 2823-4131 combine input injection, before-input sequencing, after-render capture, and report emission. | Split. Extract stateful `InteractionAutomationInputDriver` and `InteractionAutomationReportWriter`; preserve Automation-only link exclusion. Controller sequences typed value results only. |
| 3,636 | `Rendering/DX12/RenderBackendDX12.cpp` | Existing concrete `Dx12FrameOwner` implementation spans 130-1026; deferred release is 1241-1361; backend graph transient, device init, present, shutdown, resize, and fixed-function state follow. | Lift Round-3 parking. Move the already-concrete frame and deferred-release owners to owner-named TUs; move implementation-only types to a private header and keep the backend as device/present composition. |
| 3,492 | `Runtime/Replay/ReplayRecorder.cpp` | Shared bit/order/hash helpers precede presentation, solver, and event recorder methods. Presentation and solver paths intentionally mirror byte-order mechanics; event commands feed the same artifact provenance. | Retain cohesive under N5 unless the fresh dependency proof shows an existing recorder owner can move without a new internal API, duplication, or byte/order split. Prior R6 ruling remains binding. |
| 2,917 | `Runtime/Replay/ReplayV2Artifact.cpp` | One V2 artifact codec owns schema validation, table decoding, canonical hashing, and writer order. | Dated cohesion ruling: retain. Splitting reader/writer helpers would duplicate the version/table/hash contract and create a codec bridge. |
| 2,517 | `UI/UI.cpp` | Window/state commands are 69-638, `UpdateInput` is 639-1694, and draw composition is 1695-2516. Tab-specific drawing already lives in owner-named tab TUs. | Split. Move window/input capture, combo/drag state, and `UpdateInput` into a state-owning `UIWindowInteractionOwner`; `InGameUI` retains composition and tab owners. No owner may take an `InGameUI&` reach-back. |
| 2,384 | `Runtime/Render/RuntimeRenderPasses.cpp` | Implements the existing concrete sky, reflection, scene, water, shadow, tornado, debug, and post pass owners declared in `RuntimeRenderPasses.h`. | Dated cohesion ruling: retain as the implementation collection for already-separated stateless/pass-state owners; no shared bag or root reach-back is introduced. |
| 2,328 | `UI/UITabProfiler.cpp` | One Profiler-tab owner couples timeline layout, hit testing, markers, worker lanes, and labels. | Dated cohesion ruling: retain; layout/hit-test constants and tab-local state must remain together. |
| 2,337 | `Runtime/Render/RuntimeRenderer.cpp` | Concrete runtime render coordinator sequences existing pass owners and typed graph values; resource state lives in those owners. | Dated cohesion ruling: retain; another split would be a method-only TU partition of one pass-order coordinator. |
| 2,226 | `Physics/PhysicsApi.cpp` | `PhysicsStandaloneWorld` owns fixed lifecycle arrays, contact/island generation, activation, queries, cached views, and the standalone smoke boundary. | Dated cohesion ruling under N2: retain one standalone-world owner. Its state/invariants cross lifecycle, step, query, and smoke methods; a domain split would expose mutable store internals. |
| 2,154 | `Runtime/Editor/RunEditorTools.cpp` | Concrete `RuntimeTools` editor/manipulator/launcher orchestration over already-separated editor owner types. | Dated cohesion ruling: retain pending any new stateful owner evidence; no line-only split. |
| 2,129 | `Runtime/Audio/ContactAudioService.cpp` | One contact-audio service owns candidate classification, deduplication, budgets, rolling/burst voices, and diagnostics. | Dated cohesion ruling: retain; all paths share one bounded voice/candidate budget and cooldown state. |
| 2,098 | `Physics/PhysicsBodyStore.cpp` | One fixed-capacity SoA body store owns handle generation, row swap/remap, hot spans, replay restore, and view invariants. | Dated cohesion ruling: retain; splitting lifecycle from spans would divide the handle/row invariant. |
| 2,076 | `Runtime/Replay/ReplayPredictionDrawing.cpp` | Prediction-only drawing/overlay projection consumes the published immutable view and owns no prediction mutation. | Dated cohesion ruling: retain as the presentation responsibility already extracted from `ReplayPrediction`; further division is mechanical. |
| 2,026 | `Runtime/Editor/RunEditorPlacementAssets.cpp` | One editor placement-assets owner builds registered asset recipes and deterministic placement transactions. | Dated cohesion ruling: retain; asset construction and placement validation share the same authored recipe boundary. |
| 2,018 | `Physics/ObjectContactManifold.cpp` | One narrowphase manifold algorithm owns feature selection, clipping, persistence matching, and deterministic contact ordering. | Dated cohesion ruling: retain; separating stages would fracture one arithmetic/order-sensitive algorithm. |

`Rendering/DX12/RenderBackendDX12.h` is 1,431 lines and below the TU threshold,
but N4 includes it because implementation-only helper types make the backend's
public surface materially larger. Its private-type shrink remains mandatory.

All 16 rows are present in their owning product project. The three physics TUs
compile through `SKULLBONEZ_PHYSICS.vcxproj`; the other 13 compile through
`SKULLBONEZ_CORE.vcxproj`. `InteractionAutomationController.cpp` is excluded
from every configuration except `Automation|x64`; no other row has a project
exclusion.

## Line-Range Responsibility Map

Ranges below are current-tip physical lines and are the evidence boundary N7
must refresh after implementation.

| Translation unit | Current responsibility clusters |
|---|---|
| `ReplayPrediction.cpp` | 1-1234 shared prediction/build utilities; 1235-2283 publish/readiness, draw-window and retained-marker policy; 2284-3126 future-node and activation preparation; 3127-3902 worker/single-writer build; 3903-4444 public update, presentation, archive, reveal, cancellation, trajectory, and memory operations. |
| `InteractionAutomationController.cpp` | 1-2822 script parsing, targeting, probes, capture and report helpers; 2823-2882 input reset/config/result boundary; 2883-4131 before-input injection, sequencing, after-render probes/captures and final report emission. |
| `RenderBackendDX12.cpp` | 1-129 local helpers; 130-1084 concrete frame owner; 1085-1240 backend setup and submission; 1241-1366 concrete deferred-release owner; 1367-1978 backend graph transitions/transient materialization; 1979-2682 device/init/depth setup; 2683-3425 shutdown/present/finish/resize; 3426-3636 fixed-function state facade. |
| `ReplayRecorder.cpp` | 1-1802 canonical byte/order/hash and capture helpers shared by recorder paths; 1803-2213 configuration, reset, frame/solver capture, hash-log and memory state; 2214-2537 chronological sample/ring payload, keyframe/checkpoint and hash-row mechanics; 2538-3492 solver/event recorders over the same artifact provenance. |
| `ReplayV2Artifact.cpp` | 1-2917 one codec boundary: version/table validation, canonical hash, reader reconstruction and writer ordering. |
| `UI.cpp` | 1-638 window/widget state commands and hitbox overlay; 639-1694 input capture, hit testing, combo/slider/scroll and typed input result; 1695-2517 draw composition delegating to tab owners. |
| `RuntimeRenderPasses.cpp` | 1-2384 implementations of the already-distinct sky, reflection, scene, water, shadow, tornado, debug and post-pass owners. |
| `UITabProfiler.cpp` | 1-2328 one profiler-tab boundary coupling timeline layout, marker/worker rows, hit testing and labels. |
| `RuntimeRenderer.cpp` | 1-2337 one pass-order/resource coordinator over concrete render-pass owners and typed graph values. |
| `PhysicsApi.cpp` | 1-424 standalone storage/helpers; 425-745 body/collider/constraint lifecycle; 746-1182 step, contact/island generation and activation; 1183-1404 queries/views; 1405-1546 cache/record helpers; 1547-2226 standalone smoke scenarios and result reporting. |
| `RunEditorTools.cpp` | 1-2154 one `RuntimeTools` orchestration boundary over separately-owned editor, manipulator and launcher types. |
| `ContactAudioService.cpp` | 1-2129 one bounded candidate/voice/cooldown owner spanning classification, deduplication, playback and diagnostics. |
| `PhysicsBodyStore.cpp` | 1-2098 one fixed-capacity SoA store spanning handle lifecycle, row remap, hot views, replay restore and invariant validation. |
| `ReplayPredictionDrawing.cpp` | 1-2076 immutable published-prediction presentation, overlay projection and draw preparation; no prediction mutation. |
| `RunEditorPlacementAssets.cpp` | 1-2026 registered-asset recipe construction and deterministic editor placement transactions. |
| `ObjectContactManifold.cpp` | 1-2018 one arithmetic/order-sensitive manifold algorithm spanning feature selection, clipping, persistence and contact ordering. |

## Named-Task Target Map

| Task | Ratified target requested |
|---|---|
| N1 | `InteractionAutomationInputDriver` owns injected mouse/key/release state and emits typed frame input; `InteractionAutomationReportWriter` owns report serialization/capture rows. The controller sequences them without callback packs or an owner backpointer. |
| N2 | Record current-tip cohesion for `PhysicsStandaloneWorld`; no source split unless a new state boundary is proved during implementation. |
| N3 | `UIWindowInteractionOwner` receives the window, pointer-capture, combo, slider, scroll, palette-press, and mouse-override state it mutates. It returns `InGameUIInputResult` and typed widget facts; it never reaches back into `InGameUI`. |
| N4 | Explicitly lift the Round-3 DX12 re-partition parking. Move existing `Dx12FrameOwner` and `Dx12DeferredReleaseOwner` implementations to owner-named files and move their implementation-only declarations behind a DX12-private header. Preserve two-frame buffering. |
| N5 | Preserve the R6 replay-recorder cohesion ruling unless new dependency evidence proves a stateful extraction without an internal compatibility API, duplicated encoding, or byte/order change. A fresh report and the one allowed mega-gate invocation still close the task. |
| N6 | Preserve the R6 replay-prediction cohesion ruling unless a stateful sub-owner can move with the single-writer/published-prefix protocol intact and without reach-back. A fresh report and the one allowed mega-gate invocation still close the task. |

## Execution And Validation Rulings

1. Ratify branch `nightrunner-17th-july`.
2. Ratify the complete 16-file inventory; N7 must rerun the same tracked-file
   census and give every final `>2,000`-line TU an extraction or dated cohesion
   disposition.
3. Ratify the DX12 parking lift only for N4's existing concrete owner split and
   private-header shrink. No frame-buffering or new render architecture work is
   authorized.
4. Preserve Automation link exclusion, allocation allowlist ownership, replay
   byte/order/schema contracts, and zero baseline/golden refresh.
5. Run the exact per-task gates from the plan. N5 and N6 each consume one and
   only one replay mega-gate invocation. N4 carries renderer validation and a
   measured crash-free stress run.

The owner ratified these rulings and branch on 2026-07-17. N0 is
documentation-only, so no repository validation is required; N1 is released.
