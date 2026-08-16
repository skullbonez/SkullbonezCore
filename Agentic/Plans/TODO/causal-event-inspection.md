# Causal Event Inspection

Date: 2026-08-15
Status: Active. 0/9 tasks complete.
Impact area: Runtime/Planning operator surface, replay transport, camera
arrival, contact manifold presentation, solver-detail availability,
Rendering value contracts, tests
Owner: Replay planning operator surface
Priority: Active — C0 is unblocked; every later task depends on C0's seek
contract, the owner-ratified synchronized transport below, and C3's detail
availability contract.

## Owner Direction

The causal window currently lists cause rows. This plan makes a selected row
*navigable and inspectable*: one transition moves replay time from the currently
presented simulation frame to the event frame while the scene switches from its
current main camera to a dedicated causal-detail camera with the same eased
progress. That detail camera follows and orbits the selected object without
mutating the saved main camera. The two participating objects and their contact
manifold are then drawn at that frame, and an adjacent floating panel immediately
shows every solver row still available for that frame. The panel presents four
rows at once and scrolls when more exist; it does not hide initial detail behind
an advanced expander.

Placement is fixed by the `AGENTS.md` Replay-Family Placement Rule. This is a new
operator-facing feature built on recorded data, so it belongs in
`SkullbonezSource/Runtime/Planning/` beside the existing overlay, intercept
readout, trip planner, and porkchop owners. It must not be added to
`Runtime/Replay/` or `Runtime/Prediction/`, which own recorded-data
infrastructure and future-simulation production respectively. Cause-tree
*production* already living in
`SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp` does not
license putting consumption there.

### Owner Transport Ruling — 2026-08-16

Selecting a cause row does not jump to a lead-in frame and does not replay the
whole intervening distance at a fixed playback multiplier. It starts one bounded
presentation transition with two endpoints:

- replay time moves from the currently presented live, replay, or predicted
  simulation frame to `RunReplayCauseTreeRow::firstFrame`; and
- `CameraCollection` selects a dedicated, scene-registered causal-detail camera
  and tweens from the currently visible main-camera pose to that camera's focus
  pose.

Both consumers use one Planning-owned normalized progress value. The transition
samples elapsed wall-clock time, computes `u = clamp(elapsed / 1.5 seconds, 0,
1)`, then applies the same cubic ease-out power curve `p = 1 - (1 - u)^3` to
replay time and camera pose. The owner approved the 1.5-second duration and
exponent 3 on 2026-08-16. These are elapsed-time constants, not values inferred
from frame count; movement begins quickly, slows continuously as it approaches
the event, and completes exactly at 1.5 seconds.

The implementation must evaluate progress from total elapsed time rather than
recursively adding a per-frame fraction. `CameraCollection::SetCamera` currently
uses `p += (1 - p) * rate * dt`; `CameraControlState` supplies a `dt`-scaled
rate, but that update is still an Euler approximation whose sampled curve and
threshold completion depend on render cadence. Replace that recurrence and its
`p > 0.99999` completion rule with the finite-duration power curve, completing
exactly at `u == 1`.

Replay time remains a discrete `ReplayFrameIndex`. Convert eased progress to a
monotonic frame between the captured source and target, with explicit and tested
forward and reverse rounding, and force the exact target at completion. A render
frame may request at most one replay frame; if restore work is already active,
coalesce to the newest requested frame instead of queueing obsolete intermediate
restores. The transition changes which fixed-step snapshot is presented; it
never advances physics with a variable timestep.

A causal-row selection is the only entry into the detail camera. Before
selection, capture the currently selected main-camera hash; preserve that
camera's pose in its own fixed `CameraCollection` slot, select the dedicated
detail-camera slot with tweening, and exclude the detail camera from ordinary
camera cycling. The owner approved direct causal-camera retargeting on
2026-08-16: selecting another causal row while detail or aftermath mode is
active reuses the same detail camera from the currently visible pose and
currently presented replay frame, without returning through the main camera.
Both spatial and temporal endpoints restart together.

The detailed event view is paused. Selecting a valid cause row acquires the
existing replay camera pause ownership before the first restore and keeps the
simulation paused through arrival and inspection. Cancellation, invalid targets,
and failed restores unwind only inspection-owned pause state; an operator-owned
pause that predates inspection remains owned by the operator.

