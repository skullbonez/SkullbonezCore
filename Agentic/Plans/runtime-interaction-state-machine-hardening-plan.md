# Runtime Interaction State Machine Hardening Plan

Date: 2026-06-25
Status: Active implementation plan; multiple behavior-preserving slices landed
Impact area: runtime input, editor tools, replay UI, camera policy, tool physics stepping
Validation for this document-only change: none required

## Goal

Make mouse selection, camera mode, editor mode, replay mode, launcher,
manipulator, and pointer capture feel like one professional interaction system
instead of a pile of partly-related booleans.

The target standard is:

```text
State machine for ownership.
Commands for mutation.
Events for observation.
Typed gesture state instead of bool clusters.
Central input routing instead of every tool listening globally.
```

Events and subscriptions are useful, but they are not the center of the design.
The center is authoritative ownership: at any instant, exactly one workspace,
one tool, and at most one pointer gesture owns world input. Everything else is
either passive rendering or notification.

## Current Diagnosis

The current code is in a transitional state. It is not hopeless, and it is not
random: the repo already has the right architectural pieces starting to exist.
The problem is that none of them is fully authoritative yet.

Useful existing pieces:

- `SkullbonezSource/Runtime/RuntimeInteractionController.*` owns
  `RuntimeWorkspace`, `WorldInteractionOwner`, `PhysicsAdvanceState`, and frame
  policy generation.
- `SkullbonezSource/Runtime/InputController.*` owns runtime input actions and
  derived `RuntimeInputMode`.
- `SkullbonezSource/Runtime/Tools/RuntimeTools.*` owns launcher, editor,
  manipulator, ray-cast, and tool transient state after the runtime run
  decomposition work.
- `SkullbonezSource/Runtime/Replay/ReplayRuntime.*` owns replay state after the
  recent replay runtime extraction.
- `SkullbonezSource/Runtime/RunInput.cpp` already has transition cleanup
  helpers such as `ClearReplayInteractionForRuntimeTransition()` and
  `ClearEditorInteractionForRuntimeTransition()`.

The mess is that these pieces still overlap instead of forming one clear
authority chain.

Implementation progress on `nightrunner-25th-june`:

- `RuntimeInteractionController` now owns typed gesture and pointer-capture
  state, including camera-look, editor gizmo drag, mouse-pickup drag, replay
  scrub, velocity, prediction-horizon, and cause-tree drag kinds.
- `Run::TakeInput()` now builds a `RuntimeInputSnapshot` and routes pointer
  input through `RouteRuntimePointerInput(...)` before later keyboard/camera
  handling.
- Camera label writes are guarded behind
  `SetCameraModeLabelAfterInteractionTransition(...)`.
- World-interaction owner writes are guarded behind
  `SetWorldInteractionOwnerAfterInteractionTransition(...)`.
- Editor selection is routed through `RuntimeInteractionCommand` and publishes
  a command-result `RuntimeInteractionEvent`.
- Runtime picking uses `RuntimePickService::TryPickModel(...)` for attach-camera
  target, editor selection, interaction automation projection, manipulator
  pickup, and replay path target selection.
- Replay scrubber state now distinguishes `historicalSamplePaused` from
  `liveAdvanceHeld`.
- `tools/check_runtime_boundaries.py` blocks new direct camera-mode writes, new
  direct owner writes, and new duplicated `TryPick*Model*` helper declarations
  or definitions.

Current evidence:

- `Run::TakeInput()` remains the main procedural router for camera actions, UI,
  scene cycling, replay input, editor input, launcher input, manipulator input,
  selection, and fallback behavior.
- `RunCameraMode`, `RuntimeWorkspace`, `WorldInteractionOwner`, editor booleans,
  replay scrubber booleans, and `RuntimeInputMode` all describe overlapping
  slices of "what mode are we in?"
- Camera-mode and world-owner writes are now guarded, but compatibility bridge
  functions still live in `RunInput.cpp`.
- Picking is centralized behind `RuntimePickService`, but some higher-level
  tool actions still live as `Run` methods while owner APIs continue to shrink.
- Tool state structs still mix persistent settings, current hover, active drag,
  mouse capture, hot axes, shortcut latch state, and transient previews.
- Replay pause state is no longer the old `paused`/`simulationPaused` pair, but
  broader replay drag/capture booleans remain until replay tools fully migrate
  to gesture payloads.
- Mouse pickup/manipulator physics policy is worth re-checking early. Current
  frame code passes the mouse pickup physics callback when manipulator is
  active, while physics stepping depends on the controller's physics advance
  policy. If manipulator is accidentally routed through `RunWhileStepHeld`,
  left mouse drag can become dependent on holding `Space`.

## Non-Goals

- Do not rewrite the input binding system.
- Do not redesign the UI visuals.
- Do not replace replay, editor, launcher, or manipulator behavior in one big
  rewrite.
