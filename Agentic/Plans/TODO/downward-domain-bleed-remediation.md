# Downward Domain Bleed Remediation

Date: 2026-07-25

Owner: Rendering + Physics + Runtime/Prediction

State: IN PROGRESS (DB0-DB3 complete; DB4 next)

Ledger tasks: 6 (DB0-DB5)

Branch at registration: `main` (tip `c670e95f`)

Impact area: Rendering value contracts, DX12 retained geometry, physics body
store layout, physics/terrain boundary, buoyancy data ownership, dependency
enforcement

Priority: High. The repository's dependency proofs police *include
direction* and they pass everywhere. This plan owns the failure mode they
cannot see: **upper-layer domain concepts relocated into lower layers to keep
the arrows legal**. The include graph stays clean while concept ownership
degrades — the exact "compatibility spelling that hides the edge" pattern the
standing rules ban, expressed as data instead of includes.

Implementation mode: use `Agentic/Skills/orchestrator/SKILL.md`. This plan
requires one independent rubber-duck review at whole-plan closure.

## Registration

This plan is registered in `Agentic/Plans/MASTER-PLAN.md` as a six-task
active architecture plan inside the 2026-07-25 round-4 campaign. Binding
campaign order is: 1 `ui-renderer-hard-boundary`, 2 `replay-subsystem-partition`,
3 this plan.

Required plan-runner commit first line:

