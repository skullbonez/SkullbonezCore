# Runtime App Feature Ejection

Date: 2026-08-24
Status: WNF — owner-parked 2026-08-24; restore to `TODO/` only by explicit
owner decision. 0/7 phases complete.
Impact area: `Runtime/App` composition root, `Runtime/Automation`,
`Runtime/Prediction`, `Runtime/Planning`, `Runtime/Replay`, dependency graph,
project ownership, allocation allowlist, tests.
Owner: Runtime composition owner
Priority: Parked follow-up
Commit name: `APP_FEATURE_EJECTION`

Source investigation: Runtime/App god-package review, 2026-08-24 (function
complexity + wide-signature inventories, per-package line census, App file
classification, learning-header ownership audit).

## Owner Direction

This is the authority-mass follow-on that Runtime Boundary Separation
deliberately left out of scope. RBS closed App's *direction* — the Runtime
package graph is acyclic with App on top, there are no reverse App edges, and
no capability slice regains the whole surface. RBS's Non-Goals explicitly
declined to reduce App's line count or relocate feature implementation
(`Do not pursue LOC reduction`, `Do not measure App closure by how few source
files remain`). This plan owns the part RBS deferred: moving feature *ownership*
out of the composition root.

It is not active MASTER-PLAN work and grants no production-edit, dependency-rule,
allocation-allowlist, or baseline-refresh authority while it remains under
`WNF/`. It does not add itself to `MASTER-PLAN.md`; only the owner activates it.

## Problem And Evidence

RBS made App a clean composition *root*, but the layer underneath is now the
defect: App is a god-*package*. It passes every current gate while physically
owning two large feature subsystems that belong to lower Runtime owners.

Measured on 2026-08-24 at `c4b3f1f06`:

- `Runtime/App` is **38,374 lines across 48 files — 2.3× the next-largest
  package** (Replay 16,873). A composition root should be among the smallest
  packages, not the largest by more than double.
- Two independent governance inventories put App first by a wide margin:
  function-complexity **11 of 40** triggered functions, wide-signature **6 of
  13**. No other package is close.
- **~59% of App is feature implementation, not composition:**

  | Band | ~Lines | Honest owner |
  |---|---|---|
  | Composition core (`Run`, `RunFrame`, `Init`, `Startup*`, input-frame sequencing, `Operator*` composition, `SceneLoad`) | ~13.0k | `Runtime/App` ✅ |
  | Interaction-automation driver (`InteractionAutomationApplication.cpp` = 6,010 alone, + report application) | ~8.2k | `Runtime/Automation` (today only 2,769) |
  | Replay/Prediction presentation, scrubber, validation, cause-tree (`Replay*`, `ReplayPredictionDrawing`, `ReplayAuthoringCauseTree`, …) | ~14.5k | `Runtime/Planning` / new product package |
  | Other headers / small glue | ~2.7k | mixed |

- The single largest source file in App, `InteractionAutomationApplication.cpp`
  (6,010 lines), documents a full subsystem — its own glossary (world-click,
  director-shot, prediction-target, forecast-command, automation-report) and
  seven invariants. That is a feature owner, not composition glue.
- `ReplayAuthoringCauseTree.cpp`'s own `Purpose:` line reads **"Implements
  Prediction-owned cause-row composition and focus activation."** Logic the file
  itself labels as owned by another layer is physically resident in the
  composition root. This is a concrete authority inversion, not merely mass.

### Why every current gate misses it

This is a structural blind spot, not an oversight:

- **Include-direction gate cannot fire.** App is the DAG sink (Runtime rank 20);
  feature code placed in App is *permitted* to reach every owner below it. App
  is the one location where no include rule resists cross-cutting logic.
- **Per-function complexity gate passes.** Each large App function carries an
  individual `retain-owner` ruling ("one cohesive phase"). The gate is
  per-function and cannot see a *package* accreting three unrelated subsystems.
- **God-Object Closure Rule is aimed at the `Run` type** — `Run::` methods,
  capability slices, forwarding wrappers. This logic is *free functions*
  (`BuildReplayCauseTreeRows`, `WriteInteractionTraceTurn`,
  `UpdateReplayPredictionDrawList`) in App `.cpp` files, so it does not register
  as `Run` surface.

There is no package-level ownership sensor, so App can silently re-accrete
feature logic indefinitely while staying green.

## Goal

