# Attached Object Camera Mode Plan

Date: 2026-06-25
Status: Planned
Impact area: runtime input, camera control, replay/editor selection, UI/HUD,
documentation
Validation for this document-only change: none required

## Goal

Add an `Attach` camera mode that can ride any selected runtime object.

The user-facing intent is playful inspection: select an object, attach the
camera to it, then cycle between a fixed relative camera, a velocity-facing
camera, and a ragdoll head/eyes camera. The mode should feel like Inspect, not
Launcher: physics is paused by default, Space advances simulation, and Enter
toggles whether the camera is actively attached or pinned in world space so the
mouse can be used for UI and replay timeline work.

Target behavior:

```text
Tab
  Cycles the existing top-level camera modes, including Attach.

Attach mode
  Left-click selects the camera target.
  Space advances simulation while the attached camera follows.
  F1 cycles attach submodes for the current target.
  Enter pins/unpins the camera and releases/recaptures mouse-look.

Attach submodes
  Fixed Relative:
    Preserve the player-chosen camera pose relative to the selected object.

  Velocity Forward:
    Preserve the camera's relative eye offset, but look along the target's
    linear velocity.

  Ragdoll Eyes:
    For simple ragdolls, resolve the selected ragdoll group to the head part
    and place the camera near the head, looking along the head's local forward
    direction.
```

## Current Evidence

- `RunCameraMode` currently has `Demo`, `Scene`, `Inspect`, `Launcher`, and
  `Manipulator` in `SkullbonezSource/Runtime/RuntimeCameraMode.h`.
- `Run::ApplyCameraMode`, `Run::CycleCameraMode`,
  `Run::CameraModeEnabledMask`, and `Run::CameraModeLabel` live in
  `SkullbonezSource/Runtime/RunInput.cpp`.
- The minimized UI camera combo hard-codes five options in
  `SkullbonezSource/UI/UI.cpp`.
- `Tab` already cycles top-level camera modes. `F1` is not used by the current
  source or runtime reference. `F2`, `F3`, `F7`, and `F8` are already assigned.
- Existing Inspect mode already routes through
  `RuntimeInteractionController::EnterInspect()` and yields
  `RunWhileStepHeld` physics policy.
- Mouse picking can reuse the existing editor/replay ray path:
  `TryBuildMouseWorldRay()` and `TryPickEditorModel()` in
  `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`.
- Existing replay target picking intentionally normalizes ragdoll part picks
  back to the torso. The attached camera should not do this; selecting a head
  or limb should be meaningful.
- Simple ragdolls are deterministic body groups. In
  `SkullbonezSource/Physics/Ragdoll.cpp`, part index `0` is torso and part
  index `1` is head. `GameModel::GetRuntimeCollectionKind()`,
  `GetRuntimeCollectionRootModelIndex()`, and
  `GetRuntimeCollectionPartIndex()` identify ragdoll membership.
- `GameModel` exposes the data needed for camera solving:
  `GetPosition()`, `GetVelocity()`, `GetAngularVelocity()`,
  `GetOrientation()`, and `GetCollisionShape()`.
- `CameraCollection` exposes pose mutation helpers:
  `SetPrimaryPosition()`, `SetViewCoordinates()`, `SetPrimaryUp()`,
  `RotatePrimary()`, `MovePrimary()`, and `ApplyPrimaryMovementBuffer()`.

## Design Principles

1. Keep the mode scoped to runtime camera/input behavior. Do not change physics
   solver behavior, replay file formats, or scene schemas.
2. Treat Attach as an Inspect-style workspace so it pauses by default and
   advances only while Space is held.
3. Keep object attachment state separate from replay path targets and editor
   selection. Existing selections can seed the camera target, but Attach owns
   its own active target afterward.
4. Do not add new `friend` declarations or direct solver/storage shortcuts.
5. Prefer concrete helper structs and narrow methods over new global systems.
6. Keep the feature robust under scene reset, object deletion, and invalid
   selection.
7. Preserve existing key behavior outside Attach mode.

## Non-Goals