- Do not introduce a generic global event bus for all mouse events.
- Do not add broad virtual frameworks unless they are clearly useful in this
  codebase.
- Do not mix this with render, physics solver, scene format, or asset work
  except where input policy already touches them.

## Design Principles

1. One authority owns interaction state.
2. One pointer gesture owns the mouse at a time.
3. A transition exits the previous owner before entering the next owner.
4. Exit routines clear transient state completely.
5. Tools ask for transitions; they do not directly patch global mode state.
6. Commands mutate scene, replay, physics, selection, and camera state.
7. Events notify other systems after mutation succeeds.
8. UI gets first refusal for pointer input.
9. Picking is a service with explicit policy, not copied ray-test code.
10. Impossible states should be unrepresentable, or at least asserted loudly.

The high-level smell to remove is "many bools imply one hidden mode." The
replacement is "one typed mode owns a typed payload."

## Target Mental Model

The runtime should be describable as three nested layers:

```text
Workspace
  Broad context: Live, Inspect, Edit, Replay.

Tool
  World-input owner inside the workspace: None, Launcher, Manipulator,
  EditorPlacement, EditorGizmo, InspectGizmo, ReplayScrub, ReplayVelocityEdit,
  ReplayPrediction, ReplayBranchTarget, ReplayCauseTree, ReplayPathTarget.

Gesture
  Current captured pointer operation: None, CameraLook, ObjectPick,
  GizmoDrag, MousePickupDrag, ReplayScrubDrag, ReplayVelocityDrag,
  ReplayCauseTreeDrag, ReplayPredictionHorizonDrag.
```

A workspace can have no active tool. A tool can have no active gesture. A
gesture cannot exist without an owner that is allowed to hold it.

Examples:

```text
Inspect / None / CameraLook
  Right mouse owns camera look. No object mutation.

Inspect / InspectGizmo / GizmoDrag
  The inspect gizmo owns the left mouse. Physics remains paused unless policy
  says otherwise.

Edit / EditorPlacement / None
  Editor placement preview can render and update hover, but no pointer gesture
  is captured.

Replay / ReplayVelocityEdit / ReplayVelocityDrag
  Replay velocity edit owns the drag. Launcher, editor placement, and
  manipulator cannot receive world clicks.

Live / Manipulator / MousePickupDrag
  Manipulator owns the left mouse and physics runs according to manipulator
  policy.
```

## Target Types

Names can change during implementation. The important part is the shape.

```cpp
enum class InteractionWorkspace
{
    Live,
    Inspect,
    Edit,
    Replay
};

enum class InteractionTool
{
    None,
    Launcher,
    Manipulator,
    InspectGizmo,
    EditorPlacement,
    EditorGizmo,
    ReplayScrub,
    ReplayVelocityEdit,
    ReplayPrediction,
    ReplayBranchTarget,
    ReplayCauseTree,
    ReplayPathTarget
};

enum class PointerButton
{
    Left,
    Middle,
    Right
};

enum class PointerCaptureOwner
{
    None,
    UI,
    CameraLook,
    ToolGesture
};
```

Gesture state should move toward a variant-like payload:

```cpp
struct NoGesture
{
};

struct CameraLookGesture
{
    PointerButton button;
    int startX;
    int startY;
};

struct ObjectPickGesture
{
    int startX;
    int startY;
};

struct GizmoDragGesture
{
    int targetModelIndex;
    int axis;
    Math::Matrix::Matrix4 startTransform;
};

struct MousePickupDragGesture
{
    int targetModelIndex;
    Math::Vector::Vector3 localGrabPoint;
    Math::Vector::Vector3 previousAngularVelocity;
};

struct ReplayScrubDragGesture
{
    double startTime;
};

struct ReplayVelocityDragGesture
{
    int targetModelIndex;
    int axis;
    bool angular;
};

using InteractionGesture =
    std::variant<NoGesture,
                 CameraLookGesture,
                 ObjectPickGesture,
                 GizmoDragGesture,
                 MousePickupDragGesture,
                 ReplayScrubDragGesture,
                 ReplayVelocityDragGesture>;
```

The repo can decide whether this is literally `std::variant` or a local tagged
struct. The rule is that gesture payload lives in one field with one active
kind, not across a dozen loose booleans.

## Authoritative Controller Contract

`RuntimeInteractionController` should become the only owner of workspace, tool,
gesture, pointer capture, camera-look policy, and physics-advance policy.

Core responsibilities:

- Own the current `InteractionWorkspace`.
- Own the current `InteractionTool`.
- Own the current `InteractionGesture`.
- Own pointer capture state and cursor visibility intent.
- Build per-frame policy for:
  - camera look allowed,
  - fly controls allowed,
  - world clicks allowed,
  - physics advance,
  - launcher active,
  - manipulator active,
  - editor active,
  - replay active.
- Execute transition cleanup by delegating to the relevant runtime owners.
- Reject impossible transitions in debug builds.
- Emit interaction events after successful transitions.

