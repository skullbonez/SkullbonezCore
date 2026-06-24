# Runtime Run Decomposition Plan

Date: 2026-06-24
Status: Draft implementation plan
Impact area: runtime architecture, renderer orchestration, scene system, replay, editor tools, diagnostics
Validation for this document-only change: none required

## Goal

Turn `Run` back into an application shell.

The current runtime has good subsystems, but `Run` still acts as the place where
everything eventually meets: renderer pass ownership, scene loading, replay
state, editor state, launcher/manipulator tools, diagnostics, capture, UI,
physics model ownership, and world state. That is the part that looks least
like a professional engine architecture. The fix is not to split files by size.
The fix is to move ownership boundaries until each subsystem has a small public
contract and `Run` only coordinates high-level tick order.

Target outcome:

```text
Run
  Owns process lifetime, startup/shutdown, main loop, and top-level tick order.
  Does not own render pass classes, scene load implementation, replay internals,
  editor/tool internals, or subsystem-specific temporary state.

RuntimeRenderer
  Owns render pass objects, frame render orchestration, render snapshots, and
  renderer-facing frame inputs.

SceneRuntimeCoordinator
  Owns scene load/reset/advance behavior, generated scene population, authored
  scene application, scene automation gates, and scene persistence handoff.

PhysicsEngine
  Owns deterministic simulation stepping, body/collider/constraint storage,
  physics queries, physics diagnostics, and the clean command/query API between
  scene, tools, replay, renderer, and solver internals.

ReplayRuntime
  Owns replay capture, scrub, solver/presentation restore, prediction state,
  replay render overlays, and replay-owned selection/cursor state.

RuntimeTools
  Owns editor placement/gizmos, launcher, manipulator, ray-cast tools, and
  tool-specific transient render/input state.

DiagnosticsRuntime
  Owns capture, perf, SkullScope, logs, UI stress, and validation-facing output.
```

The final design should be easy to debug from a stack trace: the call path for
rendering, scene load, replay, or tools should name the subsystem that owns the
decision instead of disappearing into another `Run::*` helper.

## Current Evidence

Use these as the baseline, not as blame.

- `SkullbonezSource/Runtime/Run.h` declares `Run` and owns a large state block
  starting around the subsystem, input, replay, UI, debug, world, game model,
  render pass, and command queue members.
- `SkullbonezSource/Runtime/Run.h` declares nested render pass classes and then
  stores the pass objects directly on `Run`.
- `SkullbonezSource/Runtime/RunRender.cpp` still has `Run::DrawPrimitives()` as
  the authoritative pass scheduler.
- `SkullbonezSource/Runtime/Scene/SceneRuntime.*` already exists, but it is
  intentionally narrow and mostly owns queue/index bookkeeping. Scene load
  implementation still lives in `Run` methods.
- `SkullbonezSource/Runtime/Replay/` owns replay data types and artifact code,
  but replay interaction, replay render-pose mutation, and replay overlay
  decisions still route through `Run`.
- `SkullbonezSource/Runtime/RuntimeInteractionController.*` is the right
  direction for input ownership, but several tool/replay/editor consequences
  still live in `Run` members and helpers.
- `SkullbonezSource/GameObjects/GameModelCollection.h` friends physics and
  diagnostics classes, including `PhysicsDiagnosticsSink`,
  `PersistentContactSolver`, and `SleepIslandSystem`.
- `SkullbonezSource/Physics/PhysicsScene.h` still accepts
  `GameModelCollection&` for store refresh, stepping, wake, and diagnostics.
- `SkullbonezSource/Physics/PhysicsWorld.h` still passes
  `GameModelCollection&` through solver-facing methods. That is a compatibility
  boundary, not a clean engine API.

## Design Principles

These are the rules for every implementation slice.

1. Preserve behavior first. Extraction commits should be boring. Any behavior
   change needs to be explicit and separately validated.
2. Dependencies point inward toward data and outward toward presentation, never
   back into `Run` for convenience.
3. New subsystem APIs take explicit input structs or service references. A
   temporary `Run&` adapter is allowed only as a short-lived migration bridge
   inside a named phase.
4. Do not create new globals or singletons to make extraction easier.
5. Do not use `friend` as an architecture boundary. Existing friend access is
   migration debt to retire behind explicit APIs.
6. Do not introduce a broad virtual interface unless multiple implementations
   are real. Plain structs and concrete classes are preferred.
7. Do not split by line count. Split by ownership of state, invariants, and
   failure modes.