Space is the explicit exception. Once the causal event is presented, the
existing Space advance command hides the detail panel and enters an aftermath
follow state. The detail camera stays selected, the scene advances under the
existing fixed-step/Space semantics, and the camera continues to follow the
selected primary object. Releasing inspection pause ownership must not erase a
pre-existing operator pause or invent a second simulation-advance path.

Any mouse-button click other than a causal-row selection, or any timeline scrub,
exits both paused detail and aftermath follow. Close the detail panel, stop the
detail-camera follow owner, and call `CameraCollection::SelectCamera` with the
captured main-camera hash and tweening enabled. Start that return tween from the
currently visible pose so an exit during entry or aftermath cannot jump. A scrub
then proceeds through its ordinary timeline path under the restored main camera;
it never retargets the detail camera. The return uses the same frame-independent
1.5-second cubic spatial curve, but does not move replay time as part of the
camera return.

The selected cause row's primary focused object is the detail camera's orbit
pivot. `ReplayPresentation::ApplyCameraFocus` already records that stable
identity as `RunReplayCameraState::focusedId` from `row.id`; do not replace it
with the contact point or introduce a second pivot owner. Resolve that object at
each presented or advancing frame and drive only the dedicated detail camera
through the existing `AttachedCameraController` fixed-relative orbit path.
`CaptureOrbit` seeds yaw, pitch, and distance from the currently visible pose,
while `BuildFollowPose` keeps the view on the target. Raw mouse movement changes
orbit yaw/pitch and the wheel changes orbit distance; no mouse button is consumed
for orbit because every non-causal-selection button click exits. Start the entry
tween once, then update its destination without
restarting it as replay or aftermath frames advance. A row whose focused object
cannot be resolved has no valid detail-camera endpoint and uses the same explicit
disabled-transport feedback as any other non-restorable row.

### Owner Solver-Detail Ruling — 2026-08-16

Show all solver detail immediately when it is available. The panel viewport is
exactly four solver rows high and scrolls vertically for any additional rows;
there is no collapsed summary or separate advanced mode in the initial design.
Each row names its contact point and bodies, geometry and penetration,
warm-start state and previous impulse, bias, effective masses, friction limit,
accumulated normal/tangent impulses, and every available solver-iteration
sample with units and sign conventions.

Availability is honest and non-reconstructive. Use the solver state retained in
the exact replay frame and use per-iteration pipeline records only when those
records still identify that same frame. Do not re-simulate a prior step merely
to recreate overwritten diagnostics. If the exact replay frame has aged out,
keep the cause row visible but disable transport with `Replay frame expired`.
If the frame remains restorable but its solver detail is absent or overwritten,
continue to allow transport and manifold inspection while the panel displays
`Solver detail not available`. Never substitute detail from a nearby frame.

## Problem And Evidence

Dated 2026-08-15, measured against the current tree.

### What already exists

`RunReplayCauseTreeRow` in `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h`
already carries nearly everything the feature needs to address an event:

| Field | Use to this plan |
|---|---|
| `firstFrame` (`ReplayFrameIndex`) | the seek target; no new frame association is needed |
| `id`, `counterpartId` (`PhysicsSceneObjectId`) | the two participating objects, in stable cross-system identity |
| `contactIndex`, `solverRowIndex`, `pipelineIndex` | addressing into contact, solver, and pipeline data |
| `featureId`, `manifoldPointCount`, `penetration` | manifold identity and extent |
| `normalImpulse`, `tangentImpulse`, `warmStartImpulse`, `bias`, `effectiveMass`, `frictionLimit` | summary solver-row values |
| `point`, `normal`, `impulse` | one representative contact point |
| `warmStarted`, `terrain`, `prediction` | row provenance |

`RunReplayCauseTreeState` in the same header already owns the floating window's
placement, drag, resize, scroll, and pointer-blocking state, so the causal window
is an existing custom floating surface rather than something to invent.

