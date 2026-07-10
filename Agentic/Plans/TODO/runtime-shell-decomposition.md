# Runtime Shell Decomposition

Date: 2026-07-10 (reconciled)
Status: In progress — 0/26 remaining checklist items complete; earlier
foundation work is summarized separately and is not mixed into this count
Impact area: runtime architecture, scene lifecycle, input routing, render host
Owner: application composition root

## Goal

`Run` is a thin application shell: process lifetime, startup/shutdown, Win32
message pumping, top-level frame order, final owner wiring, and exit reporting.
A scene, replay, render, input, tool, or diagnostics feature lands inside its
owning subsystem without adding a `Run::*` method or a callback into `Run`.

This goal applies to the logical `Run` object: `Run.h`, every `Run*.cpp`, shared
headers, helper/context types, and forwarding facades are one review surface.
A short `Run.cpp` does not satisfy the goal while authority survives in those
other files.

Measured reality (2026-07-10, tracked engine/shader source-bearing files):
`Runtime/` is 82,359 lines — about 48% of 172,036 lines. The earlier split
produced 16+ `Run*.cpp` translation units sharing private state through
`RunInternal.h`; 27 files now include that header. This is partial-class
emulation, not ownership transfer. Current mega-TUs include `RunInput.cpp`
3,140, `Init.cpp` 3,495, `RunPasses.cpp` 2,333, and `RunRender.cpp` 2,323.
Cross-plan mega-files include `RunReplayTools.cpp` 4,779,
`TestSceneParser.cpp` 3,651, and `UI.cpp` 3,381.

## Design Rules

1. Preserve behavior; extraction commits are small and revertible.
2. Split by ownership of state and invariants, not file length.
3. No stored `Run&`/`Run*`, `void*` user hooks, or broad host bags in extracted
   systems.
4. Use concrete owners and value records; do not add callback or inheritance
   seams to hide the same coupling.
5. Each phase deletes a named `Run` method/state surface and adds behavioral
   evidence for the moved boundary.

## Five Concrete Ownership Extractions From `Run`

A mechanical file move does not complete a row. State, invariants, and command
authority must move to the named owner, and the deletion proof must pass.

| # | Ownership extraction | Move out of `Run` | Durable API and state owner | Deletion proof | Required evidence |
|---|---|---|---|---|---|
| 1 | **Input routing → `InputRouter`** | `TakeInput`, blocked/unfocused handling, post-UI keyboard dispatch, pointer-camera routing, repeated hardware polls, and large callback/context construction | `BeginFrame(const DeviceInputFrame&, const PreUiInputFacts&, InputActions&)` before UI and `CompleteFrame(const UiInputHitSnapshot&, const RuntimeInteractionFramePolicy&, InputActions&)` after UI; it owns fixed edge/presentation state, while `RuntimeInteractionController` remains gesture/workspace authority | `Run.h` has no `TakeInput`, `Dispatch*Keyboard*`, or pointer-routing methods; no input callback receives `void*`/`Run*`; no later frame phase polls hardware directly | CPU router/interaction-policy tests, interaction-click automation, perf, then full gate |
| 2 | **Command authority → owner-specific queues** | `DrainRuntimeCommands` and the mixed scene/capture/defaults/quit switch | Fixed bounded queues owned by `SceneController`, `CaptureController`, and `RenderDefaultsStore`, plus value-only `ApplicationExitState`; each owner returns a typed batch result and replay receives only accepted events with explicit wire codes | No central switch names scene, screenshot, defaults, and replay logging together; dead `AdvanceScene`/`Quit` types and generic runtime-command vocabulary are deleted | CPU exit/queue/order/overflow tests plus interaction and full gates |
| 3 | **Scene lifecycle → promoted `SceneController`** | `LoadScene`, reset/preserve-state orchestration, browser refresh, defaults, adjacent/deck movement, and lifecycle callback lambdas | `SceneController` owns queue, browser, lifecycle state, explicit `BeforeSceneUnload`…`AfterSceneActivated` events, and `Load(const SceneLoadRequest&) -> SbResult` | `Run.h` has no scene-load/reset/default business methods; `SceneController.cpp` is no longer a pass-through facade | Parser/round-trip tests, full gate, physics determinism |
| 4 | **Replay workspace → existing `ReplayRuntime`** | `TickReplayScrubberInput`, cause-tree/velocity/prediction input, inspection-camera decisions, replay overlays, restore/hash/probe coordination | `ReplayRuntime::TickWorkspace(const ReplayWorkspaceInput&, ReplayWorkspaceOutput&)` consumes typed UI actions and emits camera requests, owner commands, and fixed-capacity draw records | `Run.h` has no `TickReplay*`, `RenderReplay*`, replay restore/hash business method, or replay camera-transition method | CPU replay tests, replay scrub, interaction proofs, allocation evidence |
| 5 | **Render composition → existing `RuntimeRenderer`** | `BuildRuntimeRendererBindings`, backend-resource release/rebuild logging, editor/replay overlay hook lambdas, and pass-level texture callbacks | `RuntimeRenderer` receives immutable `RenderWorldView`, `RenderSceneView`, `RenderReplayOverlayView`, `RenderToolOverlayView`, and `RenderUiView`; owners build draw records before submission | `Run.cpp` contains no C-style render hook, `void*` user pointer, or callback reading `Run` private members | DX12 architecture tests, renderer gate, then full gate |