8. Each phase must leave the engine easier to validate than before it started.
9. Each phase must have an obvious rollback boundary.

## Non-Goals

- Do not rewrite solver math as part of this plan.
- Do not replace the DX12 renderer or finish the render graph here.
- Do not redesign UI visuals.
- Do not remove legacy replay exporters.
- Do not rename every `Run*` file just to make the tree look cleaner.
- Do not mix this with new gameplay features or visual effects.

Physics storage extraction is in scope only as an API and ownership boundary:
move scene/tools/replay/render callers toward a clean physics facade, remove
friend-based coupling, and preserve deterministic solver behavior. Broad solver
algorithm changes belong in a separate physics plan after this boundary exists.

## Target File Layout

Add narrowly scoped files as ownership moves:

```text
SkullbonezSource/Runtime/Render/RuntimeRenderer.h
SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h

SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp

SkullbonezSource/Physics/PhysicsEngine.h
SkullbonezSource/Physics/PhysicsEngine.cpp
SkullbonezSource/Physics/PhysicsApi.h
SkullbonezSource/Physics/PhysicsHandles.h
SkullbonezSource/Physics/PhysicsQueries.h

SkullbonezSource/Runtime/Replay/ReplayRuntime.h
SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
SkullbonezSource/Runtime/Replay/ReplayRenderBridge.h

SkullbonezSource/Runtime/Tools/RuntimeTools.h
SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
SkullbonezSource/Runtime/Tools/EditorToolRuntime.h
SkullbonezSource/Runtime/Tools/LauncherToolRuntime.h
SkullbonezSource/Runtime/Tools/ManipulatorToolRuntime.h

SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
```

These names are intentionally plain. The plan should leave the codebase with
obvious places to put future work.

## System Boundary Contract

This plan should leave clear seams between runtime, scene, physics, renderer,
replay, tools, UI, and diagnostics. The rule is simple: systems talk through
named APIs, not through friendship, raw storage access, or hidden global state.

Hard rules:

1. No new `friend` declarations between subsystems.
2. Existing `friend` declarations are migration debt. Remove them when the
   target subsystem API can express the same operation.
3. Public APIs should expose commands, queries, handles, and immutable views
   instead of another subsystem's mutable containers.
4. Physics code should not require `GameModelCollection&` as its long-term
   simulation API.
5. Render code should read a render-instance view, not physics internals.
6. Scene code should create bodies, colliders, constraints, and render instances
   through explicit builder/command APIs.
7. Replay code should snapshot and restore through physics/replay contracts, not
   by spelunking through solver-private state.
8. Diagnostics may inspect internals only through explicit diagnostic snapshot
   APIs. Debuggability is not an excuse for friendship.
9. If an API needs write access, name the operation. Do not hand out a mutable
   vector and hope callers behave.

Preferred shapes:

```cpp
struct PhysicsBodyHandle
{
    uint32_t index;
    uint32_t generation;
};

struct PhysicsBodyCreateDesc
{
    CollisionShape shape;
    RigidBodyDesc body;
    RenderMaterial material;
    uint32_t sceneObjectId;
};

class PhysicsEngine
{
public:
    PhysicsBodyHandle CreateBody(const PhysicsBodyCreateDesc& desc);
    void DestroyBody(PhysicsBodyHandle body);
    void Step(const PhysicsStepDesc& step);
    void WakeBody(PhysicsBodyHandle body);
    PhysicsSceneSnapshot CaptureSnapshot() const;
    bool RestoreSnapshot(const PhysicsSceneSnapshot& snapshot);
    PhysicsQueryResult RayCast(const PhysicsRayCastDesc& desc) const;
    PhysicsRenderView BuildRenderView() const;
};
```

The exact type names can change. The architectural point cannot: bodies,
colliders, constraints, queries, diagnostics, and render projections need named
contracts. Solver-private storage stays private.

## Ownership Model

### `Run`

Allowed responsibilities:

- parse/store launch configuration after `Init`,
- create top-level subsystems,
- call startup/shutdown hooks in order,
- drive the main loop,
- call per-frame phases in order,
- handle process-level failure reporting,
- provide short compatibility shims while migration is active.

Forbidden final responsibilities:

- owning render pass classes,
- implementing scene object population,
- implementing replay scrub/prediction behavior,
- owning editor/launcher/manipulator transient state,
- formatting diagnostics artifacts,
- mutating render resources directly except through renderer lifecycle hooks.

### `RuntimeRenderer`