`SkullbonezSource/Runtime/Replay/ReplayScrubber.h` already owns the scrub cursor
and defines `ReplayScrubberAction::RestoreBranch`, with `ReplayRuntime`
sequencing prediction cancellation, physics restore, and camera reaction. Live
restore to an arbitrary recorded frame is therefore an existing capability.

`SkullbonezSource/Runtime/Camera/` already owns a tween system.
`CameraCollection` owns fixed scene-camera slots keyed by hashes; `SelectCamera`
already preserves a tween from the visible pose to a different registered slot,
including interruption while another tween is active. It holds `m_tweenCamera`
("Interpolated pose while tweening") and `m_tweenSpeed`, and
`AttachedCameraController` carries `needsEntryTween`
with existing handling for a transition that begins while another is still
visible. Its fixed-relative path already calls `CaptureOrbit` to recover orbit
yaw, pitch, and distance from the visible camera, then `BuildFollowPose` places
the eye around the target and looks back at its current presented position.
`TickFollow` starts one entry tween and subsequently updates the live
destination without restarting it. `ReplayPresentation::ApplyCameraFocus(
const RunReplayCauseTreeRow&, ... )` already focuses the camera from a cause row
through `RunReplayCameraFocusKind` and stores `row.id` as `focusedId`. Camera
arrival and pivoting therefore need one dedicated registered camera slot and a
small mode owner, not a new collection, tween implementation, or orbit system.

`SkullbonezSource/Physics/PhysicsSolverSnapshot.h` defines
`PhysicsSolverPersistentContactSample`, which carries full per-row solver state:
`normal`, `tangent1`, `tangent2`, `rA`, `rB`, `penetration`, `normalMass`,
`tangentMass1`, `tangentMass2`, `bias`, `frictionLimit`, and the accumulated
`accN`, `accT1`, `accT2`. `ReplayRecorder` records solver frames on a
keyframe-plus-delta ring, so this state is recoverable at any restorable frame.

### The four real gaps

1. **No transport from a cause row.** The row knows `firstFrame`; nothing acts on
   it. Selecting a row does not move the simulation.
2. **No manifold visual.** The row carries one representative `point`/`normal`
   plus a `manifoldPointCount`. The remaining contact points, the reference and
   incident features, and the two bodies' poses at that frame are not presented
   together as a focused visual.
3. **Camera arrival uses a cadence-sensitive recurrence and a componentwise
   linear blend.** `CameraCollection::SetCamera` advances normalized progress as
   `p += (1 - p) * rate * dt` and stops after a threshold. Although the rate is
   multiplied by `cameraDt`, repeated Euler steps do not reproduce one exact
   elapsed-time curve across render cadences. `CameraCollection` then
   builds its tween as a whole-`Camera` vector difference —
   `m_tweenPath = m_cameraArray[toIndex] - m_cameraArray[fromIndex]`
   (`SkullbonezSource/Runtime/Camera/CameraCollection.cpp:126` and `:134`) — so
   eye position, look target, and up vector are all lerped componentwise.
   Linearly interpolating a *look target* produces a non-constant angular rate
   and can sweep badly through large reorientations, because the eye-to-target
   distance and the view direction both change on their own schedules. The
   camera is a look-at triple (`m_position`, `m_view`, `m_upVector`) with no
   orientation quaternion, so "slerp" has no existing meaning to inherit and
   must be defined.

4. **Per-iteration solver detail is transient.** `PhysicsPipelineRecord` in
   `SkullbonezSource/Physics/PhysicsDebugData.h` carries the `ManifoldRow`,
   `WarmStart`, `SolverIteration`, `VelocityWriteback`, `PositionCorrection`, and
   `CacheStore` breadcrumbs that would make the panel genuinely detailed, but
   these are live-only diagnostics gated by the `RetainPipelineRecords`
   compile-time parameter on `PersistentContactSolveTransaction`. They are not in
   the replay artifact and may already have been overwritten when a historical
   cause row is selected.

Gap 4 is an availability boundary rather than a reconstruction requirement; C3
owns its exact lookup and refusal contract. Gap 3 is owned by C2.

### Why unavailable detail stays unavailable

Recording per-iteration solver detail for every tick is not viable: the pipeline
record payload is per contact row per iteration, against a 12-sweep solver and a
4,000-body default scene capacity, and
`PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES` is already an 8 MiB cap for the far
smaller rollback state.