- Do not add saved scene data for attached camera state.
- Do not alter replay artifact camera sample layout.
- Do not add a precise mesh picker. The existing collision-radius pick is good
  enough for this feature.
- Do not change editor gizmo behavior or make Attach participate in editor
  transform tools.
- Do not add new physics constraints or body state just for the camera.
- Do not add a full first-person character controller.

## Proposed User Interaction

### Entering Attach

Attach can be selected from the minimized camera combo or by cycling with
`Tab`.

When Attach starts:

1. Normalize the requested top-level mode through the same mode pipeline used by
   other camera modes.
2. Enter an Inspect-style interaction workspace.
3. Try to adopt a valid existing target:
   - first, current replay path primary target if it maps to a live model,
   - then editor selected model if valid,
   - otherwise no target.
4. If a target exists, capture the fixed-relative camera offset from the current
   camera pose.
5. If no target exists, leave the camera pose unchanged and show Attach as
   waiting for a target.

### Selecting Targets

While Attach is active and the UI does not own mouse input:

- Left-click ray-picks the nearest model using the same broad pick heuristic as
  `TryPickEditorModel()`.
- A hit becomes the attached camera target.
- A miss clears the attached target and leaves the current camera pose
  unchanged.
- Picking a simple ragdoll part stores the actual picked part. It does not
  collapse to the torso.

Target identity should store:

```cpp
struct AttachedCameraTarget
{
    int modelIndex = -1;
    uint32_t replayBodyId = 0;
    char name[64] = {};
};
```

Use `modelIndex` for fast live lookup. Use `replayBodyId` and `name` only to
recover or report identity when the index becomes stale. If recovery is
ambiguous, clear the target.

### Cycling Submodes

`F1` cycles the attach submode only while top-level camera mode is Attach.

Cycle order:

```text
Fixed Relative -> Velocity Forward -> Ragdoll Eyes -> Fixed Relative
```

If the current target is not part of a simple ragdoll, skip `Ragdoll Eyes`.
If there is no current target, keep the submode unchanged and do nothing.

When cycling into Fixed Relative, recapture the current camera pose relative to
the selected object. This makes F1 a convenient "lock this view" action.

### Enter Pin/Unpin

`Enter` has Attach-specific meaning while Attach owns keyboard input.

Active follow state:

- camera is updated every frame from the selected object,
- mouse-look and WASD can adjust the fixed-relative offset in Fixed Relative,
- Space advances physics through the existing Inspect-style policy.

Pinned state:

- camera pose is frozen in world space,
- mouse-look and WASD camera input are released,
- the native mouse is available for UI and replay/timeline interaction,
- Space still follows the existing runtime policy for the active workspace.

Pressing Enter again resumes attached follow. If the resumed submode is Fixed
Relative, recapture the offset from the current camera pose before following.

## Runtime State

Add a small attached-camera state owner instead of expanding
`RunCameraState` with durable feature-specific data. The exact file can be a
new runtime camera helper or a small state section near existing camera/input
state.

Suggested shape:

```cpp
enum class AttachedCameraSubmode
{
    FixedRelative,
    VelocityForward,
    RagdollEyes,
    Count
};

struct AttachedCameraState
{
    AttachedCameraTarget target;
    AttachedCameraSubmode submode = AttachedCameraSubmode::FixedRelative;
    bool activeFollow = true;
    bool hasFixedOffset = false;
    bool hasLastLookDirection = false;

    Math::Vector::Vector3 localEyeOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localViewOffset = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 localUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 lastLookDirection = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
};
```

The state should live in a runtime owner that is cleared on scene reset/load.
Do not serialize it into scene files.

## Math Contract

Use object orientation to convert between world space and local space.

Helpers to add:

```cpp
Math::Transformation::RotationMatrix ModelRotation( const GameModel& model );

Math::Vector::Vector3 WorldToModelVector(
    const GameModel& model,
    const Math::Vector::Vector3& worldVector );

Math::Vector::Vector3 ModelToWorldVector(
    const GameModel& model,
    const Math::Vector::Vector3& localVector );
```