Owns:

- render pass instances,
- pass resource lifecycle hooks,
- `DrawPrimitives()` ordering,
- `RenderFrameContext` construction or consumption,
- executed-frame graph snapshot output,
- renderer-only replay overlays once replay supplies overlay inputs.

Does not own:

- scene loading,
- physics stepping,
- replay history storage,
- editor selection state,
- UI command interpretation.

### `SceneRuntimeCoordinator`

Owns:

- `LoadScene`, `ResetCurrentScene`, `AdvanceScene`, and browser selection
  behavior,
- generated object scene construction,
- authored scene application,
- world/fluid/terrain scene defaults,
- required contact and broadphase gates,
- scene snapshot persistence.

Does not own:

- frame rendering,
- replay artifact serialization,
- editor tool live input,
- renderer resource internals.

### `PhysicsEngine`

Owns:

- authoritative physics body and collider storage,
- deterministic simulation stepping,
- constraints and wake/sleep behavior,
- physics queries such as ray casts and broadphase inspection,
- physics debug/diagnostic snapshots,
- replay solver snapshot capture/restore,
- render-facing physics projection data.

Does not own:

- scene file parsing,
- editor placement UI,
- render pass scheduling,
- replay timeline policy,
- object authoring names except stable diagnostic ids.

Final API direction:

- Scene creates and destroys physics bodies through descriptors and handles.
- Tools apply forces, impulses, drags, wakes, and constraints through commands.
- Replay captures/restores deterministic physics snapshots through a replay
  contract.
- Renderer reads immutable render instance views derived from physics/game state.
- Diagnostics reads explicit diagnostic snapshots.
- No subsystem reaches into solver-private containers through `friend`.

### `ReplayRuntime`

Owns:

- presentation replay recorder,
- solver replay recorder,
- event recorder,
- scrubber state,
- replay branch/cause/prediction/velocity edit state,
- render-pose override inputs,
- replay-specific render overlays.

Does not own:

- actual object rendering,
- scene load policy,
- editor placement state,
- generic camera mode policy.

### `RuntimeTools`

Owns:

- editor placement and gizmos,
- launcher ray/fire state,
- manipulator pickup state,
- ray-cast test state,
- tool hover/drag/capture state,
- tool render overlays.

Does not own:

- replay scrub state,
- scene lifecycle,
- renderer pass resource lifetime.

### `DiagnosticsRuntime`

Owns:

- capture controller,
- performance logs,
- SkullScope trace plumbing,
- UI stress state,
- runtime diagnostics snapshots,
- validation-facing artifact writing.

Does not own:

- physics solver implementation,
- render pass order,
- scene object authoring.

## Migration Phases

### Phase 0: Baseline Audit And Guardrails

Purpose: make the current ownership visible before code moves.

Tasks:

1. Produce a short implementation note or checklist before edits that maps
   `Run` members and private methods into these buckets:
   - app shell,
   - renderer,
   - scene,
   - replay,
   - tools,
   - diagnostics,
   - physics/world,
   - UI/input bridge.
2. Count `Run` private methods, direct member fields, and render pass members.
3. Record the current validation gate for the intended first implementation
   slice.
4. Confirm worktree status and user-owned dirty files before any edits.

Acceptance:

- The first implementation PR names the members and methods it intends to move.
- No code behavior changes are made in this phase unless the user explicitly
  asks for them.

Validation:

- Documentation-only audit: no validation required.

Rollback:

- Delete the audit note if it becomes stale before implementation starts.

### Phase 1: Introduce Explicit Runtime Service Views

Purpose: stop new code from reaching across all of `Run`.

Add small borrowed-reference structs. These are not owners.

```cpp
struct RuntimeRenderServices
{
    Textures::TextureCollection& textures;
    GameObjects::GameModelCollection& models;
    Environment::WorldEnvironment& world;
    Environment::Terrain* terrain;
    Environment::CameraCollection& cameras;
    UI::InGameUI& ui;
};

struct RuntimeRenderState
{
    const RunDebugState& debug;
    const RunCameraState& camera;
    const RunTimerState& timers;
    const RunSceneState& scene;
    const RuntimeViewModel& viewModel;
};
```

The exact names can change, but the rule is fixed: renderer code gets only the
data it needs. It does not get `Run&`.

Tasks:

1. Add input/service structs near runtime render code.
2. Build them inside `Run` without moving behavior.
3. Convert one or two render helper calls to use the structs only if doing so
   reduces later churn.
