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

## N1 Automation Owner Extraction

Completed on 2026-07-18. `InteractionAutomationController.cpp` fell from 4,131
to 3,081 lines. `InteractionAutomationInputDriver` now owns mouse/key/focus
holds and publishes one synthetic device frame through the normal Input seam.
`InteractionAutomationReportWriter` owns bounded action/assertion/visual/causal
evidence, shared validation-fact calculations, durable-artifact checks, and
final JSON serialization. Its typed `InteractionAutomationReportInputs` borrows
runtime owners synchronously and retains none.

Both new implementation TUs repeat the controller's exact project exclusion:
they compile only for `Automation|x64`. The controller sequences actions and
value results; neither extracted owner accepts a controller reference,
backpointer, callback pack, or unrelated runtime context.

The touched-file comment audit covered 8/8 source-bearing files with zero
deferred. This was a touched-file audit, so no subsystem checklist plan was
required; all four new files contain complete learning headers, the project
filter rule remains explicit, and local ownership/invariant comments are
current.

The first formal `tools\validate_full.bat` attempt stopped in preflight before
build/runtime validation because the four new project items lacked semantic
filter-rule prefixes. The rule was extended with the two owner names and its
direct check passed. The complete rerun then passed in about 2m13s: 282/282
doctests, all ratified coverage floors, the Profile exclusion plus Automation
replay/prediction smoke, zero DX12 validation errors with committed captures,
and the byte-exact 44,401-line physics baseline. Evidence:
`TestOutput/validation/agent_logs/monolith_n1_validate_full_stdout.log`.

## N2 Standalone Physics Cohesion Ruling

Completed on 2026-07-18 against `PhysicsStandaloneWorld` at the current tip.
The fresh field-to-method dependency pass found one state authority rather than
separable body, collider, query, or diagnostics owners:

| State | Mutators | Consumers / invalidation coupling |
|---|---|---|
| `m_bodyStore` | `Clear`, body lifecycle, impulse, `Step`, activation | Collider lifecycle, joint lifecycle, contact/island generation, both spatial queries, every body/collider view, and all body-view cache helpers. |
| `m_colliderStore` | `Clear`, collider lifecycle, body destruction | `Step`, contact/island generation, ray cast, broadphase query, collider views, and body bounding-radius refresh. |
| `m_contacts`, `m_islands`, and island scratch | Cleared by every relevant world mutation and rebuilt by `Step` | `Contacts`/`Islands` return borrowed views whose lifetime is the next world mutation; island generation also reads bodies, colliders, joints, sleep state, and contact rows. |
| Joint rows, generations, liveness, and free list | `Clear`, point-joint lifecycle, body destruction | Island generation and the borrowed joint views share slot identity and tombstone order. |
| Body/collider/joint/query view scratch | Rebuilt by const query/view calls; invalidated by mutations | Projection requires live handle resolution across body, collider, and constraint state; the public lifetime contract is world-wide, not per API domain. |
| `m_sleepEnabled` and `m_nextInitialGeneration` | Activation/clear | Sleep policy is consumed by creation, update, step, contact/island generation, and both queries; the generation seed governs every post-clear handle. |

The owner therefore ratifies the dated cohesion ruling: retain
`PhysicsStandaloneWorld` as the single standalone simulation/state owner.
Partitioning body/collider lifecycle would split generation and destruction
invariants; partitioning queries or diagnostics would require a mutable store
backpointer, wide borrowed context, or forwarding facade. A method-only TU
split would reduce physical line count without moving authority and is
explicitly rejected by this plan. No physics source, behavior, baseline, or
golden changed in N2.

The required `tools\validate_physics.bat` commit gate passed in about 50s:
standalone/runtime handle smoke matched expected state, both builds completed
with zero warnings and zero errors, and `physics_regression_varied.csv` matched
the committed baseline byte-exact at 44,401 lines. Evidence:
`TestOutput/validation/agent_logs/monolith_n2_validate_physics_stdout.log`.

## N3 UI Window-Interaction Owner Extraction

Completed on 2026-07-18. `UI.cpp` fell from 2,517 to 1,240 lines after
`UIWindowInteractionOwner` received the persistent window, widget, tab-input,
pointer-capture, combo, slider, scroll, mini-palette, mouse-override, and cache
state that input mutates. `InGameUI` retains draw composition, draw-command
lists, GPU preview resources, the scene-navigation model, and its borrowed
profiler pointer.