Implementation detail:

- `ModelRotation()` can copy `model.GetOrientation()` and call
  `GetOrientationMatrix()`.
- world to local uses `rotation.TransposeMultiply(worldVector)`.
- local to world uses `rotation * localVector`.

Normalize all camera direction/up vectors with existing vector helpers. If a
computed vector is near zero or non-finite, fall back to a stable default.

## Camera Solve Rules

### Fixed Relative

When a target is selected or fixed mode is recaptured:

1. Read current camera eye, view, and up from `CameraCollection`.
2. Compute target rotation and position.
3. Store:
   - `localEyeOffset = R^T * (cameraEye - targetPosition)`,
   - `localViewOffset = R^T * (cameraView - targetPosition)`,
   - `localUp = R^T * cameraUp`.

Each active-follow frame:

1. Resolve target model.
2. Set eye to `targetPosition + R * localEyeOffset`.
3. Set view to `targetPosition + R * localViewOffset`.
4. Set up to normalized `R * localUp`.

While active follow is true and submode is Fixed Relative, mouse-look/WASD
should adjust the camera before recapturing the offset. This preserves the
player's chosen relative location.

### Velocity Forward

Velocity Forward should keep the same eye offset as Fixed Relative but aim in
the target's movement direction.

Each active-follow frame:

1. Resolve target model.
2. Compute eye from `localEyeOffset` exactly like Fixed Relative.
3. Use normalized `model.GetVelocity()` as look direction when its magnitude is
   above tolerance.
4. If velocity is too small, use `lastLookDirection`.
5. If no last direction exists, use the current camera view direction.
6. If that is also invalid, use model local +Z transformed into world space.
7. Set view to `eye + direction`.
8. Use model local up transformed into world space when valid; otherwise world
   up.

Do not use angular velocity for the look direction in v1. It is useful HUD data
but too chaotic for camera direction.

### Ragdoll Eyes

This mode is available only when the selected target belongs to
`GameModelCollectionKind::SimpleRagdoll`.

Resolve head model:

1. Read selected model's root model index.
2. Search the model list for a model with the same root and part index `1`.
3. If not found, search by same root and name suffix `_head`.
4. If still not found, skip/exit Ragdoll Eyes and fall back to Fixed Relative.

Camera pose:

1. Use the head model's orientation matrix.
2. Define local axes:
   - forward = local +Z,
   - up = local +Y,
   - right = local +X.
3. Place eye at the head center plus a small local offset near the front/upper
   portion of the head:
   - `localEye = (0.0f, 0.20f * radius, 0.85f * radius)`.
4. Set view to `eye + worldForward`.
5. Set up to worldUp.

Use `EditorModelRadius(head)` or collision shape radius helpers for the eye
offset scale. Clamp to a minimum offset so tiny heads do not place the eye at
the exact center.

## Input And Interaction Changes

### Camera Mode Enum

Add:

```cpp
enum class RunCameraMode
{
    Demo = 0,
    Scene,
    Inspect,
    Attach,
    Launcher,
    Manipulator,
    Count
};
```

If preserving enum integer values matters to replay/debug output, append
`Attach` before `Count` instead. Since these are runtime UI mode indices rather
than serialized replay data, the preferred order is to put Attach next to
Inspect.

Update all switch statements that use `RunCameraMode`.

### Runtime Input Actions

Add actions:

```cpp
CycleAttachedCameraSubmode,
ToggleAttachedCameraPin,
```

Bindings:

- `VK_F1` -> `CycleAttachedCameraSubmode`
- `VK_RETURN` -> `ToggleAttachedCameraPin` when `m_camera.mode == Attach`

Important: Attach-mode Enter must run before launcher repro/branch Enter logic
when keyboard input is not owned by UI. Existing Enter behaviors remain
unchanged outside Attach.

### Runtime Interaction Controller

Treat Attach like Inspect for workspace and physics:

- `EnterInteractionForCameraMode(Attach)` returns `m_interaction.EnterInspect()`
  or a dedicated `EnterAttach()` that maps to equivalent policy.