Controller APIs should be declarative:

```cpp
RuntimeInteractionTransition EnterWorkspace( InteractionWorkspace workspace,
                                             InteractionExitReason reason );

RuntimeInteractionTransition SetTool( InteractionTool tool,
                                      InteractionExitReason reason );

bool BeginGesture( InteractionGesture gesture );
void UpdateGesture( const PointerEvent& event );
void EndGesture( InteractionExitReason reason );

RuntimeInteractionFramePolicy BuildFramePolicy( const RuntimeInteractionFrameInput& input ) const;
```

Avoid APIs that allow callers to tweak half the state:

```cpp
// Avoid this style long term.
controller.SetMouseCaptured( true );
controller.SetReplayOwner( true );
controller.SetEditorDragging( false );
```

The transition itself should be the operation:

```cpp
controller.SetTool( InteractionTool::Launcher, InteractionExitReason::EnterLauncher );
```

## Tool Handler Contract

Each tool should eventually have a small handler. This does not need to be a
polymorphic inheritance hierarchy. A concrete dispatcher over tool-specific
methods is fine.

Useful shape:

```cpp
struct ToolPointerDecision
{
    bool consumed = false;
    std::optional<InteractionGesture> beginGesture;
    std::vector<InteractionCommand> commands;
};

class RuntimeToolRouter
{
public:
    void OnEnterTool( InteractionTool tool, InteractionExitReason reason );
    void OnExitTool( InteractionTool tool, InteractionExitReason reason );

    ToolPointerDecision OnPointerDown( InteractionTool tool, const PointerEvent& event );
    ToolPointerDecision OnPointerMove( InteractionTool tool, const PointerEvent& event );
    ToolPointerDecision OnPointerUp( InteractionTool tool, const PointerEvent& event );
    ToolKeyDecision OnAction( InteractionTool tool, RuntimeInputAction action );
};
```

Responsibilities:

- Launcher handler fires, previews, and clears launcher-specific state.
- Manipulator handler starts, updates, and ends mouse pickup.
- Editor placement handler owns placement preview and placement clicks.
- Editor gizmo handler owns transform gizmo hover and drag.
- Inspect gizmo handler reuses gizmo mechanics without entering editor
  workspace.
- Replay handlers own scrubber drag, velocity drag, prediction horizon drag,
  cause-tree drag, branch/path target pick, and replay camera inspection.

Handlers should not directly clear unrelated systems. They should return a
command or request a transition. The transition exit path clears unrelated
state.

## Input Routing Order

`Run::TakeInput()` should shrink into an input pump and dispatcher. The desired
routing order is:

1. Build `RuntimeInputSnapshot`.
2. Update keyboard/action edge state.
3. Give UI first refusal.
4. If a gesture is captured, route pointer movement/up to that gesture owner.
5. If no gesture is captured, route pointer down to the active tool.
6. If the active tool does not consume it, route workspace fallback behavior.
7. Route camera look only if frame policy allows it.
8. Execute commands produced by the router.
9. Publish events produced by successful commands/transitions.
10. Build the final frame policy for physics/render/UI.

The important part is that "global mouse down" is not broadcast to every tool.
Only one owner receives a world pointer event.

## Commands

Commands are the write path. They should be narrow, named operations that make
mutation auditable.

Candidate commands:

- `ChangeWorkspaceCommand`
- `SetToolCommand`
- `BeginGestureCommand`
- `EndGestureCommand`
- `SelectObjectCommand`
- `ClearSelectionCommand`
- `BeginEditorPlacementCommand`
- `PlaceEditorObjectCommand`
- `BeginGizmoDragCommand`
- `UpdateGizmoDragCommand`
- `CommitGizmoDragCommand`
- `CancelGizmoDragCommand`
- `BeginMousePickupCommand`
- `UpdateMousePickupTargetCommand`
- `EndMousePickupCommand`
- `FireLauncherCommand`
- `SetReplayLiveHoldCommand`
- `ScrubReplayToSampleCommand`
- `BeginReplayVelocityEditCommand`
- `CommitReplayVelocityEditCommand`
- `ClearReplayPredictionCommand`

The command executor can start simple. It does not need an enterprise command
bus. A local `ExecuteInteractionCommand(...)` switch near the runtime
interaction bridge is enough if that fits the repo.

The value is not abstraction theater. The value is that a mouse click produces
an explicit mutation record instead of triggering whichever booleans happen to
be true.

## Events And Subscriptions

Events are for notification, not ownership.

Good events:

- `InteractionWorkspaceChanged`
- `InteractionToolChanged`
- `InteractionGestureChanged`
- `SelectionChanged`
- `CameraModeChanged`
- `ReplayLiveHoldChanged`
- `ReplayScrubbed`
- `SceneObjectPlaced`
- `SceneObjectTransformCommitted`

Bad events:

