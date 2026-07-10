# Runtime Shell Decomposition

Date: 2026-07-10 (reconciled)
Status: In progress — 0/13 remaining checklist items complete; earlier
foundation work is summarized separately and is not mixed into this count
Impact area: runtime architecture, scene lifecycle, input routing, render host
Owner: application composition root

## Goal

`Run` is a thin application shell: process lifetime, startup/shutdown, Win32
message pumping, top-level frame order, final owner wiring, and exit reporting.
A scene, replay, render, input, tool, or diagnostics feature lands inside its
owning subsystem without adding a `Run::*` method or a callback into `Run`.

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
| 1 | **Input routing → `InputRouter`** | `TakeInput`, blocked/unfocused handling, post-UI keyboard dispatch, pointer-camera routing, and large callback/context construction | `InputRouter::Tick(const InputFrame&, InputActions&)`; it owns `RuntimeInputContext`, while `RuntimeInteractionController` remains gesture/workspace authority and UI supplies one immutable hit snapshot | `Run.h` has no `TakeInput`, `Dispatch*Keyboard*`, or pointer-routing methods; no input callback receives `void*`/`Run*` | CPU interaction-policy tests, interaction-click automation, then full gate |
| 2 | **Command authority → owner-specific queues** | `DrainRuntimeCommands` and the mixed scene/capture/defaults/quit switch | Replace the omnibus queue with bounded `SceneCommandQueue`, `CaptureCommandQueue`, and `ApplicationCommandQueue`; each concrete owner drains its queue and returns a typed result | No central switch names scene, screenshot, defaults, and replay logging together; replay records accepted owner events | CPU command-order tests plus full gate |
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
2. Extract input routing and owner-specific command queues together.
3. Promote `SceneController`; physics creation/reset work consumes this boundary.
4. Move replay workspace behavior with the replay and UI plans.
5. Finish render composition after overlay producers no longer call `Run`.

## Remaining Work

### A. Narrow the render host

- [ ] A1. Implement ownership extraction 5: replace `RuntimeRenderHostBindings`
  with the five immutable views and move replay/tool overlay production to owners.
- [ ] A2. Put texture lookup/select behind the asset/render view; no pass-level
  call back into `Run` for texture handles.

### B. Move input, command, tool, and replay decisions

- [ ] B1. Implement ownership extraction 1 and delete input callback/context bags.
- [ ] B2. Implement ownership extraction 2 and delete the mixed command switch.

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

## Known Hard Decisions

| Decision | Required answer |
|---|---|
| Input/UI ordering | Which immutable UI snapshot is built before world-input routing, and who owns it for the frame? |
| Command ordering | Which owner drains first when one frame requests scene load, capture, and quit? Encode this in a CPU test. |
| Scene load contract | Which state survives reset/load and which lifecycle event clears interaction, replay, diagnostics, and camera state? |
| Fixed-step ownership | `SimulationSystem` remains timestep owner; do not recreate a generic simulation facade. Decide only which frame coordinator calls it. |

## Acceptance

- [ ] All five ownership-extraction deletion proofs pass.
- [ ] `Run.h` exposes process/frame composition, not subsystem business methods.
- [ ] `RuntimeRenderHost` is removed or a small immutable context.
- [ ] `RunInternal.h` is deleted without a replacement shared-state header.
- [ ] No runtime source file in the reconciled inventory exceeds 1,500 lines
  without a cohesion justification and named follow-up owner.
- [ ] A new feature can enter input, scene, replay, or render through its owner
  without adding a `Run::*` method.

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
