# Selectable Solvers 1: Skullbonez Foundation

Date: 2026-09-17
Status: WNF - owner-requested plan only; 0/5 phases complete.
Owner: Physics solver and world settings
Impact area: Physics ownership, settings, state lifecycle, tests
Commit name: `SOLVER_FOUNDATION`
Estimate: Re-estimate at activation for the nine-solver scope; the former
two-algorithm estimate is withdrawn.

## Owner Direction And Sequence

Owner scope revision, 2026-09-17: the existing custom PGS-based implementation
is the **Skullbonez** solver. Preserve it as its own algorithm and add all eight
Solver2D variants as new implementations. The finished product has nine choices;
the new PGS choice must never be an alias for Skullbonez.

1. [Skullbonez foundation](selectable-solvers-01-foundation.md).
2. [Eight solver implementations](selectable-solvers-02-tgs.md).
3. [Product integration and acceptance](selectable-solvers-03-integration.md).

All three stay parked in WNF until explicitly restored by the owner. Listing
these plans in MASTER-PLAN's parked inventory does not activate them or change
active phase totals. Drafting authorizes no implementation, PR or merge.
On activation, follow `Agentic/Skills/orchestrator/SKILL.md` and recheck current
repository instructions. Documentation-only drafting needs no validation.

## Goal

Give each Physics world an explicit solver selection and isolate the current
Skullbonez implementation without changing its numerical behavior. The eventual
selection supports independently configured live, prediction and Solver Lab
worlds. Each new algorithm needs a concrete Physics implementation, settings
and tests, using the same collision and product infrastructure where valid.

This stage implements selection and preserves Skullbonez; stage 2 implements
the eight additions. Do not tune stacking, change the default solver or add a
plugin/DLL loading system during this extraction.

## Required Solver Identities

These durable keys and display names distinguish algorithms throughout settings,
diagnostics, prediction, replay and Solver Lab. Reserve keys without claiming
unimplemented choices are supported. Do not serialize enum ordinal positions.

| Durable key | Display name | Ownership in this campaign |
|---|---|---|
| `skullbonez` | Skullbonez | Existing custom solver; preserved default |
| `pgs` | PGS | New implementation |
| `pgs_ngs` | PGS NGS | New implementation |
| `pgs_ngs_block` | PGS NGS Block | New implementation |
| `pgs_soft` | PGS Soft | New implementation |
| `tgs_sticky` | TGS Sticky | New implementation |
| `tgs_soft` | TGS Soft | New implementation; help may also say Soft Step |
| `tgs_ngs` | TGS NGS | New implementation |
| `xpbd` | XPBD | New rigid-body implementation |

This table is authoritative for all three stages. A generic `tgs` identity is
ambiguous and is not a supported durable key. Shared mathematical operations
are allowed, but each new solver owns its schedule, state and tested numerical
contract. A renamed Skullbonez path or several labels for the same backend do
not satisfy the scope.

## Evidence And Starting Points