4. Do not touch pass order.

Acceptance:

- The service structs compile.
- They do not own memory.
- They do not expose editor, replay, or scene-load internals unless the render
  path truly consumes them.

Validation:

- If code changes are limited to type plumbing with no runtime behavior change:
  `tools\validate_fast.bat` at PR gate.
- If render output paths are touched: `tools\validate_dx12_renderer.bat`.

Rollback:

- Remove the structs and restore direct references. No data migration should be
  required.

### Phase 2: Extract `RuntimeRenderer`

Purpose: remove render pass ownership and frame render scheduling from `Run`.

This is the highest-value first extraction because the render path already has
clearer pass boundaries than scene/replay/tool code.

#### Phase 2A: Promote Pass Types Out Of `Run`

Tasks:

1. Move nested pass class declarations from `Run.h` into
   `Runtime/Render/RuntimeRenderPasses.h`.
2. Keep method bodies in their current implementation files initially if that
   creates the smallest diff, then move them once compilation is stable.
3. Replace `Run::SkyPass`-style qualification with top-level pass class names.
4. Keep pass constructors temporarily able to receive the references they
   already need.

Acceptance:

- `Run.h` no longer declares render pass classes.
- Render pass classes do not include `Run.h` unless temporarily unavoidable.
- No pass order changes.

Validation:

- `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat` at PR gate
  because pass declarations and renderer-adjacent code moved.

Rollback:

- Move declarations back into `Run.h`. This phase should be a mechanical type
  move.

#### Phase 2B: Add `RuntimeRenderer`

Tasks:

1. Add `RuntimeRenderer` that owns:
   - fullscreen quad pass,
   - sky pass,
   - scene target pass,
   - shadow pass,
   - reflection pass,
   - object pass,
   - terrain pass,
   - water pass,
   - tornado visual pass,
   - debug overlay pass,
   - volumetric pass,
   - tonemap pass,
   - UI/text pass if it stays coupled to late render for now.
2. Move pass member fields from `Run` into `RuntimeRenderer`.
3. Add renderer lifecycle methods:
   - `EnsureFrameResources(...)`,
   - `RenderFrame(...)`,
   - `ReleaseBackendOwnedResources(...)`,
   - `RebuildRegisteredResources(...)` only if renderer ownership truly needs
     them.
4. Make `Run::Render()` call `m_renderer.RenderFrame(...)`.

Acceptance:

- `Run` owns one `RuntimeRenderer`, not individual render passes.
- `Run::DrawPrimitives()` is either gone or is a thin compatibility wrapper.
- Pass resource release order remains explicit.

Validation:

- `tools\validate_dx12_renderer.bat` at PR gate.
- If `Run*`, window resize, or lifecycle hooks are touched broadly:
  `tools\validate_full.bat`.

Rollback:

- Move pass fields back to `Run` and restore the old wrapper call. Do not mix
  this phase with pass behavior changes.

#### Phase 2C: Remove The Temporary `Run&` Dependency

Tasks:

1. If `RuntimeRenderer` or passes temporarily borrowed `Run&`, replace that with
   explicit input structs and service references.
2. Make render frame inputs immutable where possible.
3. Convert replay prediction ghosts into data supplied by `ReplayRuntime`, or
   leave a clearly named `ReplayRenderBridge` until Phase 5.
4. Make `RuntimeRenderer` include no editor, scene-load, or replay headers
   except the narrow render bridge.

Acceptance:

- `RuntimeRenderer` can be reasoned about without reading `Run.h`.
- Render input structs show every non-render dependency by name.
- Adding a new render pass no longer requires adding a member to `Run`.

Validation:

- `tools\validate_dx12_renderer.bat` at PR gate.
- Add `tools\validate_perf.bat` if render-stream hot paths or allocations are
  changed.

Rollback:

- Reintroduce the temporary adapter only inside `RuntimeRenderer`; do not move
  pass fields back unless needed.

### Phase 3: Extract Scene Lifecycle

Purpose: make scene load/reset a scene subsystem operation instead of a `Run`
implementation detail.

Scene work is riskier than renderer extraction because it touches physics,
world defaults, UI state preservation, replay reset, camera setup, and
automation quit behavior. Keep this phase smaller than it is tempting to make.

Tasks:

1. Add `SceneRuntimeCoordinator`.
2. Move pure scene queue and browser decisions first if they are not already
   fully covered by `SceneRuntime` and `SceneController`.