- `BuildFramePolicy()` should leave physics in `RunWhileStepHeld` while Attach
  is active.
- Camera keyboard/mouse control is enabled only while Attach is active-follow,
  not while pinned.

If a dedicated `RuntimeInputMode::AttachedCamera` is added, ensure it uses the
same frame policy as Inspect and appears in transition diagnostics.

### Mouse Ownership

While Attach active follow is true:

- camera mouse-look owns the cursor when UI does not block camera mouse,
- native cursor is hidden,
- mouse deltas update camera controls.

While pinned:

- mouse-look does not own the cursor,
- native cursor is visible,
- UI and timeline interactions can consume mouse normally.

### World Click Handling

Add `TickAttachedCameraWorldClick(...)` in the shared world-click section of
`Run::TakeInput()`:

1. Run after editor/mouse-pickup ownership has had a chance to consume clicks.
2. Run before replay path target selection while Attach is active.
3. Respect `suppressWorldActionThisFrame`, UI cursor ownership, and UI camera
   mouse blocking.
4. On left press, call `TryPickAttachedCameraTargetFromMouse()`.
5. Consume the click whether hit or miss so replay target selection does not
   also fire.

## UI And HUD

Update `SkullbonezSource/UI/UI.cpp`:

- increase `CAMERA_MODE_OPTION_COUNT`,
- add `"Attach"` to `kCameraModeOptions`,
- widen `MINIMIZED_CAMERA_MODE_COMBO_W` if needed so "Manipulator" and
  "Attach" still fit cleanly,
- update disabled-mask math to use the new count.

Update frame data/HUD text:

- `CameraModeLabel(Attach)` returns `"Attach"`.
- Add compact Attach status text in the top HUD or existing mode label:
  - no target: `Attach: pick target`
  - fixed: `Attach: Fixed <name>`
  - velocity: `Attach: Velocity <name>`
  - eyes: `Attach: Eyes <name>`
  - pinned: append `Pinned`

Do not add a large instructional panel. Keep status lightweight.

Update `Agentic/Reference/runtime-reference.md` key bindings:

- `F1`: cycle attached camera submode while in Attach mode.
- `Enter`: pin/unpin attached camera while in Attach mode.
- Mention that Attach uses left-click target selection and Space stepping.

## Replay Considerations

Do not change replay binary or JSON schema.

Existing replay recorders already capture camera eye/view/up from
`CameraCollection`. The attached camera should update the same primary camera
pose before replay capture occurs, so replay samples naturally include the
resulting camera pose.

Do not make Attach consume or mutate replay path targets except when adopting an
initial target. Attach should own a separate target record.

When replay scrubber/timeline UI is active and Attach is pinned, the camera
should remain pinned. Pressing Enter resumes follow from the current live target
if still valid.

## Scene Reset And Lifetime

On scene load/reset:

1. Clear attached camera target.
2. Reset submode to Fixed Relative.
3. Reset pinned/follow state to active follow.
4. Clear stored offsets and last look direction.
5. Normalize camera mode like other modes if the scene load path changes the
   active camera mode.

If the target model index becomes invalid during a frame:

1. Try to recover by matching stored replay body id.
2. If no match, try exact stored name.
3. If still no match, clear target and leave camera pose unchanged.

If the recovered target is a ragdoll part, keep the actual recovered part. Do
not collapse to torso.

## Implementation Phases

### Phase 1: State And Mode Plumbing

Tasks:

1. Add `RunCameraMode::Attach`.
2. Add camera mode label, enabled mask, normalization, and cycling support.
3. Add attached-camera state and reset helper.
4. Add UI combo option and mode label support.
5. Add `RuntimeInputAction` names and key-memory handling for F1 and
   Attach-mode Enter.
6. Add docs for key bindings.

Acceptance:

- Project compiles.
- Attach appears in the minimized camera mode combo.
- `Tab` can enter/leave Attach without changing camera pose or crashing.
- F1 and Enter do nothing outside Attach.

Validation:

- Focused Profile build during implementation if needed.
- PR gate will be the final phase gate, not a separate validation run unless
  committing this phase independently.