These are durable domain boundaries, not migration wrappers. `InputRouter` owns
input orchestration because raw/semantic/UI ordering is one invariant; it is not
a compatibility forwarding layer. Its deletion condition is replacement by an
equivalent input owner, not completion of this migration. The existing scene,
replay, and render owners graduate only when their old `Run` surfaces are
deleted and the named behavioral evidence passes.

### Extraction Sequence

1. Land `validation-gate-integrity.md` V1/V2.
2. Land the input snapshot/router core and application-exit result first, then
   migrate keyboard actions into owner-specific queues without recreating a
   central switch.
3. Promote `SceneController`; physics creation/reset work consumes this boundary.
4. Move replay workspace behavior with the replay and UI plans.
5. Finish render composition after overlay producers no longer call `Run`.

### Landed Foundation (not checklist completion)

The 2026-07-10 foundation commit adds allocation-free `InputRouter` and
`ApplicationExitState` owners plus direct CPU coverage for key ordering,
press/hold/release, inactive contexts, focus cancellation/resynchronization,
bounded diagnostics, nonzero platform exit codes, and first-failure
precedence. `tools\validate_fast.bat`, the project-filter validator, and
`tools\validate_full.bat` passed from that source with zero warnings, zero DX12
validation errors, matching screenshots, and byte-exact physics output.

No B1/B2 checkbox is closed by the foundation alone. B1a-B1c and B2a require
production capture/wiring, old callback/state deletion, and their named
behavioral evidence before their deletion proofs are true.

## Remaining Work

### A. Narrow the render host

- [ ] A1. Implement ownership extraction 5: replace `RuntimeRenderHostBindings`
  with the five immutable views and move replay/tool overlay production to owners.
- [ ] A2. Put texture lookup/select behind the asset/render view; no pass-level
  call back into `Run` for texture handles.

### B. Move input, command, tool, and replay decisions

- [ ] B1a. Characterize press/hold/release/repress, simultaneous keys, binding
  contexts, pre/post-UI phases, UI refusal, focus loss, and quick-tap behavior
  in pure CPU tests before moving producers.
- [ ] B1b. Capture one immutable, fixed-size `DeviceInputFrame` per frame. It
  owns the 256-key bitset, buttons, client pointer, wheel, raw mouse delta, and
  focus state; automation mutates that value rather than a second key path.
- [ ] B1c. Move key-edge memory and binding-context enforcement into the
  two-phase `InputRouter`; emit fixed ordered action records and delete
  `MappedKeyboardDispatchContext` plus its callback pack.
- [ ] B1d. Publish one immutable post-UI hit/pointer snapshot. Delete duplicate
  UI/replay/editor button memories and make all consumers observe the same
  position and edge values for a frame.