3. Move generated scene setup:
   - `SetUpGameModels`,
   - `SetUpSolverObjects`,
   - generated camera setup,
   - generated scene config replay event production through a callback.
4. Move authored scene application:
   - `SetUpGameModelsFromScene`,
   - `SetUpCamerasFromScene`,
   - required contact gates,
   - required broadphase gates,
   - scene debug/world defaults.
5. Move reset snapshot capture/restore only after the load path is stable.
6. Keep `Run::LoadScene()` as a facade until all call sites are converted.

Acceptance:

- Scene load logic has a single owning class.
- `Run` no longer has to know how authored objects become physics/render
  objects.
- Scene reset/preservation rules are expressed in scene-owned types.
- Replay reset and renderer resource rebuilds are called through explicit
  callbacks or result commands, not hidden cross-subsystem mutation.

Validation:

- `tools\validate_full.bat` at PR gate for broad scene/runtime movement.
- If physics object setup or deterministic scenes change:
  `tools\validate_physics.bat`.
- If visual scene output changes or render resources move:
  `tools\validate_dx12_renderer.bat`.

Rollback:

- Keep `Run::LoadScene()` facade until the end of the phase so scene extraction
  can be reverted one method group at a time.

### Phase 4: Extract The Physics Engine API

Purpose: make physics a clean subsystem instead of a solver hidden behind
`GameModelCollection` compatibility access.

This phase is not a solver rewrite. It is an API extraction. The physics engine
should expose body, collider, constraint, query, snapshot, diagnostics, and
render-projection contracts. Solver internals should become less visible after
each slice.

#### Phase 4A: Define The Public Physics API

Tasks:

1. Add `PhysicsApi.h` and `PhysicsHandles.h`.
2. Introduce stable handles:
   - `PhysicsBodyHandle`,
   - `PhysicsColliderHandle`,
   - `PhysicsConstraintHandle`,
   - optional `PhysicsSceneObjectId` for diagnostics/replay correlation.
3. Add descriptors:
   - body create/update,
   - collider create/update,
   - point-joint constraint create/update,
   - physics step,
   - wake/sleep command,
   - query descriptors.
4. Add immutable views:
   - `PhysicsBodyView`,
   - `PhysicsRenderView`,
   - `PhysicsDiagnosticsSnapshot`,
   - replay solver snapshot view.
5. Keep old `GameModelCollection` paths compiling while the new API is unused
   or bridged.

Acceptance:

- The new API compiles without exposing solver-private containers.
- New API headers do not include `GameModelCollection.h`.
- No new friends are introduced.

Validation:

- `tools\validate_fast.bat` at PR gate for type-only API scaffolding.

Rollback:

- Remove the new API headers. This slice should not alter simulation behavior.

#### Phase 4B: Add `PhysicsEngine` Facade Over Existing `PhysicsScene`

Tasks:

1. Add `PhysicsEngine`.
2. Make it own or wrap the existing `PhysicsScene`.
3. Forward current operations through explicit methods:
   - `Step`,
   - `WakeBody`,
   - `SeedBodyAsleep`,
   - `SetSleepEnabled`,
   - `AddPointJointConstraint`,
   - `ClearPointJointConstraints`,
   - diagnostics path controls,
   - replay snapshot capture/restore.
4. Keep implementation order byte-for-byte compatible where physics determinism
   depends on iteration order.
5. Add temporary adapters from `GameModelCollection` to the new facade only
   where necessary.

Acceptance:

- `PhysicsEngine` is the public class that runtime code should call.
- `PhysicsScene` and `PhysicsWorld` become implementation details behind the
  facade.
- Deterministic stepping order is unchanged.

Validation:

- `tools\validate_physics.bat` at PR gate.
- Use `tools\validate_full.bat` if runtime frame flow changes.

Rollback:

- Keep `GameModelCollection::RunPhysics` as a compatibility entry point until
  callers are moved.

#### Phase 4C: Move Scene And Tool Callers To Physics Commands

Tasks:

1. Scene creation should produce physics body/collider descriptors instead of
   mutating physics-owned internals through `GameModel`.
2. Launcher should apply impulses through a physics command.
3. Manipulator should drag through a physics command or constraint handle.
4. Editor placement should create/update/destroy bodies through the facade.
5. Tornado field configuration should be a physics-system command.
6. Ragdoll creation should use body and constraint descriptors.

Acceptance:

- Runtime, scene, launcher, manipulator, editor, tornado, and ragdoll code call
  the physics facade for physics mutations.
