# Extraction Scar Remediation

Date: 2026-07-26
Status: NOT STARTED — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 7 of the Architecture Follow-Up Campaign
Round 5. 0/3 phases complete.
Impact area: 12 files across `Physics/`, `Runtime/App/`, `Runtime/Capture/`,
`Runtime/Scene/`, `Runtime/Input/`, `Runtime/Render/`,
`Runtime/Prediction/`, `Maths/`, and `Core/`
Owner: physics + runtime
Priority: Medium — no behavior risk and a small diff, but it is the single most
direct piece of evidence that a past decomposition moved code without moving
design, and it is invisible to every existing checker.

## Problem And Evidence (measured 2026-07-26, corrected by tooling)

**Scope correction.** The originating architecture review found this pattern by
hand in four physics files and estimated 33 aliases. The
`governance-shape-to-judgment-conversion` G2 inventory
(`tools/inventory_extraction_scars.py`) measured the real figure at
**89 findings across 12 files** — 86 member-prefixed locals and 3 pure parameter
aliases. Run it for the current list; the table below is the 2026-07-26 seed:

| File | Findings |
|---|---:|
| `Runtime/App/InputFrameExecution.cpp` | 25 |
| `Physics/Diagnostics/SkullScope.cpp` | 16 |
| `Runtime/Capture/RuntimeStressController.cpp` | 14 |
| `Physics/PersistentContactSolver.cpp` | 11 |
| `Runtime/App/InputRouter.Interactions.cpp` | 6 |
| `Runtime/Scene/SceneController.Load.cpp` | 3 |
| `Physics/SleepIslandSystem.cpp` | 3 |
| `Physics/PhysicsDiagnosticsSink.cpp` | 3 |
| `Maths/GeometricMath.cpp` | 2 |
| `Runtime/App/Window.cpp`, `Runtime/Input/Input.cpp` | 1 each (`m_cWindow`) |
| `Runtime/Scene/SceneRequestExecution.cpp` | 1 |
| `Core/WorkerPool.h`, `Runtime/Render/RuntimeRenderPasses.cpp`, `Runtime/Prediction/ReplayPredictionArchive.cpp` | 1 each (alias) |

**The two largest sites tie this plan to the frame-view finding.**
`InputFrameExecution.cpp` (25) and `RuntimeStressController.cpp` (14) are exactly
the two external consumers named by `Runtime/RuntimeFrameViews.h:30-31`. Both
destructure the four frame views straight back into `m_`-named locals
(`m_UI`, `m_applicationExit`, `m_assets`, `m_camera`, `m_config`,
`m_sceneController`, `m_renderer`, `m_renderBackendView`, `m_workerPool`, …) —
mechanical evidence that the views exist to feed pre-extraction member names, not
to express a capability boundary. `runtime-frame-view-retirement` FV0 must read
this list.

The originally reported four physics units open a function by binding its
parameters to member-prefixed locals, so a body lifted out of a god class needed
no internal edits:

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
- The three pure aliases are not equivalent. `Core/WorkerPool.h:228`
  (`IndexFunctionT& indexFn = fn;`) binds a forwarding reference to an lvalue
  reference so the chunk lambda can capture it — a language requirement, already
  ruled `retain` in `tools/aggregate_ownership_rulings.json`. The other two
  (`Runtime/Render/RuntimeRenderPasses.cpp:273`,
  `Runtime/Prediction/ReplayPredictionArchive.cpp:573`) are shorthand and are
  ruled `repair`.

## Goal

Every local across the twelve files is named for what it is in its current
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
- No rebuilding of the checker or its fixtures.
  `governance-shape-to-judgment-conversion` G2/G3 already landed
  `tools/inventory_extraction_scars.py`, its ten fixtures, and the
  `validate_fast` step 4/8 wiring. This plan consumes that tool and retires its
  `repair` rows; it does not re-author it.
- No widening of a ruling to make a finding disappear. A row leaves the file
  because its code was fixed, or because an owner recorded a `retain` reason that
  names a real requirement. Editing a verdict to silence the gate is a closure
  failure.
