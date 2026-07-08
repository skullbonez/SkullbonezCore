# 02 — PhysicsWorld Solver Decomposition

Date: 2026-07-08
Status: Proposed
Priority: P0
Owner: Physics
Source issue: audit iss-02 (severity 5) + iss-14 (union-find copy-paste)

## Problem

[`PhysicsWorld.cpp`](../SkullbonezSource/Physics/PhysicsWorld.cpp) is 3,947
lines and buries the determinism-critical solver in one un-decomposable
function.

Verified evidence:

- [`RunSolverPhysics()`](../SkullbonezSource/Physics/PhysicsWorld.cpp:2156)
  spans L2156→~3793 (**~1,639 lines**) defining **33 local lambdas** that are
  whole pipeline stages (broadphase, narrowphase, CCD, union-find islanding,
  integrate), all mutating shared captured state — so no stage is testable in
  isolation, in the one subsystem that must be byte-exact deterministic.
- The class welds ~50 parallel `std::vector` members across unrelated concerns
  (broadphase, four sleep mechanisms, tornado gameplay, analytic buoyancy,
  union-find islanding, terrain, debug, replay serialization).
- Union-find (path-compression `find` + union-by-rank) is hand-copied **three
  times**: [L1852](../SkullbonezSource/Physics/PhysicsWorld.cpp:1852),
  [L2067](../SkullbonezSource/Physics/PhysicsWorld.cpp:2067), and the
  narrowphase island builder (~L3081) — each over its own parallel arrays.
- `CaptureReplaySolverSnapshot` / `RestoreReplaySolverSnapshot` open-code ~250
  lines of field-by-field mirroring that must be edited in lockstep with every
  member.

## Goal

The solver is a sequence of separately-testable stages over explicit buffers;
gameplay (tornado, buoyancy) lives outside the core world type; union-find
exists once; replay snapshotting is table-driven.

## Approach

- [ ] **Phase 0 — Extract `DisjointSet` (quick win).** One helper (path
  compression + union-by-rank) over a caller-supplied index range; replace all
  three copies. Removes ~90 duplicated lines. **Merge order is
  determinism-sensitive — preserve it exactly.**
- [ ] **Phase 1 — Lift the 33 lambdas** into named stage functions taking
  explicit inputs (`bodyStore`, `colliderStore`, `worldForces`, `dt`) and
  returning explicit outputs. No shared captured mutable state. Each stage
  becomes unit-testable.
- [ ] **Phase 2 — Evict gameplay.** Move tornado capture/eject arrays and
  analytic sphere-cap buoyancy into their own systems; `PhysicsWorld` stops
  owning gameplay state.
- [ ] **Phase 3 — Table-drive replay snapshot.** Replace the ~250-line hand
  mirroring with a single field list / serializable record so members can't
  drift out of lockstep.

## Risks / determinism

Byte-exact determinism is a hard invariant. Every phase must be
behavior-preserving; run the byte-exact CSV gate after **each** phase, not just
at the end. Do not reorder floating-point accumulation or island-merge order.

## Step-by-step implementation

Do steps in order. After every step that changes code, run the gate named in the
step and commit before starting the next. **Byte-exact physics is a hard gate —
never commit a red `validate_physics`.** Never reorder float accumulation or
island-merge tie-breaks.

### Phase 0 — DisjointSet (execution slot 1)

- [ ] **0.1** Open `PhysicsWorld.cpp` and read the three union-find copies near
  L1852-1900 (`WakePointJointIsland`), L2067-2119
  (`WakePointJointConnectedBodies`), and L3081-3117
  (`findObjectNarrowphaseRoot`/`unionObjectNarrowphaseRoots`). Write down the
  exact tie-break each uses (which root wins on equal rank) — it must be
  preserved. No code change.
- [ ] **0.2** Add a `DisjointSet` helper (suggested: `Physics/DisjointSet.h`)
  operating on caller-supplied `parent`/`rank` buffers sized to a passed count:
  `find(i)` with path compression, `unite(a,b)` union-by-rank using the **same
  tie-break** from 0.1. No callers yet. Build only (`validate_build Profile`).
  Commit.
- [ ] **0.3** Replace copy 1 (`WakePointJointIsland`) with the helper over
  `m_sleepIslandParent`/`m_sleepIslandRank`. Keep all surrounding logic
  identical. Gate: `validate_physics` byte-exact. Commit.
- [ ] **0.4** Replace copy 2 (`WakePointJointConnectedBodies`). Gate:
  `validate_physics`. Commit.
- [ ] **0.5** Replace copy 3 (narrowphase island builder) over
  `m_objectNarrowphaseParent`/`Rank`. Gate: `validate_physics`. Commit.
- [ ] **0.6** `rg -n "= find|union.*Root|findIsland" SkullbonezSource/Physics` —
  confirm no inline union-find remains. Tick Phase 0.

> After 0.6, STOP and move to the next plan in the run order (12). Return here for
> Phase 1 at execution slot 9.

### Phase 1 — Lift the 33 lambdas (execution slot 9)

- [ ] **1.1** In `RunSolverPhysics` (L2156→~3793) list the 33 lambdas and group
  them by stage (broadphase candidate / sweep pair / narrowphase island build /
  terrain detect / wake-sleep / integrate). Paste the grouped list here as a
  sub-checklist. No code change.
- [ ] **1.2** For **one stage at a time**: extract its lambda(s) into a named
  `static` free function taking explicit parameters (`bodyStore`,
  `colliderStore`, `worldForces`, `dt`, plus the specific arrays it uses) instead
  of captures. Do not change computation order. Gate: `validate_physics`
  byte-exact. Commit. Repeat until all stages are extracted.
- [ ] **1.3** `RunSolverPhysics` is now a short driver calling named stages;
  confirm it is under ~300 lines.
- [ ] **1.4** Add a unit test for at least one now-pure stage (e.g. broadphase
  candidate) — coordinate with plan 05. Gate: `validate_tests`. Commit.

### Phase 2 — Evict gameplay

- [ ] **2.1** Move tornado capture/eject arrays + methods out of `PhysicsWorld`
  into a `TornadoGameplay` system that `PhysicsWorld` calls. Gate:
  `validate_physics`. Commit.
- [ ] **2.2** Move analytic buoyancy (`RefreshUnderwaterSubmersionForBall`) into
  a buoyancy system. Gate: `validate_physics`. Commit.

### Phase 3 — Table-drive the replay snapshot

- [ ] **3.1** Replace the ~250-line field-by-field
  `CaptureReplaySolverSnapshot`/`RestoreReplaySolverSnapshot` with one field list
  (X-macro or a serialisable record) so members cannot drift out of lockstep.
  Gate: `validate_physics` + replay scrub regression. Commit.

## Validation

`tools\validate_physics.bat` (byte-exact CSV diff) after every phase;
`tools\validate_physics_deep.bat` before sign-off.

## Acceptance (structural)

- [ ] `DisjointSet` appears exactly once; the three inline copies are gone.
- [ ] No physics function exceeds ~300 lines; `RunSolverPhysics` is a short
  driver calling named stages.
- [ ] Solver stages have unit tests exercising them in isolation.
- [ ] Replay snapshot capture/restore is driven by one field list.
- [ ] `tools\validate_physics.bat` byte-exact diff stays green throughout.