The owner chose not to trade that storage cost for on-demand re-simulation.
Even a deterministic reconstruction would be newly produced evidence rather
than the retained result the operator selected. The panel therefore distinguishes
recorded solver-row state from matching transient iteration records and reports
missing data directly. It never presents regenerated or nearest-frame values as
the original event.

### Cross-Plan Note: Slerp And The Determinism Math Gate

The owner approved true slerp on 2026-08-16. It needs `acos` and `sin`, and
`TIER2_DETERMINISM` T2 adds a gate that scopes `SkullbonezSource/Maths` as well
as `SkullbonezSource/Physics`. A camera slerp will therefore trigger that gate
and require a ruling.

The ruling is legitimate — camera state is presentation and is never
physics-reachable, exactly like the existing `retain-owner` cases for
`SkullbonezSource/Maths/RotationMatrix.h`'s `RotatePointAboutArbitrary` and
`SkullbonezSource/Maths/Matrix4.cpp`'s `tanf`. But
placement matters more than the ruling. Do not add a slerp to
`SkullbonezSource/Maths/Quaternion.cpp` beside `Quaternion::RotateAboutAxis`,
which *is* physics-reachable through `IntegrateBodyRecordPose`: that turns one
file into a mixed-reachability surface where a future caller can pull physics
into a transcendental without any rule visibly breaking. Put a presentation-only
interpolant where its non-reachability is structural rather than a comment.

Normalized-lerp would have sidestepped the gate, ruling, and placement hazard,
but its uneven angular motion does not make eased progress represent the actual
angular journey. C2 accepts the slerp cost and must satisfy the gate and
placement rule rather than substituting nlerp during implementation.

## Goal

Make a selected causal row navigable and inspectable: seek the simulation to the
event frame, present the two participating objects and their full contact
manifold at that frame, and show the solver rows the contact produced in an
adjacent four-row-high scrolling panel, with every still-available iteration
sample and explicit unavailable messages for expired frame or overwritten
detail.

## Non-Goals

- Do not place any part of this feature in `Runtime/Replay/` or
  `Runtime/Prediction/`. Shared immutable values stay in their lowest honest
  owner; `Runtime/App` may compose siblings.
- Do not introduce trajectory, replay, prediction, planning, cause-tree,
  porkchop, or operator-panel vocabulary into `SkullbonezSource/Rendering`.
  Rendering contracts stay feature-neutral and the dependency gate enforces it.
- Do not let `SkullbonezSource/UI` include Runtime or Rendering. UI consumes
  detached value snapshots and emits typed command values.
- Do not record per-iteration solver detail into the replay artifact.
- Do not re-simulate a prior step to reconstruct overwritten solver detail and
  do not substitute diagnostics from a nearby frame.
- Do not change solver behavior, contact ordering, iteration counts, or any
  physics-visible value to make inspection easier. This feature is strictly
  observational.
- Do not refresh any physics, replay, or visual baseline without a separate
  explicit owner decision.
- Do not grow any retained buffer after steady gameplay outside a registered
  `RuntimeReserveAllocator` replay owner with a phase gate, hard cap, and logged
  growth counter.
- Do not add a second retained cause-selection or transport owner. One owner
  holds the selected event and drives the seek.

## Phases

- [ ] **C0 — Fix the seek contract and prove the frame is addressable.** Confirm
  that `RunReplayCauseTreeRow::firstFrame` addresses a frame the scrubber can
  restore for every row kind, including terrain rows, prediction rows, and rows
  whose frame has aged out of the recorder ring. Define the exact behavior for a
  non-restorable row: keep it visible, disable transport, and display `Replay
  frame expired`, never a silent no-op or a nearest-frame guess. Distinguish
  this from a restorable frame whose transient solver diagnostics are gone; that
  row still transports and C3 reports `Solver detail not available`. No new
  retained state; this phase produces the contract that C1, C2, and C3 depend
  on.

