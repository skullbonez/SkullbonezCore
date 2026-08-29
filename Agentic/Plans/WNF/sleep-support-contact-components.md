# Sleep Support Contact Components

Date: 2026-08-30
Status: WNF — owner-requested design record; 0/5 phases committed
Owner: Physics sleep controller
Impact area: Physics sleep grouping, contact wake admission, deterministic scenes, tests, diagnostics, and performance

## Owner Direction

Keep this plan under `WNF/`. The owner requested an uncommitted implementation
prototype on `codex/replay-capture-bugfixes` for morning review. The plan document
is committed, but no implementation file, scene, test, tool, generated output, or
baseline from the prototype may be committed without the owner's later review.

Do not tune friction, damping, restitution, gravity, solver iterations, or the
configured quiet thresholds. Do not make an existing threshold more permissive.
The change must repair which contact edges share sleep progress and which contacts
may wake a sleeping body.

## Problem And Current Evidence

`PhysicsSleepController::RunIslandStageMode` currently joins every persistent
dynamic-to-dynamic contact before it evaluates quietness, support, or terrain
inhibition. One moving, unsupported, or inhibited neighbor therefore resets the
quiet counter of every body in the connected contact component. Transient side
contacts also change the disjoint-set root, making otherwise stable bodies restart
their quiet run as contact topology changes.

Owner-supplied evidence from 2026-08-30 records:

- `sleep_test_edge.scene.json`: the leaning body sleeps at tick 140 after 118
  inhibited rows and three counter resets;
- `sleep_test_corner.scene.json`: both bodies sleep at tick 35 with no inhibition
  or counter reset; and
- `prediction_ragdoll_wall_200.scene.json`: quiet wall bricks remain awake until
  ticks 611-1609 because unrelated moving neighbors share their contact component.

A rejected local experiment retained tangent friction on unsupported terrain-edge
contacts. It reduced peak motion in the two-body edge scene but delayed its sleep
and left 33 bodies awake in the wall at tick 1999. That experiment is not part of
this plan.

## Design

### One Explicit Contact-Edge Classifier

Physics owns one deterministic contact-edge classification with three results:

1. `StableSupport`: the existing solver-produced support candidate joins sleep
   progress only while both endpoints are quiet. A terrain-footprint veto
   classifies that terrain edge; it does not poison a distinct object-support
   edge at the same endpoint.
2. `ImpulseTransfer`: the contact-point closing speed reaches the existing linear
   sleep threshold. This edge does not join quiet counters, but it is eligible to
   wake a sleeper.
3. `Transient`: side contact, unsupported edge/point contact, separation, or
   sub-threshold closing motion. It remains a solver contact but shares neither
   sleep progress nor wake authority.

Classification order matters. A support candidate admitted by current endpoint
state remains `StableSupport` even when its ordinary gravity-support impulse
exceeds the quiet speed threshold; this prevents the gravity load itself from
turning a settled stack into an impact edge. No new numeric threshold is
introduced. The support producer retains its existing geometric boundary, while
the classifier reuses the configured linear sleep threshold for wake admission.

### Sleep Progress Components

Build the sleep disjoint set from:

- `StableSupport` object contacts whose two endpoints currently pass the existing
  linear/angular quiet gate;
- point-joint constraints, because their error and motion are one constrained
  system; and
- an existing positive sleeping visual id, so explicit wake and replay-restored
  sleeping membership retain their current deterministic grouping.

Do not join `ImpulseTransfer`, `Transient`, or stable-support contacts with a moving
endpoint. Support propagation still walks the complete directed `StableSupport`
graph from supporter to supported body; endpoint quietness filters only disjoint-set
membership. The support graph decides whether a body has a credible support path,
while the disjoint set decides only which quiet bodies must finish the same run.

This separation lets a supported quiet group advance while an adjacent brick is
sliding, rotating, toppling, or temporarily touching its side. A vertically loaded
support chain still advances as one group, and a stretched point joint still blocks
its complete joint component.

`supportsRestingPolicy` remains the terrain owner's sleep authority. An exact
terrain manifold with a narrow-footprint veto cannot be promoted into a support
anchor merely because contact exists. That veto does not suppress independent
object or joint support at the same body. A quiet body may advance and transition
while a raw terrain veto is present only when current propagated support proves
that independent path. A quiet unsupported manifold gap holds rather than
increments a positive counter, and the raw veto blocks its transition. Nonquiet
motion resets the counter. This uses the existing counter as the history witness
and adds no retained state or threshold.

### Meaningful Wake Transfer

For an awake/sleeping persistent overlap, build the existing exact manifold and
measure relative contact-point velocity, including angular velocity at every
contact arm. Wake only when at least one row has closing-speed squared at or above
the existing linear sleep threshold squared. Pure tangent motion and separating
motion do not wake the sleeper.

Swept impacts retain their current exact-hit wake path. Point-joint wake, explicit
visual-group wake, underwater locking, cache invalidation, current-step force
application, and sorted awake-list publication remain unchanged.

## Storage And Ownership Decision

Do not add a field to `PhysicsBodyRecord` or any hot Physics store. Classification
is derived once from solver-owned contact rows and frame-owned body velocities.
The sleep controller reuses its scene-reserved disjoint-set and flag arrays; the
narrowphase uses the already-built temporary manifold. No retained allocation,
new runtime reserve owner, callback, service object, or post-start growth is
permitted.

## Phases

