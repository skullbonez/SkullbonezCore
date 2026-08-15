# Causal Event Inspection

Date: 2026-08-15
Status: Active. 0/9 tasks complete.
Impact area: Runtime/Planning operator surface, replay transport, camera
arrival, contact manifold presentation, solver diagnostics regeneration,
Rendering value contracts, tests
Owner: Replay planning operator surface
Priority: Active — C0 is unblocked; every later task depends on C0's seek
contract, C1's transport semantics, and C3's regeneration decision.

## Owner Direction

The causal window currently lists cause rows. This plan makes a selected row
*navigable and inspectable*: the transport seeks the simulation to the frame the
event occurred, the camera lerps and slerps into place as the simulation runs
forward to that moment, the two participating objects and their contact manifold
are drawn at that frame, and an adjacent floating panel shows the solver rows the
contact produced in as much detail as the engine can honestly reconstruct.

Placement is fixed by the `AGENTS.md` Replay-Family Placement Rule. This is a new
operator-facing feature built on recorded data, so it belongs in
`SkullbonezSource/Runtime/Planning/` beside the existing overlay, intercept
readout, trip planner, and porkchop owners. It must not be added to
`Runtime/Replay/` or `Runtime/Prediction/`, which own recorded-data
infrastructure and future-simulation production respectively. Cause-tree
*production* already living in
`SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp` does not
license putting consumption there.

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
`CameraCollection` holds `m_tweenCamera` ("Interpolated pose while tweening")
and `m_tweenSpeed`, and `AttachedCameraController` carries `needsEntryTween`
with existing handling for a transition that begins while another is still
visible. `ReplayPresentation::ApplyCameraFocus( const RunReplayCauseTreeRow&, ... )`
already focuses the camera from a cause row through `RunReplayCameraFocusKind`.
Camera arrival is therefore a refinement of existing owners, not a new one.

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
3. **Camera arrival is a componentwise linear blend.** `CameraCollection`
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

4. **Per-iteration solver detail is not recorded.** `PhysicsPipelineRecord` in
   `SkullbonezSource/Physics/PhysicsDebugData.h` carries the `ManifoldRow`,
   `WarmStart`, `SolverIteration`, `VelocityWriteback`, `PositionCorrection`, and
   `CacheStore` breadcrumbs that would make the panel genuinely detailed, but
   these are live-only diagnostics gated by the `RetainPipelineRecords`
   compile-time parameter on `PersistentContactSolveTransaction`. They are not in
   the replay artifact and cannot be read back from it.

Gap 4 is the design crux and C3 owns it. Gap 3 is owned by C2.

### Why regeneration rather than recording

Recording per-iteration solver detail for every tick is not viable: the pipeline
record payload is per contact row per iteration, against a 12-sweep solver and a
4,000-body default scene capacity, and
`PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES` is already an 8 MiB cap for the far
smaller rollback state.

The engine's determinism contract makes recording unnecessary. Restoring to the
frame before the event and stepping once with pipeline capture enabled
reproduces that step exactly, because byte-exact fixed-step reproduction is the
validation contract the repository already enforces. The detail is therefore
regenerated on demand, bounded by one step for one selected event, rather than
stored for every tick of the timeline.

This is a deliberate dependency on the determinism envelope. If the envelope does
not hold, the regenerated detail silently stops describing the recorded event —
so C3 must prove equivalence rather than assume it, and the acceptance criteria
below make that proof explicit.

### Cross-Plan Note: Slerp And The Determinism Math Gate

If C2 chooses true slerp, it needs `acos` and `sin`, and
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

Choosing normalized-lerp sidesteps the gate, the ruling, and the placement
hazard entirely. That is not a reason to choose it on its own, but it is a real
part of the cost of slerp here and C2 should weigh it rather than discover it.

## Goal

Make a selected causal row navigable and inspectable: seek the simulation to the
event frame, present the two participating objects and their full contact
manifold at that frame, and show the solver rows the contact produced in an
adjacent floating panel, with per-iteration detail regenerated deterministically
rather than recorded.

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
- Do not change solver behavior, contact ordering, iteration counts, or any
  physics-visible value to make inspection easier. This feature is strictly
  observational.
- Do not let regeneration perturb live simulation state, and do not refresh any
  physics, replay, or visual baseline without a separate explicit owner decision.
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
  non-restorable row — a disabled transport with a stated reason, never a silent
  no-op or a nearest-frame guess. No new retained state; this phase produces the
  contract that C1, C2, and C3 depend on.

- [ ] **C1 — Add transport from the selected causal row.** Add a Planning-owned
  action that seeks the simulation to the selected row's frame through the
  existing `ReplayScrubber` restore path, so prediction cancellation, physics
  restore, and camera reaction keep their current sequencing. Fast-forward and
  rewind are the same operation against a target frame; do not add a second
  restore path. The causal window emits a typed command value; the composition
  root applies it. Prove the transport cannot run while a restore or prediction
  cancellation is already in flight.