- New call sites do not need `friend` access.
- Old compatibility adapters shrink.

Validation:

- `tools\validate_physics.bat` at PR gate.
- Add `tools\validate_full.bat` when input/runtime launch behavior is touched.

Rollback:

- Move one caller family at a time. Do not move scene, launcher, manipulator,
  and ragdoll calls in one commit.

#### Phase 4D: Remove Friend-Based Physics Coupling

Tasks:

1. Replace `GameModelCollection` friend use with explicit physics/render/replay
   accessors or command APIs.
2. Replace `PhysicsWorld` friend use by `PersistentContactSolver` and
   `SleepIslandSystem` with package-private helper structs or solver context
   objects passed explicitly from physics code.
3. Replace `PhysicsScene` diagnostic friendship with explicit diagnostic
   snapshot APIs.
4. Keep solver-private types private unless they are intentionally part of the
   public diagnostic contract.
5. Add a local check or review checklist item that rejects new cross-subsystem
   `friend` declarations.

Acceptance:

- No cross-subsystem `friend` declarations remain for normal runtime behavior.
- Any remaining friendship is same-subsystem implementation detail and justified
  in a comment.
- Physics API expresses all scene, tool, replay, renderer, and diagnostics
  needs without mutable storage leaks.

Validation:

- `tools\validate_physics.bat` at PR gate.
- `tools\validate_full.bat` for the final friend-removal cleanup if runtime or
  diagnostics behavior moved.

Rollback:

- Remove friend declarations only after their replacement API is landed and
  validated. Friend removal should be cleanup, not the risky behavior move.

### Phase 5: Extract Replay Runtime

Purpose: make replay state a subsystem, not scattered runtime state.

Tasks:

1. Add `ReplayRuntime` that owns existing recorder objects and replay state
   structs currently stored on `Run`.
2. Move replay capture and event-recording helpers behind `ReplayRuntime`.
3. Move render-pose override logic out of `RunRender.cpp`:
   - presentation sample pose override,
   - solver sample pose override,
   - prediction frame pose override,
   - restore pose backup.
4. Have `ReplayRuntime` produce render-facing data:
   - body pose override list,
   - hidden body mask,
   - ghost overlay draw requests,
   - focus mask.
5. Use the physics snapshot API from Phase 4 for solver capture/restore instead
   of reaching through `GameModelCollection` or solver internals.
6. Keep artifact serialization in `Runtime/Replay` and do not remove legacy
   exporters.

Acceptance:

- `Run` no longer owns replay recorder fields directly.
- Replay render mutation has one entry and one restore path.
- Replay-owned selection, prediction, and velocity edit state does not leak into
  editor or launcher code.

Validation:

- `tools\validate_fast.bat` for replay-only structure changes that do not touch
  runtime launch flow.
- `tools\validate_full.bat` if replay changes touch `Run*` frame flow.
- Add replay-specific validation if the touched files introduce or modify a
  replay artifact validation script.

Rollback:

- Keep old `Run` replay fields behind compatibility accessors until all replay
  methods move.

### Phase 6: Extract Runtime Tools

Purpose: make editor, launcher, manipulator, and ray-test state explicit.

Tasks:

1. Add `RuntimeTools` as the owner of tool runtimes.
2. Move launcher state and `LauncherLaser` ownership out of `Run`.
3. Move mouse pickup/manipulator state out of `Run`.
4. Move editor placement/tracer/gizmo state out of `Run`.
5. Keep `RuntimeInteractionController` as the authority for which tool owns
   input; tools execute policy, they do not decide global mode transitions.
6. Use the physics command API from Phase 4 for impulses, drag constraints,
   wakes, and body edits.
7. Convert tool render overlays into data consumed by `RuntimeRenderer`.

Acceptance:

- Tool state cannot be active unless `RuntimeInteractionController` says that
  tool owns world input.
- Tool code does not directly clear replay or scene state; it requests a
  transition through the controller.
- `Run` no longer owns launcher/manipulator/editor transient fields.

Validation:

- `tools\validate_full.bat` at PR gate because this touches runtime input and
  launch behavior.
- Add `tools\validate_physics.bat` if manipulator/launcher impulse behavior
  changes.

Rollback:

- Move one tool at a time. Do not extract editor, launcher, and manipulator in a
  single unreviewable commit.

### Phase 7: Extract Diagnostics Runtime

Purpose: keep validation and debug artifact behavior coherent without making
`Run` a logging class.

Tasks:

1. Add `DiagnosticsRuntime`.
2. Move capture controller ownership.
3. Move perf log and runtime diagnostics helpers.
4. Move UI stress state.
5. Move SkullScope diagnostic trace plumbing, preserving the existing bounded
   query workflow.
6. Keep diagnostics output paths and command-line behavior unchanged.

Acceptance:

- Diagnostics code has one owner and one place to document artifact paths.
- `Run` can ask diagnostics to begin/end a frame or write a requested artifact
  without knowing the file format details.
- SkullScope query/reporting requirements remain intact.

Validation:

- `tools\validate_fast.bat` for diagnostics-only structure.
- `tools\validate_full.bat` if runtime launch, capture, or validation-facing
  behavior changes.
- Follow SkullScope reporting rules if SkullScope is used during debugging.

Rollback:

- Keep diagnostics facade methods on `Run` until callers are moved.

### Phase 8: Slim `Run` And Lock The Boundary

Purpose: make the new architecture resistant to regression.

Tasks:

1. Remove compatibility wrappers that simply forward to subsystem methods.
2. Delete stale `Run` member groups.
3. Move subsystem-specific includes out of `Run.h`; prefer forward declarations.
4. Add comments near remaining `Run` members explaining why they are truly
   top-level app shell state.
5. Add a lightweight static check or documented review checklist:
   - no render pass class definitions in `Run.h`,
   - no replay recorder fields in `Run.h`,
   - no editor/launcher/manipulator transient fields in `Run.h`,
   - no scene object population helpers in `Run.h`,
   - no new `Run&` stored by subsystem classes.

Acceptance:

- `Run.h` reads as a top-level runtime composition root.
- New runtime features have an obvious subsystem home.
- `Run` private method count is dramatically lower than Phase 0.
- The longest implementation files belong to subsystems, not the app shell.

Validation:

- `tools\validate_full.bat` at final PR gate.

Rollback:

- This is cleanup after subsystem extraction. If it fails, revert only the
  cleanup commit, not the extracted subsystem commits.

## Validation Matrix

Repository validation scripts are PR/commit gates, not as-you-go checks.

| Change | Required PR Gate |
|--------|------------------|
| This plan only | No validation required |
| Ownership audit docs only | No validation required |
| Type-only runtime service views | `tools\validate_fast.bat` |
| Render pass declarations or renderer orchestration | `tools\validate_dx12_renderer.bat` |
| Render lifecycle plus `Run*` frame flow | `tools\validate_full.bat` |
| Scene load/reset behavior | `tools\validate_full.bat` |
| Physics API type scaffolding only | `tools\validate_fast.bat` |
| Physics facade, store ownership, or friend-removal work | `tools\validate_physics.bat` |
| Physics object setup, solver body setup, launcher/manipulator impulse behavior | `tools\validate_physics.bat` |
| Render stream hot path or allocation-sensitive pass movement | `tools\validate_dx12_renderer.bat` plus `tools\validate_perf.bat` |
| Diagnostics/capture/runtime artifact behavior | `tools\validate_full.bat` |
| Broad final boundary cleanup | `tools\validate_full.bat` |

When in doubt, use `tools\agent_validate.bat` or the repo-local orchestrator
skill's validation selection process.

## Review Checklist For Each Implementation Slice

Before code review:

- Does this slice move one ownership boundary, or is it mixing several?
- Can it be reverted without losing unrelated work?
- Did it avoid creating a new singleton/global?
- Did it avoid adding new `friend` declarations?
- Did it avoid storing `Run&` in the new subsystem, or is the temporary adapter
  explicitly scheduled for removal?
- Did it expose an operation/query instead of another subsystem's mutable
  storage?
- Are input structs smaller than `Run` and named by subsystem need?
- Did pass order, scene load order, input ownership, and replay determinism stay
  unchanged unless deliberately stated?
- Did the commit message name validation output, not just "tests passed"?

After code review:

- Can a future renderer change avoid reading scene load code?
- Can a future scene load change avoid reading replay prediction code?
- Can a future replay change avoid touching editor placement state?
- Can a future tool change avoid touching render pass ownership?
- Is `Run.h` smaller or at least less connected than before the slice?