- No frame-view or capability-slice restructuring, even though the two largest
  sites are the frame-view consumers. Renaming a local there does not close
  `runtime-frame-view-retirement`, and that plan owns the boundary question.

## Phases

- [ ] **ES0 — Rename the aliases and record what the rename exposes.**
  In all twelve files, either delete the alias and use the parameter name directly,
  or give the local a scope-honest name (for example `bodyRecords`,
  `hotFields`, `colliderRecords`, `candidatePairs`). Prefer deletion — an alias
  that only renames a parameter should not exist. For each function, record in the
  plan whether it reads every parameter it receives, and list any it does not; that
  list is the follow-up finding, explicitly not fixed here. Where a borrowed span's
  lifetime is call-scoped, add the `Lifetime:` comment the comment style guide
  requires — these are exactly the non-obvious ownership facts the guide exists
  for. Acceptance: `python tools\inventory_extraction_scars.py --repo .` reports
  zero `repair`-verdict rows remaining, with the one `retain` row
  (`Core/WorkerPool.h:indexFn`) intact and its reason unchanged; span lifetimes are
  documented at each borrow site; the physics regression CSV is byte-exact against
  the committed baseline. Because the corrected scope spans Runtime as well as
  Physics, the gate set is cumulative — see Validation.

- [ ] **ES1 — Retire the ruling rows and extend the fixtures if ES0 found a new
  shape.**
  `governance-shape-to-judgment-conversion` G2 already landed
  `tools/inventory_extraction_scars.py` with ten planted fixtures — two positive
  (member-prefixed local, pure reference alias) and eight negative (real class
  member, member read via `return`, member write from a parameter, loop
  comparison, assignment from a constant, transformed local, comment/literal,
  alias of a non-parameter, mutated value copy) — and proved guard load-bearing
  by disabling the reference-only guard and observing the self-test fail. Do not
  re-create that work.
  What remains: delete each `repair` row from
  `tools/aggregate_ownership_rulings.json` as ES0 fixes its code, so the file
  ends holding only the one `retain` row. If ES0 encounters a scar shape the
  scanner does not report — a spelling that evades the declaration matcher, or a
  legitimate alias class the reference-only rule misses — add a fixture for it in
  this phase rather than widening a ruling. Acceptance: the ruling file's
  `extraction_scars` list contains exactly the `retain` rows, with reasons
  unchanged; `python tools\inventory_extraction_scars.py --self-test` passes and
  still fails when any fixture guard is removed; the repository scan reports zero
  findings other than ruled `retain` rows.

- [ ] **ES2 — Reconcile, review, and hand off.**
  Complete the comment audit for the four touched files. Obtain one independent
  review asking: is every remaining local scope-honest, did any alias survive
  under a different spelling, and is the byte-exactness proof real rather than
  asserted. Acceptance: review clear; `validate_physics.bat` passes with the
  44,401-row CSV byte-exact from the final Debug binary;
  `validate_physics_deep.bat` passes because `SkullScope.cpp` changed;
  `validate_perf.bat` shows no hot-path regression.

## Dependencies And Decisions

- ES1's tooling dependency is already satisfied: `governance-shape-to-judgment-conversion`
  G2 landed `tools/inventory_extraction_scars.py` with self-test fixtures and the
  seeded ruling file, and G3 wired both into `validate_fast`. ES1 is therefore
  reduced to removing each `repair` row from the ruling file as its code is fixed,
  and confirming the planted fixtures still fail when their guards are removed.
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

- `tools\validate_full.bat` — required, because the corrected scope includes
  `Runtime/App/*`, `Runtime/Capture/*`, and `Runtime/Scene/*`.
- `tools\validate_physics.bat` — `PersistentContactSolver` and
  `SleepIslandSystem` changed; byte-exact CSV diff required.
- `tools\validate_physics_deep.bat` — `Physics/Diagnostics/SkullScope.cpp`
  changed, which feeds the SkullScope query baselines.
- `tools\validate_perf.bat` — the touched functions are fixed-tick hot paths.
- `tools\validate_fast.bat` — required once ES1's tool fixtures land.