- `GlobalMouseDown`
- `SomeoneMaybeWantsLeftClick`
- `AnyModeChangedNowEveryoneFixYourState`
- `ReplayStateChangedAndEditorShouldGuessWhatToClear`

Subscription rules:

- Event subscribers may update passive UI, labels, diagnostics, overlays, or
  cached read models.
- Event subscribers must not claim mouse capture.
- Event subscribers must not directly enter or exit interaction modes.
- Event subscribers must not mutate physics, scene, or replay state unless the
  event is explicitly a command result handled by the owning command executor.

## Picking Service

Picking should be centralized. Today each pick helper has a slightly different
policy. That makes it easy for editor selection, attach selection, manipulator
selection, and replay path selection to drift apart.

Add a service shape like:

```cpp
enum class PickPurpose
{
    EditorSelect,
    InspectSelect,
    AttachCameraTarget,
    ManipulatorBody,
    ReplayPathTarget,
    ReplayBranchTarget
};

struct PickRequest
{
    PickPurpose purpose;
    Math::Vector::Vector3 rayOrigin;
    Math::Vector::Vector3 rayDirection;
    bool includeInactive = false;
    bool includeHiddenByReplayFocus = false;
    bool preferSelected = false;
    float maxDistance = 0.0f;
};

struct PickHit
{
    bool hit = false;
    int modelIndex = -1; // Compatibility until stable handles exist.
    float distance = 0.0f;
    Math::Vector::Vector3 worldPosition;
};
```

Long term, `PickHit` should use stable object IDs or physics handles instead of
raw model indices where possible. Compatibility can keep model indices during
the first slices.

The service should own:

- ray construction from screen coordinates,
- filtering by model activity/visibility,
- replay pose source policy,
- editor-placement exclusions,
- manipulator body eligibility,
- attach target eligibility,
- stable hit sorting.

Target cleanup:

- Replace `TryPickEditorModel(...)` with `PickService::Pick(EditorSelect)`.
- Replace `TryPickAttachedCameraModel(...)` with
  `PickService::Pick(AttachCameraTarget)`.
- Replace `TryPickMousePickupModel(...)` with
  `PickService::Pick(ManipulatorBody)`.
- Replace `TryPickReplayPathTargetFromMouse(...)` internals with
  `PickService::Pick(ReplayPathTarget)`.

## Physics Policy

Physics stepping should be an output of interaction policy, not a side effect
of camera mode labels.

Expected policy:

```text
Live / None
  Scene physics follows scene settings.

Inspect / None or InspectGizmo
  Physics paused by default.
  Holding Space advances one policy-controlled step/run window.

Edit / EditorPlacement or EditorGizmo
  Physics generally paused or editor-controlled.
  Editor mutations wake/sleep bodies through explicit physics commands.

Replay / Replay*
  Replay owns solver/presentation time.
  Live solver hold and historical scrub are distinct states.

Live / Launcher
  Physics runs live.

Live / Manipulator
  Physics runs live while manipulator is active and the drag callback can apply
  impulses every physics step.
```

Priority fix:

- Verify manipulator mouse pickup still advances physics without requiring
  `Space`.
- If it does not, change controller policy so `Manipulator` uses live physics,
  or split "inspector step-held physics" from "tool live physics" explicitly.

## Camera Policy

Camera mode should become a view/input policy, not the master interaction mode.

Short-term compatibility:

- Keep `RunCameraMode` for UI labels and existing camera behavior.
- Route changes through the interaction controller.
- Stop assigning `m_camera.mode` directly outside the controller/bridge.

Long-term target:

```text
InteractionWorkspace
  Owns broad behavior.

CameraNavigationPolicy
  Says whether fly controls, orbit/follow, attached camera, and look gestures
  are allowed.

RunCameraMode
  Either becomes a camera navigation preset, or shrinks to labels for the
  existing camera implementation.
```

Current product policy from the session state:

- Launcher mode and Free/Inspect mode do not need right-click hold for camera
  rotation.
- Other modes should require right-click hold for camera rotation.
- Every mode that supports camera rotation should still expose right-click
  rotate when appropriate.

That policy should be encoded in `BuildFramePolicy(...)` or the future camera
navigation policy, not scattered through input branches.

## Replay Pause Naming

Rename replay pause concepts when touching replay interaction state.

Current:

```cpp
scrubber.paused
scrubber.simulationPaused
```

Target:

```cpp
enum class ReplayScrubPositionState
{
    LiveTimeline,
    HistoricalSample
};

enum class ReplayLiveAdvanceState
{
    Running,
    HeldAtCurrentFrame
};
```

This clarifies UI and transition behavior:

- Historical sample state means the scrubber is parked on recorded time.
- Live hold means the live replay/solver is held at the present frame for
  replay inspection tools.
- Entering editor, launcher, manipulator, or inspect from replay can clear
  each state deliberately instead of guessing what "paused" means.

## Transition Cleanup Contract

Every transition follows the same shape:

1. Capture previous workspace, tool, gesture, and pointer capture.
2. End active gesture.
3. Exit active tool.
4. Exit previous workspace if changing workspace.
5. Clear pointer capture and cursor-hide requests owned by the old state.
6. Enter the new workspace.
7. Enter the new tool.
8. Emit transition events.

Exit routines must clear:

- hover state,
- drag state,
- hot axes,
- active handles,
- selected transient replay targets,
- placement previews,
- prediction horizon drag,
- cause-tree drag/resize,
- mouse pickup capture,
- previous pointer button latches,
- cursor hide/lock requests,
- replay-owned camera inspection state when leaving replay inspection,
- editor-owned placement/gizmo state when leaving edit.

Do not require the destination tool to clean up the previous tool. That is how
leftover state survives.

## Implementation Phases

### Phase 0: Lock The Problem Statement

Purpose: prevent the refactor from becoming vague cleanup.

Tasks:

1. Keep this plan as the implementation anchor.
2. Add a short checklist or implementation note listing current direct
   interaction writes:
   - direct `m_camera.mode = ...`,
   - direct `SetWorldInteractionOwner(...)`,
   - direct replay pause bool writes,
   - direct mouse capture/cursor hide writes,
   - duplicated pick helpers.
3. Confirm the intended product policy for:
   - Inspect/Free camera rotation,
   - Launcher camera rotation,
   - Manipulator physics running live,
   - editor-vs-inspect selection sharing.

Acceptance:

- A future worker can start implementation without re-litigating the target
  model.
- No code behavior changes in this phase.

Validation:

- Documentation-only: no validation required.

### Phase 1: Introduce Typed Interaction State Without Behavior Change

Purpose: create the new shape while keeping current behavior.

Tasks:

1. Extend or wrap `RuntimeInteractionController` with explicit workspace, tool,
   gesture, and capture state.
2. Add `InteractionGesture` as a typed payload, initially defaulting to
   `NoGesture`.
3. Add debug-only assertions for impossible combinations.
4. Add transition result metadata that records:
   - previous workspace,
   - next workspace,
   - previous tool,
   - next tool,
   - previous gesture,
   - next gesture,
   - exit reason.
5. Keep existing booleans as compatibility state for now.

Acceptance:

- Controller can represent current state without changing behavior.
- `BuildFramePolicy(...)` still mirrors existing runtime behavior.
- New state can be logged during development for comparison.

Validation:

- PR gate: `tools\validate_fast.bat` if this remains type plumbing.
- Use `tools\validate_full.bat` if frame policy behavior changes.

Rollback:

- Remove the new state fields and leave existing behavior untouched.

### Phase 2: Route Mode Changes Through One Bridge

Purpose: stop direct mode writes from spreading.

Tasks:

1. Add one bridge function for camera/workspace/tool transitions.
2. Convert direct `m_camera.mode = ...` assignments to use the bridge where
   practical.
3. Convert direct `m_interaction.EnterReplay()` and
   `SetWorldInteractionOwner(...)` call sites to use named transition requests
   where practical.
4. Keep compatibility wrappers around old entry points during migration.
5. Add a local grep/checklist item for new direct camera mode writes.

Acceptance:

- A mode change has one obvious entry path.
- Transition cleanup always sees the old owner before the new owner is set.
- Existing UI labels and shortcuts still behave the same.

Validation:

- PR gate: `tools\validate_full.bat`, because `Run*` input/frame behavior is
  touched.

Rollback:

- Convert one call-site family at a time so a bad migration can be reverted
  without undoing the whole plan.

### Phase 3: Introduce Pointer Events And Central Routing

Purpose: make mouse input a routed event, not procedural checks scattered
through `TakeInput()`.

Tasks:

1. Add `PointerEvent` and `RuntimeInputSnapshot` structs.
2. Build the snapshot once per frame from existing input state.
3. Add a central route function:
   - UI first,
   - captured gesture,
   - active tool,
   - workspace fallback,
   - camera fallback.
4. Keep the old code branches behind the route function initially.
5. Log or assert when more than one owner attempts to consume the same pointer
   event.

Acceptance:

- There is one pointer routing order in code.
- UI capture consistently prevents world input.
- Mouse up reaches the gesture that captured mouse down even if the cursor moved
  over UI or another tool.

Validation:

- PR gate: `tools\validate_full.bat`.

Rollback:

- Keep the old `TakeInput()` branches callable until each owner migrates.

### Phase 4: Migrate Camera Look And Pointer Capture

Purpose: remove the most visible capture bugs first.

Tasks:

1. Move cursor hide/restore intent behind controller policy.
2. Represent right-mouse and free-look as `CameraLookGesture`.
3. Make focus loss call one central `CancelPointerCapture(...)`.
4. Encode product policy:
   - Inspect and Launcher can rotate without requiring right-mouse hold if
     that is the selected behavior,
   - other applicable modes use right-mouse hold,
   - right-mouse rotate remains available where appropriate.