## Risk Register

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| Mechanical extraction changes behavior | Runtime behavior is dense and stateful | Move one owner at a time; keep old facades until call sites are stable |
| Temporary `Run&` becomes permanent | It preserves the same coupling under a new name | Allow it only in a named phase and remove it before the phase closes |
| Scene extraction breaks replay reset | Scene load/reset currently touches replay branch and solver state | Use result commands/callbacks for replay resets; validate broad runtime behavior |
| Physics API extraction changes determinism | The solver depends on exact iteration order and byte-exact baselines | Start with facade/adapters; move one caller family at a time; run physics gate |
| Friend removal hides needed diagnostics | Diagnostics currently get privileged access to solver state | Add explicit diagnostic snapshots before deleting friend access |
| Render extraction breaks resource lifetime | Pass objects own backend resources and must release before device shutdown | Keep release/rebuild order explicit; validate DX12 renderer gate |
| Tool extraction creates input overlap | Editor, replay, launcher, and manipulator can all consume world input | Keep `RuntimeInteractionController` as the owner of mode transitions |
| Diagnostics extraction hides validation artifacts | Validation trust depends on visible logs and artifact paths | Preserve artifact paths and quote validation output at PR gate |
| Over-abstraction makes code slower to read | A clean engine is simple, not abstract for its own sake | Prefer concrete classes, structs, and explicit calls over generic frameworks |

## Success Metrics

Track these before Phase 1 and after each major phase:

- `Run.h` total line count.
- Number of direct data members on `Run`.
- Number of private methods declared on `Run`.
- Number of subsystem headers included by `Run.h`.
- Number of render pass classes or pass objects owned by `Run`.
- Number of replay recorder/state fields owned by `Run`.
- Number of editor/launcher/manipulator transient fields owned by `Run`.
- Number of cross-subsystem `friend` declarations.
- Number of public physics APIs that still require `GameModelCollection&`.
- Whether `Run::DrawPrimitives()` still exists.
- Whether `Run::LoadScene()` still implements scene loading or only delegates.

The target is not a specific number. The target is a shape: `Run` should be a
composition root whose members are subsystems, not all the state inside those
subsystems.

## Suggested Commit Sequence

1. `docs: map Run decomposition ownership`
2. `runtime: add explicit render service views`
3. `render: promote runtime render pass types`
4. `render: introduce RuntimeRenderer owner`
5. `render: remove RuntimeRenderer Run dependency`
6. `scene: introduce SceneRuntimeCoordinator`
7. `scene: move generated scene setup`
8. `scene: move authored scene application`
9. `physics: define public physics handles and API`
10. `physics: introduce PhysicsEngine facade`
11. `physics: move scene callers to physics commands`
12. `physics: move tool callers to physics commands`
13. `physics: remove friend-based solver coupling`
14. `replay: introduce ReplayRuntime owner`
15. `replay: move render pose override bridge`
16. `tools: move launcher runtime state`
17. `tools: move manipulator runtime state`
18. `tools: move editor runtime state`
19. `diagnostics: introduce DiagnosticsRuntime`
20. `runtime: slim Run composition root`

Do not force this exact commit list if a smaller natural boundary appears while
working. Do keep the spirit: each commit should be explainable in one sentence
and validated with the narrowest correct gate.

## Final Acceptance Criteria

The plan is complete when:

- `Run` owns top-level subsystems instead of subsystem internals.
- Render pass scheduling and pass resource lifecycle live in `RuntimeRenderer`.
- Scene load/reset/advance behavior lives in `SceneRuntimeCoordinator` or the
  existing scene subsystem.
- Physics stepping, body/collider/constraint mutation, queries, diagnostics,
  replay snapshots, and render projection live behind `PhysicsEngine`.
- Replay capture/scrub/prediction/render bridge state lives in `ReplayRuntime`.
- Editor/launcher/manipulator state lives in `RuntimeTools`.
- Capture/perf/SkullScope/UI stress state lives in `DiagnosticsRuntime`.
- No cross-subsystem `friend` declarations are required for normal runtime,
  scene, replay, render, tool, or diagnostics behavior.
- No public physics mutation path requires callers to own or mutate
  `GameModelCollection` storage directly.
- `Run.h` no longer grows for ordinary render, scene, replay, or tool features.
- Required validation for the final implementation scope is run and quoted.

## Notes For Future Agents

- Drafting this plan is documentation-only and requires no validation.
- Implementing this plan should use `Agentic/Skills/orchestrator/SKILL.md`
  unless the user explicitly asks to bypass it.
- Check `git status --short --branch` before every implementation slice.
- Treat unrelated dirty files as user-owned.
- Do not run `tools\validate_*` while iterating unless a focused diagnostic
  actually answers the current question. Run the correct gate before PR-bound
  commit/push work.