```text
Downward Domain Bleed Remediation, TASK <DONE> / 6, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

With the round-4 ledger at 19 active/future tasks and the two prior plans
complete at 13, the task percentages after DB0-DB5 are 74%, 79%, 84%, 89%,
95%, and 100%. Recalculate from the authoritative master ledger if the
portfolio changes before implementation.

## Problem And Measured Evidence

Census of 2026-07-25 at `main` tip `c670e95f`. Three concrete bleed sites:

**B1 — Replay/prediction trajectory semantics inside Rendering.**
`SkullbonezSource/Rendering/RenderCommandTypes.h:154-262` defines
`RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT = 19`,
`RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY = 24000`,
`RETAINED_TRAJECTORY_PRIORITY_SEGMENT_CAPACITY = 3000`,
`RETAINED_TRAJECTORY_MAX_DRAW_RANGES = 4096`,
`RetainedTrajectoryDrawRange`, and the inline
`AppendRetainedTrajectoryRecord` / `AppendRetainedTrajectoryContinuationRecord`
mutation logic (adjacency repair, presentation-equality packing of the
19-float record). `Rendering/DX12/RenderBackendDX12.h:132-198` adds
`RetainedTrajectoryBufferDX12`, `RetainedTrajectoryUploadPlanDX12`, and both
upload-plan builders. Seven files reference these symbols: the two Rendering
headers, `RenderBackendDX12.DynamicGeometry.cpp`, and four Runtime consumers
(`Tools/RuntimeTools.h`, `Editor/EditorTracer.cpp`,
`Replay/ReplayPredictionDrawing.cpp`, `Replay/ReplayOverlayRenderer.h`).
Every capacity, the float layout, and the continuation-repair invariant exist
for exactly one Runtime feature: prediction trajectory presentation. The
generic rendering layer intimately encodes one upper feature's data model,
and the precedent means each future retained-geometry feature grows the
backend with another bespoke buffer type.

**B2 — World terrain type inside the physics core.**
`Physics/PhysicsBodyStore.cpp:56-57` and
`Physics/TerrainContactManifold.cpp:35-36` include `../World/Terrain.h` and
`../World/TerrainSupportClassifier.h`. `PhysicsBodyRecord`
(`Physics/PhysicsBodyStore.h:95`) stores a borrowed `Geometry::Terrain*` in
every body row. The standing dependency rule bans Physics →
{Gameplay, Runtime, UI} but is silent on Physics → World, so this edge is
legal-but-unpoliced. Consequences: the deterministic physics core depends on
a concrete World-layer heightfield implementation; hot per-body data carries
a cross-package raw pointer whose validity rests on scene-reload convention;
and terrain can never be exercised by physics-only tests without dragging
World in.

**B3 — Fluid feature fields inside the universal body record.**
`PhysicsBodyRecord` carries `volume`, `projectedSurfaceArea`,
`dragCoefficient`, `submergedVolumePercent`, and the buoyancy-support
`contactEpsilon` (`Physics/PhysicsBodyStore.h:97-103`) for every body in
every scene, water or not. This is feature accretion into the shared hot
record: each future force feature that follows the precedent widens the
record for all bodies and degrades its cache profile. (The planned
data-architecture work will eventually generalize per-feature body data;
this task performs the bounded, deterministic extraction available now
without waiting for it.)

## Goal

- Rendering exposes only **feature-neutral** retained-geometry contracts:
  stream identity, revision, fixed stride, range tokens, and upload planning
  parameterized by the consumer. The 19-float trajectory layout, trajectory
  capacities, and continuation-repair semantics live with their owning
  feature in `Runtime/Prediction`.
- Physics consumes terrain through a Physics-owned boundary contract
  registered at the scene edge; no World include and no `Geometry::Terrain*`
  remains anywhere under `SkullbonezSource/Physics`. Physics → World joins
  the banned list with a standing proof.
- Buoyancy per-body facts live in a BuoyancySystem-owned dense store aligned
  with body rows; `PhysicsBodyRecord` loses its fluid fields.
- All three boundaries are mechanically enforced so the bleed cannot creep
  back.
- Physics behavior stays byte-exact throughout: the 44,401-line CSV oracle is
  the non-negotiable proof for B2 and B3.

## Non-Goals

- No visual, timing, or behavioral change of trajectory presentation; the
  same bytes reach the same draws through relocated ownership.
- No general ECS/component migration and no redesign of `PhysicsBodyStore`'s
  SoA layout beyond removing the ruled fields; the planned data-architecture
  campaign owns the general problem.
- No terrain collision algorithm change — the same math in the same order.
  This plan moves the *boundary*, not the *behavior*.
- No new runtime polymorphism: the terrain boundary is a value/borrow
  contract, not a virtual interface.
- No physics baseline, golden, screenshot, replay artifact, scene, or config
  refresh. Divergence is reverted, never normalized.
- No relocation of `PhysicsWorldForces` fluid *inputs* (surface height,
  density): world-force values remain step inputs; only per-body derived
  facts move.

## Permanent Invariants

1. `SkullbonezSource/Physics` includes only `Physics/`, `Core/`, and
   `Maths/` headers. World, Scene, Assets, Gameplay, Runtime, and UI are all
   banned, mechanically.
2. No type, constant, or function under `SkullbonezSource/Rendering` names a
   Runtime feature domain (trajectory, replay, prediction, planning, cause
   tree, porkchop, operator panel vocabulary). Rendering contracts are
   feature-neutral; the feature supplies sizes and layouts as values.
3. `PhysicsBodyRecord` and the hot SoA arrays gain a new per-body field only
   with an owner ruling that names the consuming stage and why a stage-owned
   parallel store is insufficient. The ruling lives in the owning plan or
   commit body.
4. Feature-owned retained-geometry data enters the backend through the
   generic stream contract; a feature-named buffer type in the backend is a
   review failure.
5. Terrain reaches physics only through the Physics-owned boundary contract
   established at the scene edge; no physics row retains a World-layer
   pointer.

## Ledger

- [x] **DB0 — Ratify the complete bleed census.**

  From the implementation tip, inventory: every `RETAINED_TRAJECTORY*` /
  `RetainedTrajectory*` symbol and consumer with file:line; every include
  from `SkullbonezSource/Physics` into World/Scene/Assets (and confirm the
  Gameplay/Runtime/UI proofs still return zero); every read/write site of the
  five fluid fields and the `terrain` pointer in `PhysicsBodyRecord`,
  classified by stage and phase (authoring-cold versus fixed-step-hot); and a
  sweep of `SkullbonezSource/Rendering` for any *other* Runtime feature
  vocabulary so invariant 2 starts from a complete list, not one example.
  Design the two target contracts at value level: the feature-neutral
  retained-geometry stream (identity, revision, stride, range token, upload
  plan) and the Physics terrain boundary (query surface actually used by
  `TerrainContactManifold`, `PhysicsBodyStore`, and the terrain stage — the
  exact function set, not a speculative abstraction). Record expected touched
  files for the final comment audit.

  Acceptance:

  - The census reconciles to the three B1/B2/B3 evidence blocks or documents
    drift; any additional bleed site found is added to the disposition table
    with an owner and target task.
  - Both boundary contracts are specified with exact signatures, capacity
    ownership, and lifetime rules.
  - The determinism strategy for B2/B3 is written down: which functions move,
    why float arithmetic order is unchanged, and which gates prove it.
  - No source behavior changes in DB0.

  Evidence (2026-07-26):

  - `Agentic/Reports/2026-07-26/downward-domain-bleed-remediation-db0-census.md`
    records all fourteen exact-symbol B1 source/test files, the complete
    Rendering vocabulary sweep, all thirteen upward Physics include rows, every
    terrain query and body-field phase, and the expected final comment-audit
    scope.
  - The implementation-tip drift is bounded: nine unused
    `Assets/AssetKeys.h` includes are registered as B4 and assigned to DB2.
    Physics still has zero Scene, Gameplay, Runtime, or UI include rows.
  - The report fixes exact feature-neutral retained-geometry and
    `PhysicsTerrainView` signatures, chooses one scene-lifetime terrain slot,
    defines the dense `BuoyancySystem` row lifecycle, and records byte-exact
    arithmetic-order strategy for DB2/DB3.
  - Documentation only; no repository validation required and no source
    behavior, baseline, golden, artifact, scene, config, shader, or physics CSV
    changed.

- [x] **DB1 — Make the Rendering retained-geometry contract feature-neutral.**

  Generalize the retained stream types in `RenderCommandTypes.h` and the DX12
  buffer/upload-plan machinery so stride, capacities, and range identity are
  consumer-supplied values. Move the 19-float record layout, trajectory
  capacities, continuity tolerance semantics, and both append/continuation
  helpers into `Runtime/Prediction` (created by `replay-subsystem-partition`
  RS1). Update the four Runtime consumers. Delete every
  `RETAINED_TRAJECTORY*` and `RetainedTrajectory*` spelling from
  `SkullbonezSource/Rendering`; renames and aliases do not satisfy deletion.

  Acceptance:

  - `rg -n --ignore-case 'trajectory' SkullbonezSource/Rendering` returns no
    rows.
  - The DX12 upload-plan behavior is value-equivalent: the existing
    plan-builder unit coverage is retained/extended against the generic types
    with the same cached/incoming token cases.
  - Trajectory presentation is pixel-identical: DX12 renderer gate accepted
    with unchanged committed baselines, plus the single-invocation replay
    visual-fidelity gate passing with zero refresh.
  - No dynamic allocation, callback, or virtual seam was introduced; the
    generic contract stays constexpr/value-shaped like the current code.

  Evidence (2026-07-26):

  - `Agentic/Reports/2026-07-26/downward-domain-bleed-remediation-db1-retained-geometry.md`
    records the feature-neutral stream/range/capacity contract, Prediction-owned
    record packing and retention, exact vocabulary proofs, and the complete
    touched-source comment-audit checklist.
  - `tools\validate_dx12_renderer.bat` passed with 43 current stages, zero DX12
    validation errors, and all three committed screenshot comparisons accepted
    without refresh; `tools\run_graphics_stress.bat 1` passed on the final
    runtime source.
  - `tools\validate_tests.bat` passed 391 cases / 2,403,286 assertions.
  - The replay visual-fidelity launcher ran exactly one 6,800-frame engine
    process. Its initial comparison correctly rejected a temporary physical
    shader-path rename; DB1 restored the approved shader tree byte-for-byte
    (`9eb658302f3258db762f4383f572ecde5e95a7be05df81f23c1bc069ad434b02`)
    and all ten non-engine report/control checks then passed against that same
    captured run. No baseline, golden, artifact, shader, scene, config, or
    screenshot reference was refreshed.

- [x] **DB2 — Move terrain behind a Physics-owned boundary.**

  Introduce the DB0-specified Physics-owned terrain contract (a
  Physics-package value/borrow view over heightfield sampling and support
  classification), registered once at the scene boundary by the owner that
  already supplies terrain to bodies. Replace the per-body
  `Geometry::Terrain*` with the physics-owned representation the census
  ruled (single scene-terrain slot or per-body index — decided in DB0 from
  actual usage). Delete both `../World/` includes from `Physics/`; World's
  `TerrainSupportClassifier` logic that only physics consumes moves into the
  physics package if the census proves World has no other consumer, otherwise
  the classification crosses as precomputed values.

  Acceptance:

  - `rg -n '^#include[[:space:]]+.*(World|Scene|Assets)/' SkullbonezSource/Physics`
    returns no rows.
  - `rg -n 'Geometry::Terrain' SkullbonezSource/Physics` returns no rows.
  - `tools\validate_physics.bat` passes with the 44,401-line CSV byte-exact —
    no baseline motion of any kind. If any byte moves, the task reverts and
    reworks; the danger-zone determinism rule applies in full.
  - Physics unit/store tests exercise terrain contact paths without linking
    World sources, proving the boundary is real.

  Evidence (2026-07-26):

  - `Agentic/Reports/2026-07-26/downward-domain-bleed-remediation-db2-terrain-boundary.md`
    records the Physics-owned cell/view contract, SceneWorld replacement
    lifetime transaction, removed per-body terrain authority, exact zero-row
    dependency proofs, and the 46/46 touched-source comment audit.
  - `tools\validate_physics.bat` passed on final source. Its 88,802-row output
    contains two byte-identical 44,401-row runs and matches the committed
    baseline without refresh.
  - `tools\validate_tests.bat` passed 392 cases / 2,403,298 assertions;
    `tools\validate_perf.bat` and `tools\validate_dependency_graph.bat` passed
    with zero allocation-policy or dependency findings.
  - No baseline, golden, replay artifact, screenshot, shader, scene, config,
    or physics CSV reference was refreshed.

- [x] **DB3 — Move fluid per-body facts to the buoyancy owner.**

  Extract `volume`, `projectedSurfaceArea`, `dragCoefficient`,
  `submergedVolumePercent`, and the buoyancy-support `contactEpsilon` from
  `PhysicsBodyRecord` into a BuoyancySystem-owned (or force-stage-owned, per
  DB0 census of actual consumers) fixed-capacity dense store aligned to body
  rows and populated at the same authoring/refresh boundaries that fill the
  record today. Preserve identical read values, read order, and arithmetic in
  every consuming stage. Update replay solver snapshot/restore if any moved
  field participates (DB0 must have ruled this).

  Acceptance:

  - `PhysicsBodyRecord` contains none of the five fields; the store's
    alignment static_asserts are updated with the surviving layout proven.
  - `tools\validate_physics.bat` byte-exact, and
    `tools\validate_physics_deep.bat` passes if any moved field feeds
    SkullScope/known-signature baselines.
  - `tools\validate_perf.bat` shows no hot-path regression and zero
    allocation-policy violations (the new store is fixed-capacity,
    scene-load-reserved).
  - Replay solver snapshot round-trip coverage passes unchanged if touched.

  Evidence (2026-07-26):

  - `Agentic/Reports/2026-07-26/downward-domain-bleed-remediation-db3-buoyancy-owner.md`
    records the final field-owner map, aligned lifecycle transaction, stage
    reads, determinism result, static proofs, and 27/27 comment audit.
  - `PhysicsBodyRecord` contains none of the five registered fields.
    `BuoyancyBodyFacts` is statically fixed at five floats and lives in one
    8,192-row `PhysicsFixedList` owned by `BuoyancySystem`.
  - `tools\validate_physics.bat` passed with two byte-identical 44,401-line
    output runs matching the committed baseline without refresh.
  - `tools\validate_tests.bat` passed 393 cases / 2,403,315 assertions;
    `tools\validate_perf.bat` passed every scale/perf comparison with zero
    gameplay allocation violations; the allocation checker and dependency
    validator both passed with zero findings.
  - `tools\validate_physics_deep.bat` was not required: the DB0 census and
    final source scan prove none of the moved fields feeds SkullScope or a
    deep known-signature baseline.
  - Replay solver snapshot/artifact schemas were untouched because none of
    the moved facts participates in either contract.
  - No baseline, golden, replay artifact, screenshot, shader, scene, config,
    or physics CSV reference was refreshed.

- [ ] **DB4 — Install the anti-bleed enforcement.**

  Update `AGENTS.md`: extend the Physics dependency sentence and proof to ban
  World/Scene/Assets explicitly; add invariant 2 (Rendering feature
  neutrality) and invariant 3 (hot-record field ruling) to the standing
  review rules; add the symbol-level deletion proofs below. Register the
  Physics→World/Scene/Assets edge rule and the Rendering trajectory-vocabulary
  deletion check in the dependency validator delivered by
  `ui-renderer-hard-boundary` UR5, with positive and negative fixtures (a
  planted `#include "../World/Terrain.h"` under Physics and a planted
  `RetainedTrajectory` type under Rendering must each fail mechanically).

  Acceptance:

  - All new proof commands return no rows from the tip; validator fixtures
    pass with the two planted negatives failing.
  - No frozen-count or spelling-budget ratchet was added: the vocabulary
    check is a deletion check on retired concept names plus a review rule,
    not a general word census.
  - `tools\validate_fast.bat` and the CPU umbrella invoke the extended
    validator through the established call chain.