5. Ensure active left-button tool gestures block camera look until release.

Acceptance:

- Alt-tab/focus loss cannot leave the cursor hidden or a drag alive.
- Camera look cannot overlap with gizmo drag, replay scrub drag, or mouse
  pickup drag.
- Camera behavior matches the explicit product policy.

Validation:

- PR gate: `tools\validate_full.bat`.

Rollback:

- Keep old camera look code behind a compatibility path until manual checks pass.

### Phase 5: Migrate Launcher And Manipulator Tools

Purpose: make the two live tools explicit and fix any physics policy drift.

Tasks:

1. Add launcher and manipulator handlers under `RuntimeTools` or an adjacent
   runtime tool router.
2. Move left-click fire and left-click pickup start into tool handlers.
3. Move mouse pickup drag state into `MousePickupDragGesture`.
4. Ensure entering launcher/manipulator clears replay, editor, and inspect
   gesture state through transition cleanup.
5. Verify manipulator physics runs live and mouse pickup impulses apply during
   physics steps without requiring `Space`.
6. Keep existing overlay/render data paths stable.

Acceptance:

- Launcher owns left click only when launcher tool is active.
- Manipulator owns left drag only when manipulator tool is active.
- Switching away from manipulator during drag releases capture and clears
  pickup state.
- Manipulator drag behavior is not tied to inspect step-held physics.

Validation:

- PR gate: `tools\validate_full.bat`.
- Add `tools\validate_physics.bat` if impulse or stepping behavior changes in a
  way that can affect deterministic baselines.

Rollback:

- Migrate launcher and manipulator as separate commits if possible.

### Phase 6: Migrate Editor Placement And Gizmo Interaction

Purpose: split editor persistent mode from transient mouse gestures.

Tasks:

1. Keep editor workspace state in `RuntimeTools::Editor()` as needed.
2. Move editor placement click/preview ownership into `EditorPlacement`.
3. Move gizmo hover/drag ownership into `EditorGizmo` and `InspectGizmo`.
4. Represent active gizmo drag as a typed `GizmoDragGesture`.
5. Make editor transition cleanup clear placement preview, gizmo hover, drag
   group capture, hot axes, and cursor capture.
6. Decide whether inspect selection and editor selection share one selected
   object model or have separate read models.

Acceptance:

- Editor placement and editor gizmo cannot both own the same click.
- Inspect gizmo uses transform behavior without implying editor placement mode.
- Leaving editor kills placement and gizmo transient state.
- Entering replay or launcher during an editor drag cannot leave handles active.

Validation:

- PR gate: `tools\validate_full.bat`.

Rollback:

- Migrate placement and gizmo as separate slices.

### Phase 7: Centralize Picking

Purpose: make selection rules explicit and reusable.

Tasks:

1. Add `RuntimePickService` or an equivalent helper.
2. Move ray construction into the service.
3. Add `PickPurpose` and `PickRequest`.
4. Convert editor selection first.
5. Convert manipulator pickup.
6. Convert attach-camera target picking.
7. Convert replay path/branch target picking.
8. Add compatibility fields for raw model indices until stable object IDs or
   physics handles are ready.

Acceptance:

- Picking policy is selected by `PickPurpose`.
- A future change to model visibility or replay-pose picking has one place to
  land.
- Duplicated pick helpers are removed or become thin wrappers.

Validation:

- PR gate: `tools\validate_full.bat`.
- Add `tools\validate_physics.bat` only if physics query behavior or
  deterministic stepping is touched.

Rollback:

- Convert one pick purpose at a time.

### Phase 8: Migrate Replay Interaction

Purpose: make replay tools behave like tools, not special-case global state.

Tasks:

1. Split replay pause naming into historical scrub state and live hold state.
2. Move scrubber drag into `ReplayScrubDragGesture`.
3. Move velocity edit drag into `ReplayVelocityDragGesture`.
4. Move prediction horizon drag into a replay prediction gesture payload.
5. Move cause-tree drag/resize into replay-owned gesture payloads.
6. Route replay path and branch target picks through the pick service.
7. Make replay exit cleanup clear:
   - historical scrub,
   - live hold if owned by replay inspection,
   - prediction,
   - horizon drag,
   - velocity edit,
   - cause-tree drag/resize,
   - path/branch target transient selection,
   - replay camera inspection capture.

Acceptance:

- Replay interaction state is owned by replay tools and cleared by replay
  transitions.
- Entering editor, launcher, manipulator, or inspect from replay cannot leave
  replay mouse capture alive.
- Replay UI can still render passive state without owning world input.

Validation:

- PR gate: `tools\validate_full.bat`.
- Add replay-specific artifact checks if new replay validation exists by then.

Rollback:

- Migrate one replay tool at a time; do not combine scrub, velocity edit,
  prediction, and cause tree into one risky commit.

