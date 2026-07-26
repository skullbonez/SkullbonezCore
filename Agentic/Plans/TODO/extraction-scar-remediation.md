# Extraction Scar Remediation

Date: 2026-07-26
Status: NOT STARTED — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 7 of the Architecture Follow-Up Campaign
Round 5. 0/3 phases complete.
Impact area: `Physics/PersistentContactSolver.cpp`,
`Physics/Diagnostics/SkullScope.cpp`, `Physics/SleepIslandSystem.cpp`,
`Physics/PhysicsDiagnosticsSink.cpp`
Owner: physics
Priority: Medium — no behavior risk and a small diff, but it is the single most
direct piece of evidence that a past decomposition moved code without moving
design, and it is invisible to every existing checker.

## Problem And Evidence (measured 2026-07-26)

Four physics translation units open a function by binding its parameters to
member-prefixed locals, so a body lifted out of a god class needed no internal
edits:

**`Physics/PersistentContactSolver.cpp:124-134`** — inside
`PhysicsContactSolverStage::Solve`, eleven aliases:

```cpp
auto& m_candidatePairs         = candidatePairs;
auto& m_sleepState             = sleepState;
auto& m_sleepSupportEdges      = sleepSupportEdges;
auto& m_physicsDebugContacts   = stepDiagnostics.MutableDebugContacts();
auto& m_terrainContactManifolds= terrainContactManifolds;
auto& m_terrainRestApplied     = terrainRestApplied;
auto& m_sleepSupportedThisFrame= sleepSupportedThisFrame;
auto  m_bodyRecords            = bodyStore.MutableRecords();
auto  m_hotFields              = bodyStore.MutableHotFields();
const auto m_colliderRecords   = colliderStore.Records();
```

**`Physics/Diagnostics/SkullScope.cpp:168-183`** — sixteen aliases, each binding
one `physicsDiagnostics.<field>` to `m_<field>`.

**`Physics/SleepIslandSystem.cpp:52-54`** — three aliases binding
`context.<field>` to `m_<field>`.

**`Physics/PhysicsDiagnosticsSink.cpp:207-209`** — three aliases binding
`diagnosticsView.<field>` to `m_<field>`.

Why this matters beyond style:

- The `m_` prefix is the repository's member convention. A reader of
  `PersistentContactSolver.cpp:400` sees `m_candidatePairs` and reasonably
  concludes it is state owned by the enclosing type. It is a borrowed span whose
  lifetime ends when `Solve` returns. That is a correctness trap in a file that
  also does bounded-scratch and worker-partitioned work.
- The `AGENTS.md` God-Object Closure Rule bans "nominal owner types that merely
  relay business operations while authority remains in `Run`" and forwarding
  facades. This is the same failure expressed in local variables, where no
  mechanical check can see it. `PhysicsWorld` stage-owner decomposition and
  `concrete-parameter-bag-elimination` PB5/PB6 both passed over these files —
  PB5's row 13 deleted `PersistentContactSolverContext` and its census note even
  records that the consumer "immediately aliases nearly every field"
  (`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md:43`) —
  and the aliases survived the bag's deletion.
- `SkullScope.cpp` and `PhysicsDiagnosticsSink.cpp` alias fields out of
  `PhysicsDiagnosticsView` (`Physics/PhysicsWorld.h:263`), a 17-member view of
  const references. Renaming the locals will expose whether those functions
  actually need all seventeen.

## Goal

Every local in these four functions is named for what it is in its current
scope: a borrowed parameter, a span with a call-scoped lifetime, or a derived
value. No local claims membership it does not have.

## Non-Goals

- **No behavior change of any kind.** This is a rename. The physics regression
  CSV must be byte-exact and the change must be provable as such.
- No signature changes, no parameter reduction, no owner moves. If the rename
  exposes that a function takes more than it needs, that is a *finding recorded
  for a follow-up decision*, not work done here — narrowing a physics hot-path
  signature is `concrete-parameter-bag-elimination`-class work with its own gates.
- No new type to hold the renamed locals. Introducing a struct to carry them
  would recreate the bag PB5 just deleted.
- No changes to `PhysicsDiagnosticsView` itself.
- The mechanical checker that prevents recurrence belongs to
  `governance-shape-to-judgment-conversion` G2/G3
  (`tools/inventory_extraction_scars.py`), not to this plan. This plan supplies
  the positive fixtures.

## Phases

- [ ] **ES0 — Rename the aliases and record what the rename exposes.**
  In all four files, either delete the alias and use the parameter name directly,
  or give the local a scope-honest name (for example `bodyRecords`,
  `hotFields`, `colliderRecords`, `candidatePairs`). Prefer deletion — an alias
  that only renames a parameter should not exist. For each function, record in the
  plan whether it reads every parameter it receives, and list any it does not; that
  list is the follow-up finding, explicitly not fixed here. Where a borrowed span's
  lifetime is call-scoped, add the `Lifetime:` comment the comment style guide
  requires — these are exactly the non-obvious ownership facts the guide exists
  for. Acceptance: no local under `SkullbonezSource/` matches the `m_` member
  convention; `PhysicsFixedList`/span lifetimes are documented at the borrow site;
  the physics regression CSV is byte-exact against the committed baseline.

- [ ] **ES1 — Supply the checker fixtures.**
  Provide the planted positive and negative fixtures that
  `governance-shape-to-judgment-conversion` G2's
  `tools/inventory_extraction_scars.py` self-test consumes: a member-prefixed
  local that must be reported, a genuine member access that must not be, and a
  local that aliases a parameter without transforming it. Acceptance: the G2
  self-test fails when any fixture guard is removed and passes at final source;
  the repository scan reports zero unruled rows.

- [ ] **ES2 — Reconcile, review, and hand off.**
  Complete the comment audit for the four touched files. Obtain one independent
  review asking: is every remaining local scope-honest, did any alias survive
  under a different spelling, and is the byte-exactness proof real rather than
  asserted. Acceptance: review clear; `validate_physics.bat` passes with the
  44,401-row CSV byte-exact from the final Debug binary;
  `validate_physics_deep.bat` passes because `SkullScope.cpp` changed;
  `validate_perf.bat` shows no hot-path regression.

## Dependencies And Decisions

- ES1 depends on `governance-shape-to-judgment-conversion` G2 existing. If this
  plan runs first, ES1 stays unchecked with the reason recorded and closes when
  G2 lands — do not silently skip it.
- Sequence this plan before or alongside `scene-sized-store-capacity` SC4/SC5,
  which touch the same solver and sleep files. Doing the rename first keeps the
  capacity plan's byte-exactness proof readable.
- No open owner decisions.

## Acceptance

- Zero member-prefixed locals under `SkullbonezSource/`.
- Borrowed-span lifetimes documented at each borrow site.
- Physics output byte-exact; no signature or type changes.
- Checker fixtures in place so the pattern cannot return silently.

## Validation

- `tools\validate_physics.bat` — `PersistentContactSolver` and
  `SleepIslandSystem` changed; byte-exact CSV diff required.
- `tools\validate_physics_deep.bat` — `Physics/Diagnostics/SkullScope.cpp`
  changed, which feeds the SkullScope query baselines.
- `tools\validate_perf.bat` — the touched functions are fixed-tick hot paths.
- `tools\validate_fast.bat` — required once ES1's tool fixtures land.