- [ ] B1e. Give one owner focus cancellation, cursor requests, and native mouse
  capture. Reconcile UI, replay, editor, and camera capture on focus loss.
- [ ] B1f. Delete direct `Input::Is*`/mouse-position polling from later frame,
  physics, render, editor, and replay phases; complete extraction 1's `Run`
  method/state deletion proof.
- [ ] B2a. Add value-only `ApplicationExitState`. Preserve the first owned
  Lane R failure, translate nonzero OS quit codes into failure, and prevent a
  later normal quit from overwriting failure evidence.
- [ ] B2b. Move input-triggered capture into a fixed `CaptureController` queue;
  validate bounded paths before enqueue and preserve post-render automation
  timing until its direct sink is retired deliberately.
- [ ] B2c. Move ordinary/cinematic persistence into `RenderDefaultsStore`,
  convert writers to `SbResult`, and observe final frame-mutated values at its
  named checkpoint. Defaults are not application commands.
- [ ] B2d. Move scene queue storage and submission vocabulary into
  `SceneController`; temporarily retain a scene-only execution drain in `Run`
  until C1 supplies concrete load/save authority.
- [ ] B2e. Replay records only accepted owner events with explicit stable event
  codes. Failed/rejected work and raw domain-enum ordinals are never serialized.
- [ ] B2f. Close extraction 2 with C1: move scene execution to
  `SceneController`, then delete `DrainRuntimeCommands`, the generic queue/type,
  dead zero-producer cases, scene `void*` callbacks, and all remaining owner
  bypasses.

### C. Scene lifecycle ownership

- [ ] C1. Implement ownership extraction 3: `SceneController` owns the
  preallocated scene/entity metadata store, scene-lifetime `PhysicsScene`,
  load/reset state, browser selection, adjacent load, and deck movement.
- [ ] C2. Replace `Run` scene callbacks with explicit `BeforeSceneUnload`
  through `AfterSceneActivated` lifecycle events consumed by concrete owners.
- [ ] C3. Move scene save/load orchestration behind `SceneController` and delete
  `Run`/`GameModelCollection` scene business wrappers. The writer consumes a
  borrowed owner view, never `Run` callbacks or collection-order identity.

Coordinate C with `physics-authority-and-identity.md` C0-C5 and
`Agentic/Reports/scene_asset_roundtrip_design_20260710.md`.

### D. Mega-TU decomposition

- [ ] D1. Split `RunInput.cpp` only as ownership extraction 1 lands; do not move
  the same code twice.
- [ ] D2. Split `TestSceneParser.cpp` by schema domain
  (bodies/assets/groups/water/cameras). It already uses nlohmann JSON; this is
  schema/ownership decomposition, not a JSON-library replacement.
- [ ] D3. Convert shared `.inl` composition to real translation units as owners
  move; pair hot-path conversions with perf validation.

### E. Collapse compatibility surfaces

- [ ] E1. Retire `RunInternal.h`; constants/helpers move to a single owner and
  no sibling `Internal` header replaces it.
- [ ] E2. Delete `Run::*` wrappers that only forward to a subsystem.
- [ ] E3. Slim `Core/Common.h`: remove the stale config compatibility include
  and alias includes according to current consumers.

### F. Prove the god object is gone

- [ ] F1. Rebuild the final `Run` ownership inventory from current source. Map
  every remaining `Run` method and mutable field to one of the five permitted
  shell responsibilities (owner construction/wiring, startup/shutdown, OS
  message pump, top-level frame order, final exit reporting), or move it to a
  concrete domain owner. Inspect all `Run*.cpp`, not only `Run.cpp`.
- [ ] F2. Audit the extracted owners and their boundary records for sideways
  migration. `InputRouter`, command owners, `SceneController`, `ReplayRuntime`,
  and `RuntimeRenderer` must not retain `Run*`/`Run&`, callback bags, `void*`
  contexts, friend backdoors, broad mutable contexts, forwarding-only APIs, or
  authority over unrelated domains.