- [ ] **C1 — Add transport from the selected causal row.** Add a Planning-owned
  synchronized transition command whose temporal endpoints are the currently
  presented frame and the selected row's frame. Register one dedicated
  causal-detail camera in each scene, capture the selected main-camera hash on
  entry, and select the detail slot with tweening; normal camera cycling must not
  enter that slot. Apply each requested frame
  through the existing `ReplayScrubber` restore path so prediction cancellation,
  physics restore, and camera reaction keep their current sequencing. Forward
  and reverse travel are the same bounded operation; do not add a second restore
  path. Coalesce requests while a restore or prediction cancellation is already
  in flight, and prove an obsolete intermediate frame cannot apply after a newer
  request. Acquire the existing replay-camera pause ownership before transport
  and keep it through detailed inspection. Space hides the detail panel and
  hands advancement back to the existing Space command while the detail camera
  follows the object. Any mouse-button click other than causal-row selection, or
  any scrub, closes the panel and tweens to the captured main-camera slot. Pin
  pre-paused, live, Space, cancellation, failure, click-exit, scrub-exit, and
  interrupted-return paths so none can spuriously resume, strand the simulation,
  or lose the saved camera identity.

- [ ] **C2 — Share one frame-independent power curve with camera arrival.**
  Replace the current recursive camera progress update with the owner-ratified
  finite-duration ease-out curve. A Planning transition owns elapsed time and
  publishes one eased progress value; replay-frame interpolation and
  `CameraCollection` pose interpolation consume that value rather than advancing
  separate clocks. Pin equal progress at multiple render cadences, exact endpoint
  completion, monotonic forward/reverse frame selection, mid-flight retargeting,
  and restore-request coalescing.

  Make the arrival lerp the eye position and true-slerp the orientation, as
  approved by the owner on 2026-08-16. The
  camera is a look-at triple with no orientation quaternion, so define the
  interpolant explicitly: slerp the normalized eye-to-target direction on the
  unit sphere, hold or re-derive `m_upVector` rather than lerping it
  independently, and interpolate the eye-to-target distance separately so the
  look target cannot collapse into the eye mid-flight. Extend the existing
  `CameraCollection` camera-selection tween and drive only the dedicated detail
  slot rather than adding a second tween owner or mutating the saved main slot;
  `AttachedCameraController` already carries entry-tween and
  transition-in-flight handling that must keep working. Another causal-row
  selection retargets the detail slot and synchronized time transition from the
  visible state. A click-exit or scrub-exit instead starts the spatial-only return
  tween to the captured main slot, including when entry is still visible.

  Enter the existing fixed-relative orbit mode for the detail slot around the
  selected row's `focusedId`. Seed its orbit from the currently visible pose through
  `CaptureOrbit`, keep `BuildFollowPose` as the sole continuing pivot/follow
  owner, and preserve raw-mouse yaw/pitch plus wheel-distance interaction without
  requiring a mouse button. During the
  synchronized replay transition, resolve the focus object's pose at the
  presented frame and update the camera destination without restarting the
  entry tween. Continue the same follow/orbit behavior during Space-driven
  aftermath advancement. Prove that arrival and aftermath handoff remain
  continuous, and that return restores the untouched main camera with no eye,
  view, or progress discontinuity.

  Three invariants bound this phase. Camera state is presentation and must never
  reach physics: a differently-timed or interrupted arrival must not change one
  simulation bit. Replay interpolation selects recorded fixed-step snapshots and
  never changes simulation `dt`. Camera and replay time use the exact same eased
  progress sample for the complete transition.

  Clamp the direction dot product before `acos`, provide stable near-parallel
  and antipodal handling, and pin exact endpoints plus representative large
  reorientations. A near-parallel numerical fallback may use normalized-lerp,
  but nlerp is not the general interpolation policy. See the cross-plan note
  above for the required determinism ruling and presentation-only placement.

- [ ] **C3 — Define and implement solver-detail availability.** Read every
  `PhysicsSolverPersistentContactSample` associated with the selected contact
  from the exact restored solver frame. Join `ManifoldRow`, `WarmStart`,
  `SolverIteration`, `VelocityWriteback`, `PositionCorrection`, and `CacheStore`
  records only when their retained diagnostics stamp identifies that same frame;
  never accept current or nearest-frame records by coincidence. Publish a typed
  availability result that distinguishes available detail, `Solver detail not
  available`, and the C0 `Replay frame expired` transport refusal. Do not restore
  the preceding frame, re-simulate, or add long-lived per-iteration recording.
  Pin multiple contact rows, absent rows, overwritten diagnostics, and mismatched
  frame stamps.