The owner emits the unchanged `InGameUIInputResult` command value. Drawing
borrows one synchronous `UIWindowInteractionOwner::WidgetView` so layout and
hit testing continue to operate on the exact same widget objects; the view is
never retained. The owner has no `InGameUI` pointer/reference, friend edge,
callback pack, `void*`, services/context bag, or unrelated runtime authority.
The stable public `InGameUI` methods delegate state commands to the concrete
owner, where the state and implementation now live.

A normalized source comparison proved every transplanted state/input method
body identical to its pre-N3 implementation after only the class qualifier and
explicit profiler argument changed. The direct Profile product build and the
719-item project/filter parity check passed during iteration. The touched-file
comment audit covered 4/4 source-bearing files (`UI.cpp`, `UI.h`, and the new
owner pair), with zero deferred and no unchecked files; each has a complete
learning header and the new borrowing/authority invariants are documented
beside the boundary. No baseline, golden, screenshot, input schema, command
value, or runtime behavior changed.

The first two `tools\validate_fast.bat` attempts stopped in formatting
preflight before build or tests: the first identified the two moved
implementations, and the second identified the new header's declaration-join
pipeline. Formatting was applied only to those files. The complete final rerun
passed in about 45s: all 255 headers and implementation files were clean,
719/719 production project/filter items matched, the ten staged candidates had
zero size violations, Profile and Debug builds reported zero warnings/errors,
and all 282 doctests (21,389 assertions) passed. Evidence:
`TestOutput/validation/agent_logs/monolith_n3_validate_fast_stdout.log`.

The separate interactive smoke launched the final Profile DX12 binary with
`--frames 360 --ui-stress --ui-stress-seed 1357911 --ui-stress-actions 8`.
It completed the bounded run, captured 393 physics samples, and exited 0 in
5.499s without a crash. Evidence:
`TestOutput/validation/agent_logs/monolith_n3_ui_stress_stdout.log`.

## N4 DX12 Frame And Retirement Owner Extraction

Completed on 2026-07-18 under the N0 parking lift. The existing
`Dx12FrameOwner`, its draw/upload/release capability views, and
`Dx12DeferredReleaseOwner` moved from `RenderBackendDX12.cpp/.h` into
`Dx12FrameOwner.cpp/.h` and `Dx12DeferredReleaseOwner.cpp`. The aggregate
backend still composes these owners and exposes the same renderer API, while
the implementation-only epoch, profiler-stack, upload, and retirement types
now remain behind a DX12-private header.

`RenderBackendDX12.cpp` fell from 3,636 to 2,610 lines and its header from
1,431 to 1,095 lines. The new frame implementation is 941 lines, the private
header 394, and the deferred-release implementation 153. Normalized comparison
against the pre-N4 source proved the 900-line frame/capability body, 124-line
retirement body, and 338-line declaration block identical before formatting.
The `FRAME_COUNT = 2` latency invariant, command-recording transitions, upload
policy, covering-fence rules, public backend methods, project configuration,
baselines, goldens, and screenshots did not change. Neither owner retains an
aggregate-backend pointer or obtains unrelated renderer authority.

The project/filter checker now recognizes both owner-named DX12 prefixes; its
direct check reports 722 project items and 722 filter items with zero errors.
The touched-source comment audit covered 6/6 files with zero deferred or
unchecked: the five DX12 source/header files and the small prefix-map edit in
the checker's existing documented semantic table.

Formal commit-gate evidence from the final staged source:

- `tools\validate_fast.bat` passed in 54.356s: 256 headers/implementations were
  clean, all 722 project/filter items matched, eight staged candidates had
  zero size violations, Profile and Debug built with zero warnings/errors, and
  all 282 doctests / 21,389 assertions passed. Log:
  `TestOutput/validation/agent_logs/monolith_n4_validate_fast_stdout.log`.
- `tools\validate_dx12_renderer.bat` passed in 51.656s: the Profile build and
  DX12 suite succeeded, InfoQueue reported zero validation errors, and all
  three committed screenshot baselines matched. Log:
  `TestOutput/validation/agent_logs/monolith_n4_validate_dx12_renderer_stdout.log`.
- `tools\run_graphics_stress.bat 1` ran the final Profile DX12 binary for the
  bounded minute, stopped PID 45448 through the script's PID timeout, and
  exited 0 in 61.627s without a crash. Log:
  `TestOutput/validation/agent_logs/monolith_n4_graphics_stress_stdout.log`.