- [ ] **DB5 — Close behavior, ownership, and documentation.**

  Re-run the complete census and every standing proof. Audit every touched
  source-bearing file against the comment-style guide. Run one independent
  rubber-duck review with an explicit hostile mandate: find any surviving
  feature vocabulary below its owner, any physics row still reaching a
  World-layer object, any generic contract that is secretly shaped around
  one consumer, and any determinism risk the gates did not cover. Any
  credible finding reopens its owning task.

  Acceptance:

  - All permanent invariants and static proofs pass from final source.
  - One independent review with no unresolved finding.
  - All mapped gates pass from final source; physics CSV byte-exact;
    zero refresh of any golden, baseline, artifact, scene, or config.
  - Closure evidence under `Agentic/Reports/<date>/`; plan deleted under
    inventory rule 4; `MASTER-PLAN.md` and `Agentic/SessionState.md` updated.

## Dependencies And Decisions

- DB1 requires `replay-subsystem-partition` RS1 (the `Runtime/Prediction`
  package exists as the trajectory semantics' destination). DB2 and DB3 are
  physics-only and independent of the partition; the binding campaign order
  still runs this plan third, and no DB task starts before its plan turn
  without an explicit owner note in `MASTER-PLAN.md`.
- DB4 requires the `ui-renderer-hard-boundary` UR5 validator; if UR5 was
  descoped, DB4 ships the proofs as standing `AGENTS.md` commands plus a
  focused checker and records the decision.
- B2/B3 are inside the physics-determinism danger zone. The byte-exact CSV
  gate is the acceptance authority; the bounded-divergence allowance in
  `MASTER-PLAN.md` does **not** apply to this plan — these are pure
  relocations and any divergence is a defect.
- Hot-path rule compliance: the new buoyancy store and terrain view are
  compact arrays/value records consumed inside existing stage loops — no
  polymorphic service objects, callbacks, or handle lookups inside hot loops.

## Static Closure Proofs

```powershell
rg -n '^#include[[:space:]]+.*(World|Scene|Assets|Gameplay|Runtime|UI)/' SkullbonezSource/Physics
rg -n 'Geometry::Terrain' SkullbonezSource/Physics
rg -n --ignore-case 'trajectory|porkchop|replay|prediction' SkullbonezSource/Rendering
rg -n 'RetainedTrajectory|RETAINED_TRAJECTORY' SkullbonezSource/Rendering
```

All four commands must return no rows at closure. If the third command's
broad vocabulary sweep produces a justified false positive (e.g. an unrelated
mathematical use of "prediction"), DB4 records the ruled exception next to
the proof rather than weakening the pattern silently.

## Validation Map

| Phase | Iteration evidence | Pre-commit/closure gates |
|---|---|---|
| DB0 | Census tables, contract specs, determinism strategy | Documentation-only; no repository validation |
| DB1 | Generic-type unit tests, include scans | `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`, `tools\validate_tests.bat`, `tools\validate_replay_visual_fidelity.bat` (single invocation) |
| DB2 | Physics-only terrain tests, include scans | `tools\validate_physics.bat` (byte-exact), `tools\validate_tests.bat`, `tools\validate_perf.bat` |
| DB3 | Store layout asserts, stage read-site diff | `tools\validate_physics.bat` (byte-exact), `tools\validate_physics_deep.bat` if SkullScope-facing, `tools\validate_perf.bat` |
| DB4 | Validator fixtures, proof outputs | `tools\validate_fast.bat`, `tools\validate_all_cpu_tests.bat`, direct validator run |
| DB5 | Final census, comment audit, review record | `tools\validate_full.bat`, plus any phase gate whose files were re-touched by remediation |

Mapped gates are cumulative with the standing file-to-validation table;
`RenderBackendDX12*`/`RenderCommandTypes.h` changes carry the DX12 renderer
and bounded-stress requirements per the danger-zone table.

## Closure Evidence Requirements

The closure report must contain:

- the complete before/after symbol and include census for all three bleed
  classes;
- the final generic stream contract and terrain boundary signatures;
- byte-exact physics proof output for DB2 and DB3 individually;
- DX12 baseline acceptance and visual-fidelity result for DB1;
- perf-gate output proving no hot-path regression from the store moves;
- validator fixture proof for every new rule, including both planted
  negatives;
- touched-source comment-audit inventory with checked/deferred counts;
- the independent review verdict and any remediation;
- confirmation that no baseline, golden, scene, config, shader, replay
  artifact, or physics CSV was refreshed.