- [ ] **C2 — Define transport semantics and add camera arrival.** Decide first
  what "fast forward to the causal moment" means, because C1's restore is
  instantaneous and an instant jump has no duration for a camera to move across.
  Three candidates, and the phase must pick one and record why:

  1. Instant restore to the event frame with the camera tweening afterwards over
     wall-clock. Cheapest, but nothing is seen to fast-forward.
  2. Accelerated playback from the current frame to the event frame. Literal, but
     unbounded: the cost scales with timeline distance, so it needs a cap and a
     fallback when the event is far away.
  3. Restore to a short lead-in before the event, then run forward at a chosen
     rate while the camera arrives, so the operator is in position before the
     contact resolves and watches it happen. Recommended: bounded like option 1,
     legible like option 2.

  Then make the arrival lerp the eye position and slerp the orientation. The
  camera is a look-at triple with no orientation quaternion, so define the
  interpolant explicitly: slerp the normalized eye-to-target direction on the
  unit sphere, hold or re-derive `m_upVector` rather than lerping it
  independently, and interpolate the eye-to-target distance separately so the
  look target cannot collapse into the eye mid-flight. Extend the existing
  `CameraCollection` tween rather than adding a second tween owner;
  `AttachedCameraController` already carries entry-tween and
  transition-in-flight handling that must keep working. Define what happens when
  the operator selects another row or scrubs mid-flight, and honour the existing
  precedent that a new transition may begin while another is still visible.

  Two invariants bound this phase. Camera state is presentation and must never
  reach physics: a differently-timed or interrupted arrival must not change one
  simulation bit. And whatever playback the chosen option uses must keep the
  fixed timestep, so a faster or slower arrival cannot alter fixed-step output.

  Decide slerp against normalized-lerp on evidence rather than reflex. True slerp
  gives constant angular velocity and needs `acos` and `sin`; nlerp needs neither
  and is often indistinguishable for the modest reorientations a camera arrival
  performs. If slerp is chosen, see the cross-plan note below — it is not free
  here.

- [ ] **C3 — Add the deterministic solver-detail regeneration owner.** Restore to
  the frame preceding the event, step once with pipeline capture enabled, and
  collect the `ManifoldRow`, `WarmStart`, `SolverIteration`, `VelocityWriteback`,
  `PositionCorrection`, and `CacheStore` records for the selected contact into a
  fixed-capacity, preallocated buffer. The step must run in isolation and must
  not perturb live physics state; the isolated prediction clone is the existing
  precedent for that isolation, but the owner lives in Planning rather than
  Prediction. Prove the regenerated step reproduces the recorded frame exactly by
  comparing the resulting body state against the recorded frame byte-for-byte,
  and prove a regeneration leaves live simulation output unchanged. Bound the
  work: one step, one selected event, with a stated cost and an explicit
  truncation state when the selected contact produces more records than capacity.

- [ ] **C4 — Publish the manifold as a feature-neutral Rendering value contract.**
  Present the two bodies' poses at the event frame and the full manifold — every
  contact point, the normal, both tangents, and penetration per point — through
  generic Rendering value contracts. `ObjectContactManifold` reduces to at
  most four points (`SkullbonezSource/Physics/ObjectContactManifold.h:98`), down
  from `MAX_OBJECT_CONTACT_CANDIDATES` of 32 pre-reduction candidates, so the
  packet is small and fixed capacity. Decide whether the discarded candidates are
  worth presenting alongside the surviving rows: which points survived reduction,
  and why, is often the interesting part of a contact, and C3's `ManifoldRow`
  records carry it. Rendering must not learn what a cause row is; Planning supplies
  layout, capacity, and presentation data. Reuse the existing contact debug
  presentation path where it already serves this, rather than adding a parallel
  submission route.

- [ ] **C5 — Add the adjacent solver-row detail panel.** Add the floating detail
  surface next to the manifold visual, showing per-row solver state at the event:
  the `PhysicsSolverPersistentContactSample` fields recovered from the restored
  frame — normal, both tangents, `rA`/`rB`, penetration, `normalMass`, both
  tangent masses, `bias`, `frictionLimit`, and accumulated `accN`/`accT1`/`accT2`
  — plus the C3 per-iteration trail: warm-start impulse applied, per-iteration
  normal and friction impulse deltas, the friction-cone clamp state per
  iteration, and the final cache store. Rows are fixed capacity with an explicit
  truncation state. Units and sign conventions must be stated in the panel, not
  inferred by the reader.

- [ ] **C6 — Pair the visual and the panel as one focused surface.** Place the
  detail panel adjacent to the manifold visual and keep the pairing coherent
  under drag, resize, scene reload, and a scrub that leaves the event frame.
  Selecting a different causal row re-targets both together or neither. Define
  and enforce the invalidation rule: leaving the event frame must visibly
  invalidate the panel rather than leave stale rows that read as current. The
  existing `RunReplayCauseTreeState` window placement fields are the precedent
  for placement ownership; do not introduce a second placement convention.