Reduce `Runtime/App` to composition and top-level frame sequencing by moving
feature ownership to honest lower owners, matching RBS's own target diagram
(`App -> Replay/Prediction/Planning/Automation`, never the reverse). Success is
measured by **relocated authority and corrected ownership**, not by a line or
file count. Every move must be a genuine ownership transfer — not a forwarding
header, alias, callback pack, service bag, or once-called helper that moves code
without moving design.

## Non-Goals

- Do not LOC-golf. A file stays in App when its responsibility is genuinely
  composition, sequencing, platform message pumping, or typed cross-owner
  result application, regardless of its length.
- Do not move genuinely App-owned composition into a lower package to shrink a
  number.
- Do not fake a move with a `Run::` forwarder, forwarding header, compatibility
  alias, `*Bridge`/`*Adapter`/`*Sink`, callback pack, service/context bag, or a
  helper called once immediately by the original body (Extraction Scar Rule).
- Do not move Runtime feature vocabulary or state into Core, Rendering, Physics,
  or Maths to route around a package boundary.
- Do not place operator-facing recorded/predicted-data features in
  `Runtime/Replay` or `Runtime/Prediction` (Replay-Family Placement Rule); those
  are recorded-infrastructure and future-simulation-production owners, not homes
  for cause-tree, scrubber-tool, or panel product logic.
- Do not add a new per-line JSON rulings registry for the AFE6 guardrail; keep
  it coarse and line-number-free, consistent with the parked
  `governance-simplification-and-scar-removal.md` direction.
- Do not refresh any physics, replay, visual, causal, or SkullScope golden to
  make a move pass. Moves are behavior-preserving; a golden difference is a bug
  in the move.
- Do not weaken the registered replay/prediction allocation exceptions; moving a
  file updates its allocation-allowlist `path` and keeps its owner/phase/cap.

## Move Contract

Every ejection is a behavior-preserving relocation proven acyclic before it
lands:

- The moved code keeps byte-exact behavior. The mapped gate for the touched area
  runs after the move: automation report equality for the driver; the replay
  visual-fidelity gate for replay/prediction presentation; `validate_fast` for
  dependency direction, project ownership, and the inventories.
- The move introduces no upward or cyclic Runtime edge. If a candidate file
  depends on a higher-rank owner (e.g. the automation driver's UI surface-
  selection, or a `Render`/`UI` reach), that dependency stays in App as a typed
  command the lower owner emits and App applies; only the owner-side logic moves
  down.
- Ownership comments are corrected in the same commit: a moved file's
  `File:`/`Purpose:`/owner claims must name the post-move owner and path. The
  self-labeled "Prediction-owned" cause-tree text is reconciled to its true
  owner as part of the move that resolves it.
- Dependency rule data (`tools/dependency_graph_rules.json`), the generated
  AGENTS.md proof block, project ownership rows, and the allocation allowlist are
  updated in the same change as the file move, never after.

A move that cannot be made acyclic without a bag, forwarder, or upward edge is
recorded as blocked with its exact reach set, not forced.

## Phases

- [ ] **AFE0 — Baseline evidence and per-file reach-set proof.** Re-measure App
  mass, the complexity/wide-signature concentration, and classify all 48 App
  files as composition-core vs feature-implementation. For every feature file,
  compute its exact owner reach set and the target package it would move to, and
  whether that move introduces an upward or cyclic Runtime edge. Emit the
  concrete move manifest (`file -> target package`, plus blocked-with-reason
  rows) and lock the behavior-preservation controls. No source moves in this
  phase.
- [ ] **AFE1 — Ratify destination topology and resolve Replay-family placement.**
  Decide, with owner ruling, where the replay/prediction presentation and
  cause-tree land: `Runtime/Planning`, or a new explicitly-named product package
  above Planning (the Replay-Family Placement Rule forbids Replay/Prediction as
  their home). Split genuine future-simulation *production* (which may stay in
  `Runtime/Prediction`) from operator-facing *presentation* (which may not).
  Decide the automation driver's destination (`Runtime/Automation`, or an
  `Automation` application sub-owner) and confirm surface selection reduces to a
  typed command applied by App. Output is the ratified target topology and the
  rank/edge deltas it requires.