- [ ] **SSC0 — Lock the classifier and negative controls.** Add the named edge
  enum and Physics-owned policy method. Pin admitted stable support, vetoed
  support, transient contact, threshold-below, threshold-equal, and
  threshold-above cases. Prove classification does not read friction or change
  any configured value.
- [ ] **SSC1 — Build sleep progress from stable constraints only.** Replace the
  unconditional persistent-contact union with quiet-endpoint `StableSupport`
  admission while retaining the complete directed graph for support propagation.
  Retain point-joint and sleeping visual-id grouping. Add a four-body regression
  in which a quiet supported pair advances while a moving vertical neighbor does
  not join it and a terrain-vetoed endpoint retains an independent object-support
  edge. Pin supported progress and transition during a raw terrain veto, the veto
  on an unsupported transition, quiet unsupported-gap hold, and nonquiet reset.
- [ ] **SSC2 — Require contact-point closing motion for persistent wake.** Measure
  manifold point velocity before waking a persistent overlap. Add normal-closing,
  angular-closing, tangent-only, separating, just-below, and exact-threshold
  cases. Retain the swept-hit, point-joint, explicit wake, underwater, and
  deterministic parallel pair-order tests.
- [ ] **SSC3 — Add the authored scene regression lane.** Promote the two minimal
  scenes as deliberate Physics fixtures and add a bounded CSV analyzer plus one
  command that runs them with the existing 200-brick wall scene. Report the last
  sleep transition, maximum final sleep tick, final sleeping count, and every
  sleeping-to-awake transition in the observation tail.
- [ ] **SSC4 — Prove deterministic closure without committing the prototype.**
  Run the focused unit family, Debug affected-target build, the authored scene
  lane twice, a 0/1/4-worker comparison, `tools\validate_physics.bat`, allocation
  and dependency checks, compiler-backed source design, and focused performance.
  Obtain one independent read-only ownership review. Do not run the terminal
  plan-completion command or refresh a baseline before the owner accepts the
  uncommitted implementation.

## Scene Acceptance

All tick values are Physics ticks at the existing 120 Hz fixed step. The checked
analyzer owns exact names/prefixes and fails if an expected body is absent.

| Scene | Required bodies | Maximum final sleep tick | Observation tail | Negative control |
|---|---|---:|---:|---|
| `sleep_test_corner.scene.json` | `sleep_ground_brick`, `sleep_leaning_corner_brick` | 60 | through tick 3599 | neither body wakes after its final sleep |
| `sleep_test_edge.scene.json` | `sleep_ground_brick`, `sleep_leaning_edge_brick` | 220 | through tick 3599 | the leaner must remain awake through tick 90 so edge balance is not frozen early |
| `prediction_ragdoll_wall_200.scene.json` | all 200 `prediction_wall_brick_*` bodies | 1300 | at least 600 ticks after the last wall sleep | all 200 finish asleep and no wall brick wakes in the tail |

The wall target is intentionally below the measured 1609-tick maximum and above
the faster representative bodies. If the implementation cannot meet it without a
new tuning value, report the result and reject the implementation rather than
moving the target.

## Determinism And Performance Acceptance

- Two clean-process scene-lane runs are byte-identical for each CSV.
- The same scene outputs and sleep-transition summaries are byte-identical with
  0, 1, and 4 Physics workers.
- The normal-closing wake case and all component roots are deterministic under
  reversed contact input order.
- No per-frame allocation or new Physics reserve registration appears.
- The 200-brick scene does not regress Physics time by more than 5 percent in an
  alternating same-executable comparison; the component classifier is linear in
  persistent contact rows.
- Existing byte-exact Physics output either passes unchanged or follows the active
  Physics golden-transition rules after owner review. The prototype must not
  refresh any Physics, Replay, visual, SkullScope, or performance baseline.

## Validation Map

| Change | Required evidence before owner review |
|---|---|
| Edge classification and component construction | focused `TestPhysicsStageState` cases; Debug `SKULLBONEZ_TESTS`; compiler-backed source-design scan |
| Persistent-contact wake admission | focused narrowphase wake cases including parallel/reversed ordering |
| Minimal and wall scene fixtures/analyzer | analyzer self-test; two clean runs; 0/1/4-worker byte comparison; exact tick/tail report |
| Physics behavior | `tools\validate_physics.bat`; dependency and allocation-policy repository scans |
| Hot contact/sleep path | `tools\validate_perf.bat` plus direct zero-allocation review |
| Documentation-only plan commit | `git diff --check`; no repository validation required |

## Planned Files

- `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp`
- `SkullbonezSource/Physics/SleepIslandSystem.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezTests/TestPhysicsStageState.cpp`
- `SkullbonezData/scenes/sleep_test_corner.scene.json`
- `SkullbonezData/scenes/sleep_test_edge.scene.json`
- `SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json` (read-only fixture)
- `tools/check_sleep_component_regression.py`
- `tools/validate_sleep_component_regression.bat`

## Review Questions

1. Does any non-support contact still merge sleep counters?
2. Can a stable support contact be misclassified as an impact solely because of
   the ordinary gravity-support impulse?
3. Can tangential or separating motion wake a sleeper without contact-point
   closing motion?
4. Do meaningful normal and angular impacts still wake through the exact manifold
   path and publish the existing sorted awake transition?
5. Did the change add a hot-store field, retained allocation, second sleep owner,
   or new threshold?
6. Do comments and diagnostics describe the post-change component and wake rules
   accurately?