### Phase 2: Target Selection

Tasks:

1. Add `TryPickAttachedCameraTargetFromMouse()`.
2. Add `SetAttachedCameraTarget(int modelIndex)`.
3. Add target validation/recovery helper.
4. Add Attach world-click handling in `Run::TakeInput()`.
5. Seed Attach from existing replay/editor selection when entering Attach.
6. Clear target on miss-click.

Acceptance:

- Left-click in Attach selects visible objects.
- Miss-click clears the Attach target.
- Ragdoll part clicks store the clicked part, not the torso.
- Replay target selection does not also fire from Attach clicks.
- Existing replay/editor clicks behave unchanged outside Attach.

Validation:

- Focused manual run for selection behavior.
- Final PR gate: `tools\validate_full.bat`.

### Phase 3: Fixed Relative Follow

Tasks:

1. Add camera local offset capture.
2. Add fixed-relative solve each frame while Attach is active-follow.
3. Integrate solve into `UpdateLogic()` after normal camera input.
4. Let mouse-look/WASD modify the current camera first, then recapture offset
   while Fixed Relative is active.
5. Add Enter pin/unpin behavior.

Acceptance:

- Fixed Relative preserves the player-chosen camera location relative to the
  selected object.
- Space-stepping moves the object and camera together.
- Enter freezes the camera pose and returns native mouse control.
- Enter again resumes follow and recaptures the current fixed offset.

Validation:

- Manual generated-scene run.
- Final PR gate: `tools\validate_full.bat`.

### Phase 4: Velocity Forward

Tasks:

1. Add velocity-facing direction solve.
2. Store last valid velocity/look direction.
3. Add F1 cycling between Fixed Relative and Velocity Forward.
4. Preserve fixed eye offset while changing only look direction.
5. Fall back cleanly when velocity is near zero.

Acceptance:

- Velocity Forward aims along moving targets.
- Slow or stopped objects keep a stable view without snapping to zero.
- F1 recaptures Fixed Relative when cycling back.
- No per-frame heap allocation is introduced.

Validation:

- Manual generated-scene or physics scene run with moving objects.
- Final PR gate: `tools\validate_full.bat`.

### Phase 5: Ragdoll Eyes

Tasks:

1. Add simple-ragdoll head resolver.
2. Add eye camera pose solve from head local axes.
3. Make F1 skip Eyes for non-ragdoll targets.
4. Fall back to Fixed Relative if head resolution fails.
5. Add concise HUD text for skipped/unavailable eyes mode only if existing HUD
   has a suitable transient message path.

Acceptance:

- Selecting any simple-ragdoll part allows Eyes mode.
- Eyes mode follows the head part, not the torso.
- Non-ragdoll targets skip Eyes in the F1 cycle.
- Scene reset or ragdoll removal clears/falls back safely.

Validation:

- Manual ragdoll scene run.
- Final PR gate: `tools\validate_full.bat`.

### Phase 6: Polish, Docs, And Guardrails

Tasks:

1. Update runtime reference key binding table.
2. Add HUD/status text if not already added.
3. Review touched files for comment quality and no stale instructions.
4. Run the required PR gate.
5. Commit with a detailed commit body if PR-bound work is requested.

Acceptance:

- The feature is discoverable through the camera mode combo and key reference.
- No existing top-level camera mode behavior regresses.
- Validation output is captured and quoted before PR-bound commit/push.

Validation:

- `tools\validate_full.bat`.

## Validation Plan

Repository validation scripts are PR/commit gates, not iteration commands.

For this plan document:

- No validation required.

For implementation:

- Use focused `tools\validate_build.bat Profile` only if compile feedback is
  needed during iteration.
- Required final gate: `tools\validate_full.bat`, because the implementation
  touches `Runtime/*`, `Run*`, input, UI, and camera behavior.
- If implementation unexpectedly changes physics stepping or solver behavior,
  also run `tools\validate_physics.bat`.
- If implementation changes render pass code, shaders, baselines, or screenshot
  behavior, also run `tools\validate_dx12_renderer.bat`.