### Phase 9: Add Commands And Events

Purpose: formalize mutation and notification after ownership is stable.

Tasks:

1. Add a small interaction command executor.
2. Convert high-risk direct mutations to commands first:
   - selection,
   - tool changes,
   - gesture begin/end,
   - launcher fire,
   - manipulator begin/end,
   - replay live hold/scrub.
3. Add event publication after successful command execution.
4. Update UI/read models from events where useful.
5. Keep command execution synchronous unless there is a concrete reason to
   defer.

Acceptance:

- Mouse input produces explicit commands.
- Events do not own pointer input or global mode transitions.
- UI can observe selection/tool changes without reaching into every tool's
  mutable state.

Validation:

- PR gate: `tools\validate_full.bat`.

Rollback:

- Keep the executor local and concrete so it can be unwound if it proves too
  heavy.

### Phase 10: Remove Obsolete Bool Clusters And Add Guardrails

Purpose: make the cleanup stick.

Tasks:

1. Remove transient bools that are replaced by `InteractionGesture`.
2. Remove compatibility wrappers that only forward to the controller.
3. Remove duplicated pick helpers or leave only thin wrappers with TODO-free
   names.
4. Add a lightweight boundary check or review checklist for:
   - no new direct `m_camera.mode = ...` outside the interaction bridge,
   - no new direct world owner writes outside the controller/tool router,
   - no new global mouse-down subscriber,
   - no new replay pause bool aliases,
   - no new duplicated pick helper.
5. Update `Agentic/SessionState.md` only when implementation becomes active or
   lands.

Acceptance:

- The main runtime input path reads as a router, not a mode maze.
- Tool transient state is visibly split from persistent tool configuration.
- A future tool has an obvious integration path.

Validation:

- PR gate: `tools\validate_full.bat`.

Rollback:

- This should be cleanup after behavior-preserving migrations. Revert cleanup
  without reverting earlier extracted owners if needed.

## Manual Test Matrix

Run these during implementation slices as focused manual checks. Formal
repository validation remains the PR/commit gate.

Camera and capture:

- Right mouse look starts and ends cleanly.
- Focus loss releases hidden cursor/capture.
- Camera look cannot start during left-button gizmo drag.
- Camera look cannot start during replay scrub drag.
- Inspect and Launcher camera rotation match the selected product policy.

UI vs world input:

- Clicking UI controls never selects world objects behind the UI.
- Dragging a UI slider does not start camera look or world drag.
- Releasing the mouse over UI still ends a world gesture that captured outside
  UI.

Inspect:

- Enter Inspect.
- Physics pauses by default.
- Holding `Space` advances according to policy.
- Left click selects a body.
- Inspect gizmo edits do not accidentally unpause simulation.

Editor:

- Tilde enters editor.
- Placement preview appears only in editor placement.
- Editor gizmo drag captures and releases mouse.
- Switching to replay during drag clears editor capture and handles.
- Switching to launcher during placement clears placement preview.

Launcher:

- Launcher left click fires only in launcher.
- Switching from replay velocity edit to launcher clears replay handles first.
- Switching from editor gizmo to launcher clears editor drag first.
- Camera controls match launcher policy.

Manipulator:

- Manipulator left drag picks an eligible body.
- Drag updates the target while mouse is held.
- Releasing left mouse clears pickup state.
- Switching modes during drag clears pickup state.
- Physics advances live for manipulator drag without requiring `Space` if that
  is the intended policy.

Replay:

- Replay scrub drag captures and releases mouse.
- Historical scrub state and live hold state are visually distinct.
- Velocity edit drag captures and releases mouse.
- Prediction horizon drag cannot overlap velocity drag.
- Cause tree drag/resize cannot overlap replay scrub drag.
- Entering editor from replay clears replay active gestures.
- Entering manipulator from replay clears replay active gestures.
- Replay passive overlays can still render when allowed without consuming world
  clicks.

Picking:

- Editor select, inspect select, attach target, manipulator pickup, and replay
  path target all pick the expected object under the cursor.
- Hidden/inactive/replay-focused objects follow the chosen `PickPurpose` policy.
- Selection results are stable when objects overlap.

## Validation Matrix

Repository validation scripts are PR/commit gates, not as-you-go checks.

| Change | Required PR Gate |
|--------|------------------|
| This plan only | No validation required |
| Audit/checklist docs only | No validation required |
| Type-only controller state scaffolding | `tools\validate_fast.bat` |
| Runtime input routing or `Run*` behavior | `tools\validate_full.bat` |
| Camera mode policy behavior | `tools\validate_full.bat` |
| Editor/launcher/manipulator tool routing | `tools\validate_full.bat` |
| Physics stepping or manipulator impulse behavior | `tools\validate_physics.bat` plus `tools\validate_full.bat` if runtime behavior is also touched |
| Replay interaction behavior | `tools\validate_full.bat` |
| Picking changes that use physics queries differently | `tools\validate_full.bat`, add `tools\validate_physics.bat` if deterministic physics output can change |
| Broad final cleanup | `tools\validate_full.bat` |

