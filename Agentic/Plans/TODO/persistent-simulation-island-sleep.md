# Persistent Simulation-Island Sleep

Date: 2026-08-30
Status: Active owner-review implementation; 0/6 phases accepted; PSI0 next
Owner: Physics simulation-island sleep system
Impact area: Physics contact and joint topology, deactivation, wake propagation, replay state, diagnostics, authored-scene validation, and performance
Supersedes: `Agentic/Plans/WNF/sleep-support-contact-components.md`

## Owner Direction

The support-contact-component prototype is rejected. It allowed an unstable
edge-balanced body to freeze while nearby bodies continued to jitter, and its
wall acceptance could report success with only 198 of 200 bodies sleeping. Its
support-edge classifier, component rule, analyzer thresholds, and produced
evidence are not an implementation baseline.

The owner accepted the wall result on 2026-08-30 and requested a baseline refresh
and pull request from `codex/replay-capture-bugfixes`. That instruction reactivates
this plan under `TODO/` and permits the current implementation, tests, tools,
scene fixtures, and governed Physics baseline transition to be committed for
review. It does not mark any phase accepted or rewrite the rejected plan's
history; the corner and edge investigations remain open acceptance work.

Do not tune friction, damping, restitution, gravity, solver iterations, contact
slop, or the configured sleep speed and duration values to make a scene pass.
The change is a conventional deterministic simulation-island policy: contact
and joint topology defines membership, body-local motion and solver stability
define deactivation progress, and the complete island sleeps or wakes together.

## External Source Review

The design adapts policy and ownership from these official upstream sources. It
does not copy their storage layout or code.

### Box2D

Reviewed `erincatto/box2d` at commit
`617d32ab02570930625bbcb8479f54be9bf8d045`:

- [`src/island.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/island.c)
  persists islands, merges them when contacts or joints link dynamic bodies,
  records removed constraints, and splits only from the remaining topology.
  Static bodies have no island id, so contacts through the static world do not
  merge unrelated dynamics.
- [`src/solver.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/solver.c)
  advances per-body sleep time only below the motion threshold, accounts for
  angular velocity at the body's extent and position-correction motion, and
  keeps an island awake when any member has insufficient sleep time.
- [`docs/simulation.md`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/docs/simulation.md)
  describes merge-on-contact, delayed split after contact removal, island-wide
  sleep, waking sleeping bodies that an awake body touches, and deterministic
  ordering based on creation order.

### Bullet

Reviewed `bulletphysics/bullet3` at commit
`63c4d67e337017f9d8b298c900e9aabdb69296e7`:

- [`btRigidBody.h`](https://github.com/bulletphysics/bullet3/blob/63c4d67e337017f9d8b298c900e9aabdb69296e7/src/BulletDynamics/Dynamics/btRigidBody.h)
  owns body-local deactivation time, resetting it when either linear or angular
  motion exceeds its threshold.
- [`btSimulationIslandManager.cpp`](https://github.com/bulletphysics/bullet3/blob/63c4d67e337017f9d8b298c900e9aabdb69296e7/src/BulletCollision/CollisionDispatch/btSimulationIslandManager.cpp)
  constructs dynamic connectivity with union-find, excludes static/kinematic
  bodies from merging, and sleeps an island only when every eligible member is
  ready. Its deterministic mode sorts manifolds by stable body identity.
- [`btDiscreteDynamicsWorld.cpp`](https://github.com/bulletphysics/bullet3/blob/63c4d67e337017f9d8b298c900e9aabdb69296e7/src/BulletDynamics/Dynamics/btDiscreteDynamicsWorld.cpp)
  advances activation state, zeroes sleeping velocity, and returns an island to
  active state as a unit when it cannot remain deactivated.

### SkullbonezCore Adaptation

SkullbonezCore keeps fixed-capacity, dense body rows and deterministic fixed
steps. It therefore stores deactivation as fixed-step ticks, where elapsed time
is `ticks * dt`, rather than accumulating a variable floating-point wall clock.
The existing authored sleep duration maps to the required tick count. Island
identity and topology remain stage-owned parallel rows and bounded edge lists;
no field is added to `PhysicsBodyRecord` or another hot body store.

Unlike Box2D, SkullbonezCore does not move sleeping islands into separate body
sets in this change. Unlike Bullet, it does not expose per-body activation modes
as a public API. Those are storage mechanisms, not required policy. The adopted
invariants are persistent constraint topology, all-member deactivation, and
whole-island transitions.

## Failure Being Corrected

`PhysicsSleepController::RunIslandStageMode` currently rebuilds a disjoint set
every step from persistent contact rows, point joints, and a persisted visual id.
It then gates the entire component through per-frame support and a byte-sized
quiet counter. This has four structural problems:

1. a debug visual id can keep bodies joined after solver topology disconnects;
2. support eligibility is mixed into topology even though support is an edge or
   terrain fact, not a definition of the simulation constraint graph;
3. velocity-only quietness can freeze a body while solver residuals, penetration,
   or pose correction still show an unstable contact; and
4. scene acceptance has observed sleep flags and timing without proving that the
   pre-sleep pose was stable or that every required body reached a valid rest.

The rejected prototype made the topology problem worse by selecting only
"stable support" edges. A transient neighbor could then remain active outside a
sleeping body's component even while a real contact constraint still coupled the
two. That is the opposite of conventional island behavior.

## Required Invariants

### Persistent Deterministic Topology

- Every active dynamic-to-dynamic contact manifold and every active
  dynamic-to-dynamic point joint is an island edge. A support classifier never
  decides membership.
- Terrain, authored-fixed bodies, and other static endpoints anchor a constraint
  but have no island membership. Two dynamic bodies touching the same terrain or
  fixed body remain in separate islands unless a dynamic edge connects them.
- Edges use stable canonical keys. Object contact keys are ordered body identities
  plus manifold identity; joint keys use the stable constraint handle/topology
  ordinal. Input row order, worker completion order, and union rank cannot change
  the chosen canonical island identity.
- The retained edge set is compared with the current step in canonical order.
  A newly created edge merges its dynamic endpoints immediately and wakes the
  affected island. A missing prior edge marks only its previous island dirty and
  wakes the affected members.
- A dirty island splits only after the contact/joint edge is actually absent from
  the authoritative completed step. Its connected components are rebuilt from
  the remaining active edges in ascending canonical order. Ordinary steps with
  unchanged topology retain island membership and identity.
- Body creation, deletion, dense-row compaction, joint creation/update/destruction,
  replay restore, and authored topology reset invalidate or rebuild the retained
  topology explicitly. No stale body index may survive compaction.

### Body-Local Deactivation

- Each eligible awake dynamic body owns a stage-local deactivation tick count.
  The count advances only on a completed fixed step that satisfies every
  pre-sleep stability requirement. It resets to zero on motion, solver activity,
  topology activity, invalid terrain support, explicit wake, or a non-finite
  observation.
- Linear motion uses the existing configured linear sleep threshold. Angular
  motion is evaluated both against the existing angular threshold and as
  contact-relevant farthest-point speed using collider extent, so a long body
  rotating slowly cannot hide meaningful endpoint motion.
- An island sleeps only when every eligible dynamic member has reached the
  configured deactivation duration in the same completed step. A fixed or static
  endpoint is not a member and does not contribute a timer.
- Transition is whole-island and body-index ordered. Every member receives the
  same canonical island id, zero velocity, and awake-state change in one serial
  commit. A state with touching eligible bodies in the same island split between
  sleeping and awake is invalid.
- Sleeping poses are locked. A sleeping body cannot accumulate integration,
  position correction, contact-cache mutation that changes its pose, or a hidden
  non-zero velocity while it remains asleep.

### Pre-Sleep Stability

Velocity thresholds alone are insufficient. A body may advance deactivation only
when all applicable observations are stable for that step:

- linear speed, angular speed, and farthest-point speed are below the existing
  sleep boundaries;
- finite pose and velocity values are present;
- terrain footprint policy does not inhibit sleep, and an unsupported dynamic
  body cannot bank deactivation time;
- every incident contact's penetration is within the solver's existing accepted
  slop/correction envelope and is not increasing across the observation window;
- post-solve position correction/pose delta is within the existing solver
  convergence envelope;
- accumulated normal/tangent impulse deltas and point-joint constraint error are
  below a derived, unit-correct stability boundary for the complete observation
  window; and
- no incident contact or joint reports a meaningful new impulse, closing motion,
  stretched constraint, or other wake activity.

Do not introduce a magic scene-specific scalar. PSI0 must derive each mechanical
boundary from an existing configured threshold, solver slop, fixed step, body
mass/extent, or documented floating-point comparison. If the required stability
cannot be expressed without a new authored setting, stop and obtain owner review
before adding one.

### Whole-Island Wake

The following events wake every eligible member of the affected island in
canonical body order and reset their deactivation ticks:

- an awake dynamic body touches a sleeping island through an exact active
  manifold, regardless of whether the contact is classified as support;
- a solver row or point joint transfers meaningful impulse or reports meaningful
  constraint activity;
- a contact or joint is created or destroyed;
- a joint descriptor changes, a body changes fixed/dynamic eligibility, or dense
  topology changes;
- an explicit model wake, authored force/velocity change, underwater wake, or
  replay/config transition requests activation.

Awake contact is itself a deterministic wake cause; no closing-speed prefilter may
leave one side sleeping while the other side participates in the same manifold.
Impulse and constraint thresholds prevent ordinary supported load from causing
repeated wake churn after the complete island is sleeping; they do not weaken the
awake-contact rule.

## Ownership And Storage

Create or rename one Physics-owned behavior owner for simulation-island sleep.
It owns:

- canonical retained contact/joint edge keys and previous/current edge sets;
- per-body canonical island membership and topology generation;
- per-body deactivation ticks and previous-step stability observations;
- bounded split/merge scratch and island transition flags; and
- deterministic wake requests consumed at the serial PhysicsWorld boundary.

`PhysicsSleepController` may compose this owner, but must not duplicate its
topology or timer authority. `SleepIslandSystem` must either become this real
owner or be removed; it cannot remain a support-propagation helper with an island
name while a second type owns actual islands.

The contact solver retains ownership of manifolds, impulses, penetration, and
position-correction observations. It exposes a narrow immutable per-step activity
view; it does not receive sleep policy. The point-joint owner exposes stable keys
and completed-step error/impulse facts. Terrain support classification remains
terrain/diagnostic information only.

No callback pack, broad context object, service bag, retained owner pointer, hot
body-store field, post-start allocation, or new Runtime reserve privilege is
permitted. All lists reserve from existing scene-body, candidate-pair, contact,
or point-joint capacity during scene load.

Replay snapshots must restore enough retained timer and topology state for the
next fixed step to match. Prefer restoring body-local deactivation ticks and the
canonical edge/island generation explicitly. If topology is rebuilt from the
already-restored contact and joint rows instead, the restore path must prove the
same island identities and wake decisions byte-for-byte. Bump the Physics solver
snapshot version for any serialized layout change and retain prior-version
conversion or rejection according to the existing replay compatibility contract.

## Phases

- [ ] **PSI0 - Plant hard failure controls and stability observations.** Add
  focused fixtures for an edge-balanced box, a quiet supported box, touching
  awake/sleeping bodies, a jittering neighbor, contact creation/destruction,
  joint creation/destruction, and long-body angular endpoint motion. Expose
  deterministic per-body observations for farthest-point motion, penetration
  trend, position correction, impulse delta/variance, joint error, topology
  activity, and the exact reset reason. Prove each boundary at below/equal/above
  values and reject non-finite input. The old edge-frozen and 198/200 results are
  planted negative controls that must fail before behavior changes.
- [ ] **PSI1 - Own persistent canonical island topology.** Replace visual-id and
  support-defined grouping with retained canonical dynamic contact/joint edges.
  Merge on new edges, wake on edge changes, split only dirty disconnected
  components after authoritative edge removal, and exclude terrain/static
  endpoints from membership. Cover reversed manifold/joint order, body
  compaction, fixed/dynamic changes, unchanged-topology identity retention, and
  static-anchor non-merging.
- [ ] **PSI2 - Advance body-local deactivation from stability.** Replace the
  byte quiet counter with fixed-step deactivation ticks. Advance only when all
  motion, support, penetration, correction, impulse, joint, and topology facts
  are stable. Reset locally on any failure, then require every eligible island
  member to reach the duration before one whole-island transition. Cover a quiet
  lower body adjacent to a jittering upper body: neither may split state while a
  manifold connects them; after real disconnection their independent island
  timers may progress separately.
- [ ] **PSI3 - Route deterministic whole-island wake.** Make awake contact,
  meaningful solver/joint activity, edge create/destroy, explicit wake, forces,
  underwater transitions, and topology mutation activate the complete retained
  island. Remove visual-id traversal and support-edge wake as authority. Cover
  ordinary gravity load without wake churn, meaningful normal/tangent/angular
  impulse, joint error, simultaneous reversed-order requests, and sorted
  publication.
- [ ] **PSI4 - Build complete authored-scene and visual acceptance.** Retain the
  useful minimal corner and edge scenes as deliberate Physics fixtures, add a
  dedicated jittering-neighbor scene, and run the full existing 200-box ragdoll
  wall. The analyzer must delete old outputs before launch, require fresh files,
  require every expected body and every expected contiguous fixed-step row, and
  reject truncation. Record poses, velocities, sleep transitions, island ids,
  reset reasons, contact residuals, penetration, correction, and impulse
  variance. Capture fixed-camera beginning, pre-sleep, final, and worst-residual
  screenshots or scene shots for owner inspection.
- [ ] **PSI5 - Prove deterministic and performance closure without committing
  implementation.** Run focused tests/builds while iterating, then the terminal
  Physics, Replay, allocation, dependency, compiler-backed source-design, build
  configuration, and performance gates. Run the authored lane twice in clean
  processes and with 0/1/4 Physics workers. Obtain one independent read-only
  ownership and acceptance review. Do not refresh a golden baseline, commit the
  implementation, or mark the plan complete before owner review.

## Hard Acceptance

All tick values are fixed Physics steps at the existing 120 Hz rate. The command
owns the exact expected body set and fails on a missing, duplicated, gapped,
truncated, stale, or non-finite row.

### Every Selected Body

- starts awake and does not transition to sleep while terrain sleep inhibition
  is active without a separate valid constraint/support path;
- has a contiguous observation window of at least the configured deactivation
  duration immediately before sleep;
- remains within all stability boundaries throughout that window;
- has no sleeping/awake split with another eligible member of the same island;
- has exactly zero sleeping pose drift and zero hidden velocity until a recorded
  wake cause; and
- produces the same island ids, reset reasons, transitions, poses, residuals,
  and final state across repeated and worker-count runs.

### Required Scenes

| Scene | Required result | Hard failures |
|---|---|---|
| `sleep_test_corner.scene.json` | Both named bodies reach a visually stable supported pose, pass the complete pre-sleep window, sleep as their true island topology permits, and remain pose-locked through tick 3599. | Initial/pre-stabilized sleep, residual or pose drift at transition, late wake, incomplete rows. |
| `sleep_test_edge.scene.json` | The leaning body may sleep only after its center of mass/contact footprint and all residual observations are stably admissible. If it remains edge-balanced or inhibited, it stays awake and resolves physically instead of freezing. | The previously observed edge-balanced frozen pose is an unconditional failure even if sleep timing passes. |
| New jittering-neighbor fixture | While an active manifold connects the quiet and jittering bodies, they share one island and neither may enter a split sleep state. After the manifold truly disappears, the retained island splits and the stable component progresses independently. | Per-frame support-edge split, stale joined topology, touching awake/sleeping pair. |
| `prediction_ragdoll_wall_200.scene.json` | All 200 named wall bodies reach stable poses, satisfy complete pre-sleep windows, finish asleep, remain pose-locked for at least 600 ticks after the last transition, and show no visible jitter/frozen unstable balance. | 199/200 or fewer asleep, including the prior 198/200 result; any late wake; any unstable edge-balanced sleeper; any body with persistent jitter/residual hidden by a sleep flag. |

The owner reviews the scene shots in addition to the mechanical report. A green
sleep count cannot override visibly bad geometry, a balancing edge, penetration,
or continuing jitter. The analyzer emits the worst body/tick and the associated
pose/contact metrics so a visual defect cannot disappear into an aggregate.

## Determinism And Performance Acceptance

- Two clean-process outputs are byte-identical for every authored scene.
- Outputs are byte-identical with 0, 1, and 4 Physics workers, including topology
  edge order, canonical island identity, wake reasons, and per-body timers.
- Reversing simultaneous contact/joint creation or removal inputs chooses the
  same canonical island ids and transition order.
- Replay capture/restore on a partly deactivated island produces the exact next
  step, topology, timer, and wake result as uninterrupted simulation.
- No per-step allocation, retained growth, or new reserve registration appears.
- The 200-body scene does not regress alternating same-executable Physics time by
  more than 5 percent. Retained edge comparison, dirty-island split, timer update,
  and transitions remain linear or near-linear in bodies plus active constraints.
- Existing Physics/Replay byte-exact output passes unchanged. Any true intended
  golden transition requires separate owner approval after visual review; the
  prototype may not refresh it.

## Validation Map

| Change | Required evidence before owner review |
|---|---|
| Persistent contact/joint topology | focused topology/merge/split/static-anchor tests; reversed-order controls; Debug affected-target build |
| Deactivation and stability | boundary tests for motion, extent, penetration, correction, impulses, joint error, support, and non-finite rows |
| Whole-island wake/sleep | awake-contact, impulse, joint, topology, explicit, force, underwater, and replay-restore tests |
| Authored fixtures/analyzer | analyzer self-test with planted stale/gap/truncation/initial-sleep/pose-drift/198-of-200 failures; two clean runs; 0/1/4 worker comparison; scene shots |
| Physics behavior | `tools\validate_physics.bat`; focused Replay capture/restore; dependency and allocation-policy scans |
| Changed source design | compiler-backed source-design and build-configuration consistency gates on every affected translation unit |
| Hot path/performance | `tools\validate_perf.bat`; direct capacity/allocation review; 200-body alternating measurement |
| Documentation-only plan commit | `git diff --check`; no repository validation required |

## Planned Files

Exact source placement is decided in PSI0 after the stability observations are
mapped. Expected owners are:

- `SkullbonezSource/Physics/SleepIslandSystem.h`
- `SkullbonezSource/Physics/SleepIslandSystem.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
- `SkullbonezSource/Physics/PhysicsSolverSnapshot.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezTests/TestPhysicsStageState.cpp`
- a focused Physics island-sleep test translation unit if the existing file would
  exceed source-design limits;
- `SkullbonezData/scenes/sleep_test_corner.scene.json`
- `SkullbonezData/scenes/sleep_test_edge.scene.json`
- a new jittering-neighbor scene fixture;
- `SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json` as a read-only
  existing fixture; and
- a complete island-sleep analyzer and validation command under `tools/`.

The user-owned working-tree edits in `PersistentContactSolver.cpp`,
`TestPersistentContactSolver.cpp`, `Agentic/Reports/`, and supplied scene files
are outside cleanup and staging. The implementation must work around them unless
the owner explicitly expands scope.

## Review Questions

1. Does every active dynamic contact manifold and joint define island topology,
   independent of support classification?
2. Can terrain or one fixed body accidentally merge two unrelated dynamics?
3. Can an island split before the authoritative edge actually disappears, or
   retain a stale edge after removal/body compaction?
4. Does any eligible member sleep before every member reaches the complete
   body-local deactivation and stability condition?
5. Can touching eligible bodies in one island become split sleeping/awake state?
6. Does awake contact or meaningful constraint activity wake the complete island
   in canonical order without ordinary sleeping support load causing churn?
7. Can velocity thresholds hide penetration, pose correction, impulse variance,
   non-finite state, or an edge-balanced unstable pose?
8. Does replay restore reproduce retained topology, deactivation timers, and the
   exact next transition?
9. Do all 200 wall bodies finish stably asleep with a pose-locked tail, and do
   scene shots agree with the mechanical report?
10. Did the change add hot-store state, post-start allocation, broad ownership,
    or a scene-specific tuning value?