- [ ] **C7 — Prove the cost and the allocation posture.** Measure the regeneration
  step and the added presentation work against the existing overlay budget, on a
  representative dense scene rather than the marble-run fixture alone. Prove zero
  steady-state allocation: every buffer added here is fixed or reserved before
  steady gameplay, and any replay-phase growth is registered with owner, phase
  gate, hard cap, and logged growth counter, with the replay reserve inventory
  updated in the same commit. State the cost of an inspection so an operator
  action with a visible frame cost is a known trade rather than a surprise.

- [ ] **C8 — Close with tests, gates, and an independent ownership review.** Add
  focused tests named for the subsystems they pin, not for this plan: seek
  targeting and non-restorable-row refusal, regeneration equivalence against the
  recorded frame, manifold packet capacity and truncation, and panel value
  mapping including units. Run the mapped gates below. Obtain an independent
  ownership review answering the five questions in the `AGENTS.md` review model,
  with particular attention to whether the new Planning owner has absorbed
  unrelated transport, presentation, and diagnostics authority and become the
  next god object.

## Validation

Per-phase, using the smallest gate in the `AGENTS.md` file-to-validation map.
Rows are cumulative; the replay visual-fidelity gate supplements rather than
replaces the normal gate.

| Phase | Required gate |
|---|---|
| C0 | `tools\validate_fast.bat` |
| C1 | `tools\validate_full.bat` (Runtime), then `tools\validate_replay_visual_fidelity.bat` |
| C2 | `tools\validate_full.bat`, then `tools\validate_replay_visual_fidelity.bat`; add `tools\validate_physics.bat` to prove a differently-timed or interrupted arrival changed no simulation bit |
| C3 | `tools\validate_physics.bat` for the equivalence proof, `tools\validate_full.bat`, then `tools\validate_replay_visual_fidelity.bat` |
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
  non-restorable row disables the transport with a stated reason rather than
  failing silently.
- The two participating objects and every reduced manifold point are presented
  at the event frame, with the point count matching the row's
  `manifoldPointCount` or an explicit truncation state.
- The detail panel shows recovered per-row solver state and the regenerated
  per-iteration trail, with stated units and sign conventions.
- Regeneration reproduces the recorded frame byte-for-byte, proved by comparison
  rather than asserted, and a regeneration leaves live simulation output
  unchanged.
- No `SkullbonezSource/Rendering` type, constant, or function names a Runtime
  feature domain; no `SkullbonezSource/UI` file includes Runtime or Rendering;
  no part of the feature lives in `Runtime/Replay/` or `Runtime/Prediction/`.
- Zero steady-state allocation, with any replay-phase growth registered and the
  reserve inventory updated in the same commit.
- The camera arrives with the eye position lerped and the orientation slerped or
  nlerped by an explicitly recorded decision, the look target cannot collapse
  into the eye mid-flight, and no second tween owner was introduced.
- A differently-timed, interrupted, or repeated camera arrival changes no
  simulation bit, proved by the physics gate rather than asserted.
- Measured cost against the overlay budget is recorded, on a dense scene, and
  includes whatever playback the chosen transport semantics performs.
- An independent ownership review finds no unrelated responsibility absorbed into
  the new Planning owner.

## Open Questions

Resolve these inside the owning phase; do not treat them as settled.

1. Can every cause row kind address a restorable frame, or do terrain and
   prediction rows need a distinct rule? C0 answers this and its answer shapes
   C1's refusal path.
2. Does regeneration require the isolated engine clone, or is a save-restore
   bracket around the live engine sufficient and cheaper? C3 decides by measured
   cost and by whether the bracket can be proven not to perturb live state.
3. Is the existing contact debug presentation path the right carrier for the
   manifold visual, or does a focused single-manifold view need its own packet?
   C4 decides by whether reuse forces feature vocabulary into Rendering.
4. What is the correct invalidation behavior when the operator scrubs away from
   the event frame while the panel is open — close, freeze with an explicit stale
   marker, or re-target? C6 decides, and the choice must not leave stale rows
   that read as current.
5. Which transport semantics does the operator actually want — instant restore,
   accelerated playback, or restore-to-lead-in then run forward? C2 picks one and
   records why. The plan recommends the third, but the choice is the owner's and
   it sets what "fast forward to the causal moment" means for the whole feature.
6. Should the camera arrival slerp or normalized-lerp? C2 decides on measured
   visual difference at realistic reorientation magnitudes, weighed against the
   gate, ruling, and placement cost recorded in the cross-plan note above.
7. Should an inspection be possible during live simulation, or only while paused?
   This bounds C3's isolation requirement and C7's cost budget.