- [ ] **C4 — Publish the manifold as a feature-neutral Rendering value contract.**
  Present the two bodies' poses at the event frame and the full manifold — every
  contact point, the normal, both tangents, and penetration per point — through
  generic Rendering value contracts. `ObjectContactManifold` reduces to at
  most four points (`SkullbonezSource/Physics/ObjectContactManifold.h:98`), down
  from `MAX_OBJECT_CONTACT_CANDIDATES` of 32 pre-reduction candidates, so the
  packet is small and fixed capacity. Present matching retained `ManifoldRow`
  records when available, but do not reconstruct discarded candidates after
  their diagnostics are overwritten. Rendering must not learn what a cause row
  is; Planning supplies layout, capacity, and presentation data. Reuse the
  existing contact debug presentation path where it already serves this, rather
  than adding a parallel submission route.

- [ ] **C5 — Add the adjacent solver-row detail panel.** Add the floating detail
  surface next to the manifold visual and show all available rows immediately,
  without an advanced-mode expander. Fix the viewport height to four complete
  solver rows and provide vertical scrolling for every additional row. Each row
  shows its contact point and bodies; normal, both tangents, `rA`/`rB`,
  penetration, `normalMass`, both tangent masses, `bias`, `frictionLimit`, and
  accumulated `accN`/`accT1`/`accT2`; warm-start state and previous impulse; and
  every available iteration's normal impulse delta, accumulated normal impulse,
  tangent impulse magnitude, friction-cone clamp state, position correction,
  velocity writeback, and final cache store. Units and sign conventions must be
  stated in the panel, not inferred by the reader. When C3 reports unavailable
  detail, render exactly `Solver detail not available` in place of the row list.

- [ ] **C6 — Pair the visual and the panel as one focused surface.** Place the
  detail panel adjacent to the manifold visual and keep the pairing coherent
  under drag, resize, and scene reload. Selecting a different causal row retargets
  panel, manifold, time, and the detail camera together or none. Space hides the
  panel and manifold while preserving detail-camera follow for aftermath. Any
  other mouse-button click or any scrub closes the panel and manifold, exits
  detail/aftermath state, and tweens to the captured main camera before the
  ordinary action continues. No stale marker or frozen detail remains. The
  existing `RunReplayCauseTreeState` window placement fields are the precedent
  for placement ownership; do not introduce a second placement convention.

- [ ] **C7 — Prove the cost and the allocation posture.** Measure solver-detail
  lookup, four-row viewport layout, scrolling, and manifold presentation against
  the existing overlay budget, on a representative dense scene rather than the
  marble-run fixture alone. Prove zero
  steady-state allocation: every buffer added here is fixed or reserved before
  steady gameplay, and any replay-phase growth is registered with owner, phase
  gate, hard cap, and logged growth counter, with the replay reserve inventory
  updated in the same commit. State the cost of an inspection so an operator
  action with a visible frame cost is a known trade rather than a surprise.

- [ ] **C8 — Close with tests, gates, and an independent ownership review.** Add
  focused tests named for the subsystems they pin, not for this plan: seek
  targeting and expired-frame refusal, exact-frame solver-detail availability
  and overwritten-detail messaging, four-row viewport scrolling, manifold
  packet capacity and truncation, and panel value mapping including units. Add
  camera coverage for dedicated-slot registration and cycling exclusion, saved
  main-camera restoration, orbit seeding from the visible pose, moving-target
  destination updates that do not restart the entry tween, Space-driven
  aftermath follow with the panel hidden, and click/scrub return during both
  entry and aftermath. Run the mapped gates
  below. Obtain an independent ownership review answering the five questions in
  the `AGENTS.md` review model, with particular attention to whether the new
  Planning owner has absorbed unrelated transport, presentation, and diagnostics
  authority and become the next god object or introduced a second retained
  transport, camera, or pivot owner.

## Validation

