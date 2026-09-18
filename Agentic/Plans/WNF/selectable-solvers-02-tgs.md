# Selectable Solvers 2: Eight Solver Implementations

Date: 2026-09-17
Status: WNF - owner-requested plan only; 0/14 phases complete.
Owner: Physics constraint algorithms and temporal integration
Impact area: contacts, joints, integration, CCD, sleeping, retained state, tests
Commit name: `SOLVER_ALGORITHMS`
Estimate: Re-estimate after SA0 maps all eight implementations and their shared
work. The earlier 1-2-week estimate covered one TGS solver and does not apply.

## Owner Direction And Dependencies

Implement eight new algorithms alongside the preserved custom **Skullbonez**
solver: PGS, PGS NGS, PGS NGS Block, PGS Soft, TGS Sticky, TGS Soft, TGS NGS
and XPBD. All eight are required; none is a deferred third-solver option.
The [stage 1 identity table](selectable-solvers-01-foundation.md) owns their
durable keys. Skullbonez remains the default and is not the new PGS entry.

Use the accepted [foundation](selectable-solvers-01-foundation.md), then hand
all nine choices to [product integration](selectable-solvers-03-integration.md).
The historical `02-tgs` filename is retained so existing links keep resolving;
its scope is now all eight additions, not just TGS.

This plan remains parked in WNF. Editing it does not activate implementation,
baseline changes, a PR or a merge. On explicit restoration to TODO, follow
`Agentic/Skills/orchestrator/SKILL.md` and recheck current repository rules.
Documentation-only drafting requires no runtime validation.

Reconcile current hull acceptance and the related parked
[contact-stack experiments](contact-stack-stability-techniques.md). Reuse their
fixtures and observations, but do not activate that plan or count one experiment
as acceptance of a named solver. This plan owns the eight production algorithms;
the older plan retains its broader adaptive-work and split-repair experiments.

## Goal And Dated Evidence

Deliver eight distinct 3D rigid-body algorithms with measured quality/cost,
bounded work, repeatable results and complete Physics lifecycle support.
Preserve the outer fixed-step duration and Skullbonez numerical behavior.
Different algorithms may produce different trajectories. No algorithm is required
to outperform every other algorithm, but each must meet its declared correctness
and supported-feature contract.

Inspection on 2026-09-17 at `e59e397f8` found the existing custom PGS sweep in
`SkullbonezSource/Physics/PersistentContactSolver.cpp`, joint preparation in
`SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`, and remaining CCD
time sequencing in `SkullbonezSource/Physics/PhysicsWorld.cpp`.
`SkullbonezSource/Physics/PersistentContactSolver.h` retains local hull anchors;
this does not prove primitive/terrain rows already support temporal updates.
Recheck accepted source when this plan is activated.