- [ ] **AFE2 — Eject the interaction-automation driver.** Move
  `InteractionAutomationApplication.cpp` and `InteractionAutomationReportApplication.cpp`
  to the ratified Automation owner, leaving only typed command and surface-
  selection composition in App. Update dependency rules, project ownership, and
  allocation-allowlist paths. Prove acyclic direction and byte-exact automation
  reports/traces against the recorded manifests.
- [ ] **AFE3 — Eject replay/prediction presentation and the cause-tree.** Move
  `ReplayPredictionDrawing.*`, `ReplayCauseFocusSubmission.cpp`,
  `ReplayAuthoringCauseTree.cpp`, `ReplayPredictionPresentation.cpp`, and the
  retained-geometry headers to the AFE1 owner. Correct the "Prediction-owned"
  and any other stale ownership headers to the true post-move owner. Run the
  replay visual-fidelity gate plus the dependency and project-ownership gates.
- [ ] **AFE4 — Eject replay runtime orchestration, scrubber, and validation, or
  record why they stay.** Assess `ReplayRuntime.*`, `ReplayScrubberTools.cpp`,
  and `ReplayValidation*.cpp` against the same owner test. Move what is feature
  orchestration into its honest owner; for any file that is genuinely App-level
  composition of several owners, record the concrete cohesion reason it remains
  in App rather than moving it to hit a number.
- [ ] **AFE5 — Reduce App to composition and prove the target shape.** Confirm
  App contains only composition, top-level frame sequencing, platform message
  pumping, and typed cross-owner application. Obtain the mandatory independent
  God-Object ownership review; a finding of concrete unrelated responsibility,
  state, or dependency authority still resident in App reopens the relevant
  phase and blocks closure. Re-run `validate_fast` and every touched mapped gate.
- [ ] **AFE6 — Install a lean package-ownership guardrail.** Add a single coarse,
  line-number-free check that flags a composition-root package re-accreting a
  feature subsystem (for example, an App source file that declares its own
  `Glossary:`/`Invariants:` feature contract beyond composition). It must not be
  a new per-line rulings registry. Reconcile explicitly with
  `governance-simplification-and-scar-removal.md`: if `DE_BUREAUCRATIZE` is
  activated first, fold this guardrail into its lean-guardrail model instead of
  adding tooling that plan would then delete.

## Relationship To Sibling Plans

- **Runtime Boundary Separation** is the completed predecessor recorded in
  `Agentic/Plans/MASTER-PLAN.md` and Git history. This plan starts where RBS's
  Non-Goals stop and must not reopen RBS's direction closure; it consumes a
  clean acyclic Runtime graph as its precondition and keeps it clean.
- **Replay-Family Placement Rule (AGENTS.md)** governs AFE1/AFE3 destinations.
- **Governance Simplification And LLM Scar Removal
  (`WNF/governance-simplification-and-scar-removal.md`)** constrains AFE6. The
  two plans must not fight: AFE6 adds at most one coarse guardrail and defers to
  `DE_BUREAUCRATIZE` on tooling philosophy if that plan lands first.

## Verification And Acceptance Criteria

- [ ] Runtime package graph remains acyclic with App at the top; strict runtime
  graph check stays empty (`--check-runtime-graph`).
- [ ] Each landed move is a genuine ownership transfer: no new forwarding header,
  alias, `Run::` forwarder, bag, callback pack, or once-called extraction helper
  introduced to accomplish it.
- [ ] No operator-facing recorded/predicted feature resides in `Runtime/Replay`
  or `Runtime/Prediction` after AFE3/AFE4.
- [ ] Every moved file's ownership comments name its post-move owner and path;
  the "Prediction-owned" cause-tree claim is reconciled.
- [ ] Automation reports/traces are byte-exact against the recorded manifests;
  the replay visual-fidelity gate passes; no golden refreshed to pass a move.
- [ ] Independent God-Object review finds no unrelated feature responsibility,
  state, or dependency authority still owned by `Runtime/App`.
- [ ] `validate_fast`, the dependency/project gates, and the touched mapped gates
  pass; allocation allowlist paths track the moved files with unchanged
  owner/phase/cap.

## Reactivation Condition

Move this file from `WNF/` to `TODO/` only when the owner explicitly activates
App feature ejection in `MASTER-PLAN.md`. At reactivation, re-run AFE0's
measurements against the then-current tree before any source moves; App mass and
the feature bands will have shifted, and the move manifest must be rebuilt from
current evidence rather than the 2026-08-24 snapshot.