Read-only inspection on 2026-09-17, HEAD `e59e397f8`:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp` implements
  `ConstraintSolveTransaction::SolveRowsIterations` as bounded PGS impulses.
  PGS describes its mathematical family; its product identity is Skullbonez.
- `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` owns explicit
  preparation, solving, writeback, release and cache transaction phases.
- `SkullbonezSource/Physics/PhysicsRuntimeSettings.h` contains
  `ContactSolverSettings`; the inspected settings have no algorithm selector.
- `SkullbonezSource/Physics/PhysicsWorld.cpp` sequences solving after motion,
  narrowphase and terrain, including per-body remaining CCD time.
- `SkullbonezSource/Physics/PhysicsSolverSnapshot.h` is an existing state
  boundary whose complete consumers must be mapped before representation changes.

Hull work still awaits aggregate acceptance according to SessionState. Reconcile
accepted source at activation; do not combine extraction with hull-response tuning.

## Design Decisions

- Physics owns algorithm identity, normalized settings and implementations.
  Dispatch at the appropriate world/solve boundary using concrete composition;
  do not introduce virtual dispatch or callbacks inside constraint row loops.
- Keep one owner for body state, collision geometry, island connectivity,
  sleeping and publication. Share preparation only where semantics agree.
  Leave the new temporal and position-based solvers control of their own schedules.
- Skullbonez stays the default. Missing legacy configuration means Skullbonez; explicitly
  unknown or unavailable algorithms fail clearly. Never implement a nominal
  new choice by silently executing Skullbonez or another implemented solver.
  If activation discovers an older explicit `pgs` spelling for the custom
  implementation, migrate it by artifact version and provenance to `skullbonez`;
  do not reinterpret historical custom-solver state as the new PGS algorithm.
- Preserve Skullbonez row/arithmetic order, warm starts, friction, restitution, joint
  interleaving, early exit, position correction and cache publication.
- Selection belongs to each world, not a mutable process-global switch.
  Algorithm and timestep-affecting changes require cold reset/recreation;
  stage 3 exposes an explicit apply-and-restart action.
- Reserve scratch during setup. Prefer bounded stage-owned parallel stores;
  any proposed hot body field needs its consumer and the reason a parallel
  store is insufficient recorded before implementation.
- Admit implemented algorithms only. Avoid fake backends, speculative plugin
  registries, unused forwarding interfaces and placeholder UI choices.
- Settings and cache state are algorithm-specific values. Normalization rejects
  incompatible combinations; an impulse cache, sticky friction anchor or XPBD
  multiplier cannot be reinterpreted as another solver's state. Define separate
  support, settings-version and retained-state-version checks.
- No dependency exceptions are proposed. Physics retains its downward-only
  dependency direction; no Runtime, Replay, Prediction or Planning include.

## Phases And Acceptance

- [ ] **SF0 - Capture the contract.** Map callers, configuration projection,
  clone/snapshot state, transaction phases, joint coupling, CCD remaining time
  and fixed-contact release. Capture current Skullbonez outputs and baseline hashes
  under `TestOutput/solver-selection/foundation/`; record scenes, ticks, settings,
  workers, executable identity and toolchain before changing implementation.
- [ ] **SF1 - Introduce world selection.** Define stable identity and explicit
  support checks. Normalize once and retain defaults. Prove missing/invalid
  values, all nine distinct keys, independent worlds and rejection of mid-step changes. Do not expose
  an unavailable algorithm as a supported choice.
- [ ] **SF2 - Isolate Skullbonez execution.** Separate algorithm execution from shared
  world lifecycle without changing order or arithmetic. Enforce sequencing in
  the existing transaction or a coherent replacement. Preserve terrain, joints,
  wake/release and cache semantics; delete superseded duplicate execution paths.
- [ ] **SF3 - Prove lifecycle compatibility.** Cover clear, reserve, reset,
  clone, capture/restore and invalid selections. Inventory settings and retained
  caches affecting continuation. Preserve formats where possible; document any
  required version transition for stage 3. New state cannot be omitted from a
  supported snapshot path.
- [ ] **SF4 - Terminal review and closure.** Complete focused/mapped validation,
  independent ownership/rubber-duck review and final fixes, then the full plan
  closure gate. Prove Skullbonez parity and no hot allocation; record commands, exit
  codes and artifacts, and reconcile downstream dependencies.

A phase is checked only with its required behavior and evidence. SF0-SF3 do not
constitute completion without SF4.

## Validation And Behavioral Proof

Iteration uses affected builds and focused tests in
`SkullbonezTests/TestPersistentContactSolver.cpp`,
`SkullbonezTests/TestDeterminism.cpp` and the existing point-joint tests. Add
meaningful dispatch/lifecycle tests under their subsystem owner. Keep heavy
checks and independent review in SF4, except required earlier push-boundary gates.

Terminal commands from the repository root:

```bat
tools\validate_fast.bat
tools\validate_dependency_graph.bat
tools\validate_physics.bat
tools\validate_physics_deep.bat
tools\validate_perf.bat
tools\validate_full.bat --plan-completion
```

Apply additional file-to-gate mappings for replay, serialization or project
changes. Reuse gates already covered on the same final source by the terminal
umbrella; do not duplicate them. Leave local Profile built and verify an
unchanged Profile build performs no compilation/linking.

Require unchanged existing Skullbonez baselines and same-envelope determinism across
supported worker configurations. No golden refresh may conceal extraction drift.
For native evidence follow `Agentic/Skills/skarness/SKILL.md`, bind assertions to
identity and output, preserve event streams and stop owned sessions orderly.

## Completion And Handoff

Supply stage 2 with the concrete dispatch boundary, settings/state inventory,
memory cost, Skullbonez parity evidence and outstanding decisions. Use required commit
sections for ownership and validation evidence. No broad context or forwarding
layer may remain merely to claim extraction.

Reconcile MASTER-PLAN and SessionState through activation/closure workflow.
Before deleting this completed plan, replace downstream links with its closure
commit and current implementation references. Parked successors remain inactive.