Algorithm names follow [Erin Catto's Solver2D overview](https://box2d.org/posts/2024/02/solver2d/).
The [reference repository](https://github.com/erincatto/solver2d) is a 2D
experimental implementation, not a ready-made Core backend. At SA0 pin the exact
upstream revision and relevant source/paper references for every algorithm.
Record any copied/adapted source and retain its applicable license notices.
Reference defects must be documented and tested, not copied to manufacture parity.

## Required Algorithm Contracts

The following concise descriptions identify the eight reference variants.

| Solver | Defining behavior |
|---|---|
| PGS | Sequential impulses, warm starting and Baumgarte stabilization. |
| PGS NGS | PGS velocity solving followed by nonlinear position correction instead of Baumgarte velocity bias. |
| PGS NGS Block | PGS NGS with coupled contact-point normal solving. |
| PGS Soft | Soft velocity constraints and relaxation. |
| TGS Sticky | Temporal substeps, Baumgarte stabilization, relaxation and persistent friction anchors; no inter-tick impulse warm start. |
| TGS Soft | Temporal substeps, soft constraints, impulse warm starting and relaxation. |
| TGS NGS | Temporal substeps with nonlinear position correction. |
| XPBD | Compliant position constraints with velocity reconstruction and rigid-body contact/friction handling. |

These distinctions come from the [Solver2D overview](https://box2d.org/posts/2024/02/solver2d/).
The requirements below are Core integration and acceptance decisions, not claims
that upstream provides these product features or a certified 3D port.

### Distinct Implementations And Shared Work

- Implement new PGS independently of the Skullbonez execution path. Audit custom
  contact preparation, support seeds, friction, convergence stopping and positional
  cleanup before sharing any part; do not inherit them accidentally.
- Share mathematical kernels and geometry only where the numerical contracts
  match. Dispatch once at an appropriate world/solve boundary, with concrete
  composition and no callbacks or virtual dispatch inside constraint loops.
- Every choice owns an explicit schedule and typed settings/state. Avoid a free
  combination of flags that permits unnamed hybrids. Tests must demonstrate the
  distinguishing operation and actual selected execution, not demand different
  outputs on trivial scenes where algorithms can legitimately agree.
- Port contacts and existing point joints for every algorithm. A contact-only
  backend, implicit joint delegation to Skullbonez, or a label sharing another
  algorithm's entire execution does not complete its phase.
- Define 3D angular updates, full rotated inertia and two-direction friction.
  For block solving, specify deterministic manifold grouping, block size,
  conditioning and a bounded fallback for degenerate blocks. A fallback is local
  to the block algorithm, observable, and cannot become a global backend fallback.
- NGS variants must correct orientation as well as translation where appropriate.
  Test their positional pass independently from physical kinetic velocity and
  show that Skullbonez's existing cleanup is not applied again afterward.
- Sticky mode needs its own friction-anchor lifecycle across ticks, rotation,
  slipping, contact replacement, teleport and reset. Impulse warm starting must
  remain unavailable for this mode; retaining anchors is a separate mechanism.
- XPBD needs a declared compliance/unit convention, timestep scaling, multiplier
  lifetime/reset policy, angular correction, velocity reconstruction, restitution
  and static/dynamic friction treatment. Position multipliers are not impulse
  caches. Its rigid-body scope does not add cloth, fluids or deformable bodies.
- Soft variants need separate constraint-frequency/damping controls and a bounded
  relaxation schedule. Simulation frequency must never be confused with constraint
  frequency. Inapplicable settings must not silently alter another algorithm.

### Time, State And World Ownership

- Normalize outer tick, substeps, velocity/position iterations and relaxation
  passes separately with finite bounds. Record which counts are fixed or exposed
  by each algorithm. More sweeps at an unchanged pose do not constitute TGS.
- Each temporal schedule must advance intermediate poses and refresh the required
  row geometry. Define anchor validity and bounded contact refresh for primitives,
  hulls and terrain, including contacts born or lost within an outer tick.
- Trace force sampling, free motion, collision advancement and final writeback.
  Account for gravity, mutual gravity, drag and torque once over the admitted time.
  Intervals sum to one outer tick; existing TOI advancement and each body's remaining
  CCD time must never be integrated twice. Test no-contact and late-woken bodies.
- Specify restitution timing and thresholds, warm-start scaling, friction limits,
  rolling/spin resistance and joint ordering for each algorithm. Record impulse,
  force and position-multiplier units at their conversion boundaries; existing
  sleep/release thresholds must not consume differently scaled quantities.
- Preserve outer-tick gameplay, sleep and event cadence. Prove waking, moving
  support and fixed-contact release affect the correct connected identities.
- Keep temporary poses, multipliers and anchors in bounded Physics-owned storage.
  Measure setup reserve, peak scratch and retained bytes per algorithm; no new
  post-startup allocation privilege. Use stable identities for retained contacts.
- Use displacement/rotation representations with measured precision at large world
  coordinates. Test slow movement far from the origin and compare translated
  fixtures; do not infer precision from an origin-only stack test.
- Inventory every continuation value for clone/restore. Reject wrong algorithm,
  incompatible state version or incomplete state before replacing a valid world.
  Diagnostics carry algorithm, outer tick, substep, iteration and pass identity;
  unavailable measurements are marked unavailable, never synthesized as zero.

## Phases And Acceptance

- [ ] **SA0 - Fix all eight numerical contracts.** Pin references; record equations,
  units, schedules, settings bounds, joint treatment, retained-state lifetime and
  2D-to-3D decisions for every new algorithm. Define acceptance tolerances, run
  durations and CPU/work budgets before observing candidates. Preserve Skullbonez
  output/hashes and replace the former two-algorithm estimate with a scoped estimate.
- [ ] **SA1 - Prepare shared 3D execution support.** Implement bounded scratch,
  geometry/anchor updates, time accounting and publication needed by the new
  algorithms. Share only operations justified by SA0. Test rotated inertia,
  contact refresh, capacity limits and no-contact motion; retain Skullbonez parity.
- [ ] **SA2 - Implement new PGS.** Add distinct velocity/impulse execution, contact
  and joint behavior under the PGS contract. Prove its selected identity, impulse
  limits, cold/warm behavior and overlap response; audit against accidental custom
  Skullbonez policy reuse. Run the applicable common matrix.
- [ ] **SA3 - Implement PGS NGS.** Add the separate nonlinear position schedule for
  contacts/joints, angular correction and refreshed geometry. Prove the position
  pass does not inject physical velocity or duplicate other penetration cleanup.
  Run the applicable common matrix, including initially overlapping bodies.
- [ ] **SA4 - Implement PGS NGS Block.** Add the documented 3D coupled normal
  solve and deterministic degeneracy fallback. Exercise coupled, single-point,
  redundant, nearly singular and changing manifolds; measure block use and cost.
  Prove that well-conditioned fixtures actually execute coupled solving.
- [ ] **SA5 - Implement PGS Soft.** Add normalized soft coefficients, contact/joint
  solving and relaxation. Test parameter limits, timestep response and pass order,
  including that relaxation does not integrate body positions a second time.
- [ ] **SA6 - Implement TGS Sticky.** Add temporal solving and persistent friction
  anchors with explicit break/reset rules. Test sticking, transition to sliding,
  rotating support, contact turnover and no inter-tick impulse warm start. Prove
  actual intermediate-pose advancement and single-counted elapsed time.
- [ ] **SA7 - Implement TGS Soft.** Add soft substep contact/joint solving,
  warm-start scaling and relaxation. Test forces, restitution, effective mass and
  settings across substep counts; prove the schedule is distinct from PGS Soft.
- [ ] **SA8 - Implement TGS NGS.** Couple substeps to nonlinear contact/joint
  position solving. Test position/velocity separation, updated angular geometry,
  high mass ratios and absence of duplicate bias or integration.
- [ ] **SA9 - Implement XPBD.** Add the complete rigid-body position/multiplier
  schedule, angular treatment and velocity reconstruction. Test compliance and
  timestep scaling, sliding/sticking, restitution and large-coordinate precision.
  Prove it executes XPBD rather than an impulse backend with an XPBD label.
- [ ] **SA10 - Close all eight world lifecycles.** Run terrain/CCD, moving support,
  point joints/ragdolls, sleep/wake, fixed-contact release, empty worlds and bodies
  admitted mid-tick through every backend. Close any restrictions left during
  construction; feature omissions cannot silently become final scope reductions.
- [ ] **SA11 - Complete retained state and diagnostics.** For all nine algorithms,
  prove uninterrupted versus clone/restore continuation, reset/removal behavior,
  wrong-state rejection and observational diagnostics. Record exact downstream
  format requirements for stage 3 and memory/work bounds for every backend.
- [ ] **SA12 - Prepare comparative acceptance.** Complete the repeatable matrix
  harness, scene/settings manifests, metrics and bounded work/CPU comparisons for
  nine algorithms, repeated processes and supported worker counts. Use focused
  smoke checks to prove selection and measurement; schedule the full matrix for
  SA13. All eight new solvers need separate evidence and retained failure output.
- [ ] **SA13 - Terminal validation and review.** Complete independent numerical
  and ownership review, execute the full nine-algorithm matrix, record quality/cost
  results and limitations, and recheck unchanged Skullbonez numerical baselines.
  Complete final repairs and mapped gates, then plan-completion validation.
  Supply stage 3 with nine identities and eight accepted additions; a partial
  selection menu or default-only gate is not completion.

Implementation follows SA0-SA1, then the algorithm phases in the order above,
then SA10-SA13. Each algorithm phase includes focused contact/joint proof;
SA10 and SA12-SA13 add integrated coverage rather than excusing incomplete algorithms.
Keep heavy validation and independent review at SA13 except required earlier
push-boundary checks. Every checked phase needs its stated evidence.

## Required Acceptance Matrix

Run each row for Skullbonez and all eight additions unless a setting is explicitly
inapplicable to an algorithm. Inapplicability concerns controls, not permission to
omit basic contacts, terrain, point joints, CCD, prediction or replay support.

| Case | Required evidence |
|---|---|
| Free motion, gravity, torque, zero contacts | Correct total time/forces; finite state |
| Primitive and convex-hull contacts | Correct identities, penetration bounds and energy behavior |
| Flat/sloped/uneven terrain and moving support | Support, friction, rolling/spin and wake behavior |
| Tall/mixed stacks, cold/warm starts | Penetration, drift, settling, sleep and measured cost; applicable cache modes |
| Initially overlapping bodies and rotating contacts | Bounded repair, correct angular response, no unexplained energy injection |
| Ragdolls and joint chains; large mass ratios | Anchor error, contact/joint coupling and repeatability |
| Fast bodies/projectiles and terrain impacts | Required CCD impacts; no duplicate integration or restitution |
| Fixed-contact release, removal and teleport | Correct released/woken identities; stale state invalidated |
| Slow bodies far from origin | Bounded translation/rotation error and stable velocity reconstruction |
| Timestep, iteration, substep and material sweeps | Predeclared bounds and numerical limits; no per-scene tuning to hide failures |
| Reset, clone and supported snapshot restore | Same-algorithm continuation and incompatible-state rejection |
| Diagnostics on/off and supported worker counts | Unchanged physics within the pinned build envelope |
| Singular blocks, lost anchors and invalid inputs | Bounded documented fallback or explicit failure; no backend substitution |

Preserve scene/state identity and tick schedule. Compare both documented work
counts and measured CPU budgets; equal numeric iteration counts are not equal
cost across algorithms. Record energy drift, maximum/RMS penetration, joint
error, sliding, sleep/wake outcomes, CPU time, row visits and memory. Set limits
before runs and distinguish legitimate toppling from numerical failure. Failure
to beat Skullbonez is acceptable; violating the correctness contract is not.

## Validation And Closure

Focused coverage belongs with Physics algorithms, lifecycle and determinism,
including `SkullbonezTests/TestPersistentContactSolver.cpp`,
`SkullbonezTests/TestDeterminism.cpp` and the existing point-joint tests. New test
files name their owning subsystem. Register any new solver-specific validation
entry points in ordinary gate mappings; no algorithm relies on an optional demo.

Terminal commands from the repository root:

```bat
tools\validate_fast.bat
tools\validate_dependency_graph.bat
tools\validate_physics.bat
tools\validate_physics_deep.bat
tools\validate_perf.bat
tools\validate_full.bat --plan-completion
```

The existing commands do not imply eight-algorithm coverage. Extend their owning
fixtures/selection paths during implementation and record exact invocations and
observed identities for all nine algorithms. Apply extra replay/serialization
file-to-gate mappings; reuse checks already covered by the same final umbrella.

Use `Agentic/Skills/skarness/SKILL.md` for native acceptance. Extend missing
controls/topics, assert identities and outcomes, inspect relevant screenshots
and stop owned sessions. Preserve executable identity, settings, commands,
timings and bounded diagnostics under
`TestOutput/solver-selection/algorithms/<durable-key>/`.

Keep Skullbonez numerical goldens unchanged. Add separate new-solver fixtures
under AGENTS.md's active Physics golden policy, with focused negative controls.
Never refresh Skullbonez goldens to absorb new-backend results. Leave local
Profile built and prove an unchanged rebuild performs no compilation/linking.

Hand stage 3 the per-algorithm settings/state versions, applicability table,
memory limits, reference revisions, measured results and remaining product work.
Reconcile MASTER-PLAN and SessionState at activation/closure. Before deleting
this completed plan, repair sibling links to closure commits/current sources.
Product integration stays parked until activated. No PR or merge is implied.