Manual checks before final validation:

1. Generated demo: enter Attach, click a moving body, cycle Fixed and Velocity
   with F1, step with Space, and pin/unpin with Enter.
2. Authored scene: confirm Attach can be selected and cleared even when scene
   mode cameras are active.
3. Ragdoll scene: click torso, head, and a limb; verify Eyes resolves to the
   head and stays stable while Space steps.
4. Non-ragdoll target: verify F1 skips Eyes.
5. Replay/timeline interaction: pin camera with Enter, operate UI/timeline with
   mouse, press Enter again to resume follow.
6. Launcher/editor/replay modes: verify existing left-click, Enter, F1/F2/F3,
   F7/F8 behavior outside Attach.
7. Scene reset/load: verify target clears safely and no stale model index is
   dereferenced.

## Review Checklist

- Does Attach use Inspect-style physics policy?
- Does Attach have its own target state instead of mutating replay path
  selection?
- Are existing replay/editor/launcher interactions unchanged outside Attach?
- Does the F1 action do nothing outside Attach?
- Does Attach-mode Enter take precedence only while Attach owns keyboard input?
- Are stale target indices recovered or cleared safely?
- Does ragdoll eyes mode resolve the head deterministically?
- Does the camera solver avoid per-frame heap allocation?
- Does the implementation avoid new `friend` declarations and globals?
- Does final validation include `tools\validate_full.bat` output?

## Risk Register

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| Attach clicks also alter replay targets | User loses the timeline selection while selecting camera targets | Consume Attach clicks before replay target picking |
| Stale model index after reset | Could dereference invalid model storage | Clear on scene reset and revalidate every frame |
| Ragdoll eyes resolve wrong body | The funny mode becomes disorienting or unsafe | Use simple ragdoll root and part index 1, then name suffix fallback |
| Velocity direction snaps at low speed | Camera becomes jittery when target slows down | Keep last valid direction and stable fallbacks |
| Enter conflicts with launcher/replay Enter | Existing workflows could regress | Apply Attach Enter only when `m_camera.mode == Attach` and keyboard is not UI-owned |
| UI combo text overflows | Minimized diagnostics UI looks broken | Adjust combo width and title fitting if needed |
| Attach accidentally enables Inspect gizmo | Camera selection could start transform interaction | Keep Attach world-click handling separate from `InspectGizmoInteractionActive()` |
| Replay capture becomes inconsistent | Replay samples must still contain camera poses | Update normal `CameraCollection` pose before existing replay capture |

## Suggested Commit Notes

Subject:

```text
feat: add attached object camera mode
```

Body outline:

```text
Adds an Attach camera mode that lets the runtime camera follow a selected
object with fixed-relative, velocity-facing, and simple-ragdoll eyes submodes.

Runtime/input:
- Added Attach to camera mode cycling and UI selection.
- Added F1 submode cycling and Attach-mode Enter pin/unpin behavior.
- Kept Attach on Inspect-style physics policy so Space advances while the
  scene is otherwise paused.

Camera/selection:
- Added camera-owned target state separate from replay/editor selection.
- Reused existing world ray picking while preserving actual ragdoll part
  selection.
- Added fixed-relative, velocity-forward, and ragdoll-head camera solves.

Docs:
- Updated runtime key reference for Attach, F1, Enter, Space, and click target
  selection.

Validation:
- tools\validate_full.bat
- <quote meaningful success lines here>
```

## Notes For Future Agents

- This plan is documentation-only and requires no validation.
- Before implementation, follow the startup contract in `AGENTS.md`.
- Implementation from `Agentic/Plans` should use
  `Agentic/Skills/orchestrator/SKILL.md` unless the user explicitly asks to
  bypass it.
- There were user-owned dirty files present when this plan was verified:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h` and
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`; an additional dirty
  `SkullbonezSource/Runtime/RunPasses.cpp` also appeared during the final
  status check. Do not overwrite or revert those unless the user explicitly
  asks.
- Keep this feature out of scene serialization unless a later request asks for
  saved camera rigs.