Per-phase, using the smallest gate in the `AGENTS.md` file-to-validation map.
Rows are cumulative; the replay visual-fidelity gate supplements rather than
replaces the normal gate.

| Phase | Required gate |
|---|---|
| C0 | `tools\validate_fast.bat` |
| C1 | `tools\validate_full.bat` (Runtime), then `tools\validate_replay_visual_fidelity.bat` |
| C2 | `tools\validate_full.bat`, then `tools\validate_replay_visual_fidelity.bat`; add `tools\validate_physics.bat` to prove a differently-timed or interrupted arrival changed no simulation bit |
| C3 | `tools\validate_full.bat`, then `tools\validate_replay_visual_fidelity.bat` |
| C4 | `tools\validate_dx12_renderer.bat` plus `tools\run_graphics_stress.bat 1`, then `tools\validate_replay_visual_fidelity.bat` |
| C5 | `tools\validate_full.bat`, then `tools\validate_ui_boundary_tests.bat` |
| C6 | `tools\validate_full.bat`, then `tools\validate_replay_visual_fidelity.bat` |
| C7 | `tools\validate_perf.bat`, then `tools\validate_replay_allocation_policy.bat` |
| C8 | `tools\validate_full.bat` and `tools\validate_all_cpu_tests.bat` |

Every phase touching `SkullbonezSource/Runtime/Replay/*` or replay-facing
presentation additionally requires `tools\validate_replay_visual_fidelity.bat`.
The dependency gate must pass at every phase, since the placement rule and the
Rendering feature-neutrality rule are both mechanically enforced.

## Acceptance

- Selecting a causal row seeks the simulation to that row's frame, and a
  non-restorable row remains visible but disables transport with `Replay frame
  expired` rather than failing silently or selecting a nearby frame.
- A valid selection pauses before transport and remains paused for inspection.
  Space hides the panel and advances through existing input semantics while the
  detail camera follows the primary object; a pre-existing operator pause
  survives selection, cancellation, failure, Space handoff, and close according
  to its existing ownership rules.
- The two participating objects and every reduced manifold point are presented
  at the event frame, with the point count matching the row's
  `manifoldPointCount` or an explicit truncation state.
- The solver panel is visible by default, shows exactly four complete rows at
  once, scrolls through every additional available row, and states units and sign
  conventions. Matching retained iteration records appear with their row; absent
  or overwritten detail displays `Solver detail not available` without
  reconstructing or substituting values.
- No `SkullbonezSource/Rendering` type, constant, or function names a Runtime
  feature domain; no `SkullbonezSource/UI` file includes Runtime or Rendering;
  no part of the feature lives in `Runtime/Replay/` or `Runtime/Prediction/`.
- Zero steady-state allocation, with any replay-phase growth registered and the
  reserve inventory updated in the same commit.
- The camera and replay time arrive together under one elapsed-time power curve,
  with identical progress at 30, 60, 120, and uncapped render cadences. The eye
  position is lerped and orientation is true-slerped, the look target cannot
  collapse into the eye mid-flight, and no second tween owner was introduced.
- Causal-row selection is the only way to select the dedicated detail-camera
  slot; ordinary camera cycling never enters it and the saved main slot remains
  untouched. The selected row's primary `focusedId` remains the detail-camera
  orbit pivot through arrival and Space-driven aftermath, with mouse-relative
  orbit and no destination-update tween restarts.
- Any other mouse-button click or any timeline scrub closes the panel and
  manifold, exits detail/aftermath follow, and tweens from the visible pose back
  to the saved main camera. The return is cadence-independent and does not move
  replay time.
- A differently-timed, interrupted, or repeated camera arrival changes no
  simulation bit, proved by the physics gate rather than asserted.
- Measured cost against the overlay budget is recorded, on a dense scene, and
  includes whatever playback the chosen transport semantics performs.
- An independent ownership review finds no unrelated responsibility absorbed into
  the new Planning owner.

## Open Questions

Resolve these inside the owning phase; do not treat them as settled.

1. Is the existing contact debug presentation path the right carrier for the
   manifold visual, or does a focused single-manifold view need its own packet?
   C4 decides by whether reuse forces feature vocabulary into Rendering.