- [ ] F3. After every other runtime-shell item and required gate passes, run one
  independent read-only adversarial ownership review. Record concrete evidence
  for zero remaining god-object or disguised shared-state-hub findings in
  `Agentic/Reports/<date>/runtime-shell-final-ownership-review.md`, including
  the final method/field inventory and inspected substitute-hub surfaces. Any
  credible finding reopens its owning item, is fixed in this plan, and blocks
  closure rather than becoming optional follow-up debt.

## Binding And Open Decisions

| Decision | Binding answer or remaining question |
|---|---|
| Input/UI ordering | **Binding:** sample `DeviceInputFrame` once; run `InputRouter::BeginFrame`; UI then publishes one immutable `UiInputHitSnapshot`; run `CompleteFrame`; later phases consume values only. |
| Command ordering | **Binding:** at the unconditional pre-simulation checkpoint, return an existing owned failure, persist render defaults, accept input-triggered capture, accept at most one scene transition, then apply a normal exit. Commands produced after the checkpoint run on frame N+1. Encode this in CPU tests. |
| Scene load contract | Which state survives reset/load and which lifecycle event clears interaction, replay, diagnostics, and camera state? |
| Fixed-step ownership | `SimulationSystem` remains timestep owner; do not recreate a generic simulation facade. Decide only which frame coordinator calls it. |

## Mapping Evidence And Defects To Preserve

The 2026-07-10 call-site audit found 11 independent pointer-position reads,
repeated late key polling in frame/render code, binding contexts recorded but
not generically enforced, and four competing native-capture authorities. The
input extraction is incomplete until those direct consumers are deleted; a
file move around `TakeInput` is not acceptance.

The same audit found that nonzero `WM_QUIT` values are currently discarded:
capture/window failures post exit code 1, `Run::Execute` breaks, then returns
success. It also found zero producers for generic `AdvanceScene` and `Quit`
commands, swallowed file-write results, replay recording attempted rather than
accepted work, and queue overflow without owner/high-water/phase diagnostics.
B2 tests must prove each corrected behavior before the omnibus vocabulary is
deleted.

## Acceptance

- [ ] All five ownership-extraction deletion proofs pass.
- [ ] The complete logical `Run` surface exposes only owner construction/wiring,
  startup/shutdown, OS message pumping, top-level frame order, and final exit
  reporting; it owns no mutable subsystem business state.
- [ ] `RuntimeRenderHost` is removed or a small immutable context.
- [ ] `RunInternal.h` is deleted without a replacement shared-state header.
- [ ] No `*Internal`, `*Context`, `*Services`, `*Bindings`, callback pack,
  forwarding facade, stored host pointer/reference, friend access, or renamed
  compatibility surface recreates `Run` authority.
- [ ] Each extracted owner is cohesive and does not combine unrelated input,
  scene, replay, render, UI, physics, tool, capture, defaults, or diagnostics
  authority.
- [ ] No runtime source file in the reconciled inventory exceeds 1,500 lines
  without a cohesion justification and named follow-up owner.
- [ ] A new feature can enter input, scene, replay, or render through its owner
  without adding a `Run::*` method.
- [ ] The final independent adversarial review reports zero credible god-object
  findings and its evidence report is committed. Any credible finding reopens
  this plan and blocks completion.

## Validation

| Slice | Required gate |
|---|---|
| Render host narrowing | CPU DX12 architecture tests + renderer gate; add full gate if behavior shifts |
| Input/interaction ownership | CPU-test umbrella + interaction policy + interaction clicks + full gate |
| Replay workspace | CPU-test umbrella + replay scrub + focused interaction proof + allocation gate when capacity changes |
| Scene ownership | parser/round-trip tests + full gate + physics gate when creation/reset changes |
| Mechanical file splits | owning file-to-validation gate |
| Hot `.inl` → TU conversion | owning area gate + perf gate |

`validate_full` is now the broad CPU/runtime superset after
`validation-gate-integrity.md` V2; keep the owning focused gates alongside it.