When implementation starts, use the repo-local orchestrator skill unless the
user explicitly asks to bypass it.

## Risk Register

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| Behavior changes hide inside cleanup | Mouse interaction is high-touch and hard to inspect from code alone | Migrate one owner at a time; use manual scenario checks after each slice |
| Event bus becomes a second input system | Global subscriptions can recreate the bool mess under a different name | Events notify only; routing and transitions stay centralized |
| Gesture variant duplicates old state for too long | Compatibility fields can drift from authoritative fields | Add temporary debug assertions and remove compatibility state by phase |
| Replay tools are special-cased forever | Replay has many active submodes and can leak capture/state | Treat replay submodes as tools with gestures; migrate one replay tool at a time |
| Picking service changes subtle selection order | Users notice wrong object selection immediately | Preserve current sorting first; add purpose-specific policy explicitly |
| Manipulator physics regresses | Dragging bodies must run physics and apply impulses reliably | Verify early and encode manipulator physics as explicit policy |
| Camera policy becomes confused with workspace | Camera mode and interaction mode currently overlap | Introduce a camera navigation policy separate from workspace/tool ownership |
| Large refactor becomes unreviewable | Input code is dense | Keep slices small and behavior-preserving; prefer compatibility bridges |
| Raw model indices outlive their safety | Object deletion/reordering can invalidate interaction targets | Keep compatibility first, then introduce stable IDs/handles in a focused slice |

## Success Metrics

Track these before implementation and after major phases:

- Number of direct `m_camera.mode = ...` assignments outside the interaction
  bridge.
- Number of direct `SetWorldInteractionOwner(...)` calls outside the
  controller/tool router.
- Number of duplicated pick helper implementations.
- Number of loose transient gesture booleans in editor, replay, and manipulator
  state.
- Whether `Run::TakeInput()` reads as a router or as a tool implementation.
- Whether one captured gesture owns pointer input.
- Whether focus loss clears capture through one path.
- Whether replay pause concepts are named by meaning instead of generic bools.
- Whether manipulator physics policy is explicit and tested manually.

There is no magic line-count target. The shape is the metric: a future reader
should be able to answer "who owns this click?" without reading every mode's
bools.

## Suggested Commit Sequence

1. `docs: map runtime interaction state machine plan`
2. `runtime: add typed interaction gesture state`
3. `runtime: route mode changes through interaction bridge`
4. `runtime: introduce pointer input snapshot`
5. `runtime: centralize pointer capture cleanup`
6. `runtime: encode camera look policy`
7. `tools: route launcher input through tool handler`
8. `tools: route manipulator input through tool handler`
9. `tools: route editor placement input through tool handler`
10. `tools: route editor and inspect gizmo gestures`
11. `runtime: introduce pick service`
12. `tools: migrate editor and manipulator picking`
13. `replay: split replay scrub and live hold state`
14. `replay: route scrub and velocity gestures`
15. `replay: route prediction and cause tree gestures`
16. `runtime: execute interaction commands`
17. `runtime: publish interaction events`
18. `runtime: remove obsolete interaction bools`
19. `runtime: lock interaction ownership guardrails`

Keep the sequence flexible. The important rule is one owner or one behavior
family per commit.

## Final Acceptance Criteria

The plan is complete when:

- `RuntimeInteractionController` owns workspace, tool, gesture, pointer
  capture, camera-look policy, and physics-advance policy.
- `Run::TakeInput()` builds input state, routes it, executes commands, and does
  not implement every tool inline.
- UI always receives first refusal for pointer input.
- Exactly one gesture can capture the mouse.
- Every mode transition exits the previous tool/gesture before entering the new
  one.
- Launcher, manipulator, editor placement, editor gizmo, inspect gizmo, and
  replay tools are explicit owners.
- Selection/picking goes through one pick service with purpose-specific policy.
- Replay historical scrub and live hold are named separately.
- Manipulator physics policy is explicit and does not accidentally depend on
  inspect step-held behavior.
- Events are used for observation only.
- Commands are used for scene, selection, replay, camera, and tool mutations.
- Direct camera mode writes and duplicated selection logic are gone or isolated
  behind named compatibility bridges scheduled for deletion.

## Notes For Future Agents

- Drafting this plan is documentation-only and requires no validation.
- Implementing this plan touches `Run*` and runtime input behavior, so the
  default final PR gate is `tools\validate_full.bat`.
- If manipulator physics stepping, impulses, body wake/sleep behavior, or
  deterministic physics output changes, include `tools\validate_physics.bat`.
- Check `git status --short --branch` before every implementation slice.
- Treat unrelated dirty files as user-owned.
- Do not use events as a replacement for ownership. Events are the
  notification layer after the state machine and command layer have done the
  real work.
