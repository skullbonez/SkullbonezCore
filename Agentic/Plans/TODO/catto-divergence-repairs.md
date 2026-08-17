# Catto Divergence Repairs

Date: 2026-08-15
Status: **Live — registered by owner direction 2026-08-15. 2/6 phases complete.**
Impact area: Physics contact solver, joints, CCD sequencing, terrain rest
policy, physics baselines, tests, documentation
Owner: Physics contact solver
Priority: CD2 R5 terrain restitution, then R6 sleep-owned quieting

## Scope Gate — Read First

This plan is registered in `Agentic/Plans/MASTER-PLAN.md` and is selectable by
a plan runner. The completed CD0 ruling below authorizes CD1-CD5 in the recorded
order and scope.

CD0 is owner scope ratification. A run may not select CD1 or any later phase
until the owner has recorded, in this file, which of R1-R6 are in scope and in
what order. **A run may not answer CD0 on the owner's behalf.** Deciding which
repairs to perform is the owner's judgement, not a task an agent completes by
reasoning about it; a run that finds CD0 unanswered must stop and report rather
than proceed.

Every repair below can change physics-visible behavior and byte-exact CSV
baselines. The owner's `Include` answers authorize the behavior described for
each selected repair and baseline movement isolated to that behavior. Never
blindly rebaseline: run the focused evidence first, inspect the diff, and stop
for owner review only when output moves outside the approved repair's stated
effect.

## Evidence Basis

Source read on 2026-08-15 against
`Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf` ("Iterative
Dynamics with Temporal Coherence", Catto 2005). Paper claims below were
verified by text extraction from that PDF, not from recollection. Notably the
paper contains **zero** occurrences of "restitution" and **zero** of "island",
and Section 9.1 states its ten-box stack used no sleeping and no damping.

`Agentic/Audits/physics-solver-catto-reference-audit.md` (2026-06-01, "Draft
for review") was **not** used as evidence: it is stale. See Documentation
Repairs below.

---

## Section 1 — Divergences To Repair

Third column states the benefit of performing the repair.

| Divergence and site | How we might fix it | Benefit of fixing |
|---|---|---|
| **R1. Position projection stacked on Baumgarte, and it multiplies by manifold row count.** `PersistentContactSolver.cpp:1802-1873`. Catto uses velocity bias only (Section 4.2). Here each row applies its own 35 percent correction from its own stale `c.penetration`, never re-reading position or re-deriving separation, so a four-point face manifold receives roughly 1.4x the intended correction rather than 0.35x. Also linear-only: no orientation correction. | Accumulate per-body correction in a scratch array during the row loop, reduce by **max required per normal direction** rather than sum, then apply once after the loop. Alternatively re-derive separation per row from current positions, as Box2D 2.4's position solver does. Add a focused test pinning that a four-row manifold produces the same body displacement as the one-row case at equal penetration. Do **not** fold in split-impulse here; that is CS3 in the parked WNF plan. | Penetration recovery stops depending on how many points the clipper happened to emit. Removes a hidden coupling between narrowphase row count and positional behavior. Establishes an honest baseline: split impulse cannot be evaluated fairly against a projection pass that itself over-corrects. |
| **R2. Joints sit outside the constraint system and lose warm start.** `Ragdoll.cpp:383-491`. Catto lists "joints and contacts are handled in the same way" as an advantage of the formulation (Section 1). Here joints run as a separate pass after contacts, with no accumulated impulse and no warm start, a scalar row along the current error axis instead of a 3-DOF point-to-point block, ad-hoc softness `(relVel + biasSpeed) * (1 + damping)`, and their own direct position projection. | Staged. (a) Give joints an accumulated impulse cached on constraint handle, reusing the contact cache pattern; measure ragdoll sag under load before and after. (b) Replace the scalar error-axis row with a 3-DOF point-to-point block so the constraint pins coincidence, not distance. (c) Replace the damping multiplier with either Baumgarte consistent with the contact rows or an explicit documented soft constraint. (d) Only then consider folding joint rows into the shared PGS array so one sweep sees both families. | Ragdolls stop being materially softer under load than the contact system. Contacts and joints stop fighting each other across passes — today a body pinned under a box is pushed out by contacts and pulled back by joints, with neither seeing the other's impulses. Joint stiffness becomes a physical parameter instead of an iteration-count artifact. |
| **R3. Global solver early-out uses a sum where Catto specifies a max.** `PersistentContactSolver.cpp:1372`. Breaks when total squared delta-lambda across all rows falls below a fixed 1e-6. Catto Section 7.1 lists `max abs(delta x_i)` below tolerance; a sum reports nothing about any individual row, and an absolute threshold unscaled by row count makes the criterion scene-size dependent. | Track maximum per-row squared delta-lambda alongside the existing sum and gate on the max, which is Catto's stated criterion. Keep the sum for diagnostics only. Island-local termination is strictly better but depends on CS1 in the parked WNF plan, so do not couple the two. | Converged small islands stop burning iterations held up by one large island; unconverged islands stop exiting early because many neighbours went quiet. Removes a scene-size-dependent term from a simulation whose contract is byte-exactness. |
| **R4. CCD advances bodies mid-tick, but the solver still uses the full `dt`.** `PersistentContactSolver.cpp:901` and `:1087`. Catto Section 6 assumes one uniform delta-t across the system; Equations 34-35 are written for it. Here swept narrowphase and terrain advance bodies to time-of-impact and consume per-body fractions of the tick via `m_timeRemaining`, the solve runs on that mixed configuration, then remaining time integrates per body — yet `invDt` and the friction bound `mu * m_c * g * dt` both use the whole tick. | Retain the owner-authored partial-TOI architecture: advance to impact, solve the redirected velocity, and integrate that velocity through the remainder of the same fixed step. Define an explicit contact interval from the participating bodies' remaining-time values, then use that interval consistently for Baumgarte scaling and friction impulse bounds. Prove terrain, dynamic/dynamic, fixed/dynamic, near-zero remainder, and unequal-remainder cases. Speculative contacts are a separate future design in `Agentic/Plans/TODO/future_physics.md`; they are not part of R4. | The solver's time-scaled row terms describe the interval in which the contact actually exists without discarding the partial-step CCD design. The friction budget and penetration correction become consistent with post-impact integration while speculative-contact risk remains separately reviewable. |
| **R5. Terrain restitution is divided by manifold point count; object contacts are not.** `PersistentContactSolver.cpp:1033` versus `:1051`. One physical quantity, two formulas. Bounce becomes a function of how many rows the manifold emitted, so a flat landing on four points behaves differently from an edge landing on two, beyond what the physics justifies. | Delete the `/pointCount` division and use one restitution formula on both paths. If the division exists to suppress multi-row bounce amplification, then the real defect is restitution being applied per row: compute the restitution target once per manifold from the pre-solve approach velocity and distribute it, rather than scaling each row. | Bounce height stops depending on manifold cardinality. One restitution formula to reason about and test instead of two. Removes a damping term that is currently disguised as restitution and therefore invisible to anyone tuning damping. |
| **R6. Velocity snapped to zero on hardcoded thresholds inside the solve transaction.** `PersistentContactSolver.cpp:1649-1660` in `ApplyTerrainRestPolicy`. Unconditional energy destruction below 0.05 linear and 0.02 angular, hardcoded while every neighbouring threshold is config-exposed, and applied before writeback where the closed-solve energy oracle cannot attribute it. Catto Section 9.1 is explicit that his stack needed no damping. | Delete the snap and move the responsibility to `PhysicsSleepController`, which already owns quiet-frame counting with configurable thresholds. If a snap is genuinely required to reach sleep, derive its thresholds from the sleep thresholds rather than restating them as literals, and apply it after writeback so `ContactEnergyOracle` can see it. | Removes unaccounted energy destruction from inside the solve. Restores one owner for "is this body quiet" instead of two disagreeing ones. Makes the thresholds tunable and visible. Lets the energy oracle measure the true solve rather than a post-damped one. |

### Repair Sequencing Note

R1, R5 and R6 are local and independently landable. R3 is local but changes
convergence behavior on every scene. R2(a) and the local R4 interval repair each
deserve their own measured baseline decision. None of these are the parked stack-stability
experiments — they are correctness repairs to the current solver, and the WNF
plan's non-goal "do not bundle multiple solver techniques into one
experimental result" applies to them equally.

R1's structural alternative (Bullet-style split impulse) is **CS3 in
`Agentic/Plans/WNF/contact-stack-stability-techniques.md`** and stays parked.
Do not import it into this plan.

### Owner Scope Ruling — 2026-08-16

The owner selected this execution order:

1. R1 position projection.
2. R5 terrain restitution.
3. R6 sleep-owned quieting.
4. R3 max-based convergence.
5. R2 stage (a) only: accumulated joint-impulse caching and warm start, followed
   by the planned ragdoll-sag measurement.
6. R4 local interval consistency, retaining the partial-TOI architecture.

R2 stages (b) through (d) and speculative contacts are excluded from this plan.
They are described as unregistered future work in
`Agentic/Plans/TODO/future_physics.md`, which is intentionally absent from
`Agentic/Plans/MASTER-PLAN.md` by owner direction.

The owner's earlier `Include` answers approve both the selected scope and the
primary repair behavior described in the R1-R6 table; they are not merely
permission to ask the same repair questions again in finer language. R2 is
limited to stage (a), and R4 preserves the owner-authored partial-TOI design.
Baseline movement is authorized only where focused evidence attributes it to the
selected repair. An unexpected or unrelated physics, replay, SkullScope, or
visual difference still blocks the touching phase and returns to the owner with
the measured evidence.

### R1 Exact Behavior And Baseline Ruling — 2026-08-16

The owner approved one position correction per contact manifold, using the
deepest retained penetration rather than summing a correction for every manifold
point. Apply that linear correction once along the manifold normal and share it
between the participating bodies according to their existing inverse-mass
weights. Retain the current contact slop and 35 percent correction strength. R1
does not add angular position correction, split impulse, or any other solver
technique.

The expected R1 baseline transition is approved where removing manifold
point-count multiplication changes motion. The one-point and four-point forms
of an otherwise equivalent face contact must produce the same separation. Any
other physics, replay, SkullScope, or visual difference remains unapproved and
blocks the transition.

### Owner Baseline Artifact Ruling — 2026-08-16

A physics-baseline mismatch in a Catto repair phase is evidence to preserve,
not a stop condition for this orchestration run. Before another build can
overwrite a failing candidate, copy every executable used by the gate and name
each copy with the plan and phase (for example,
`SKULLBONEZ_CORE_CATTO_REPAIRS_CD3.exe`). Record the baseline diff and saved
artifact path in the phase evidence, then continue in plan order. This ruling
supersedes the earlier instruction to stop solely for unexpected baseline
movement during this run; it neither refreshes a golden nor accepts the changed
physics behavior. The owner will inspect all preserved candidates and decide
which phases are acceptable. Build failures, crashes, invariant failures, and
non-physics correctness failures remain blocking.

### CD1 Baseline Acceptance — 2026-08-17

After reviewing the preserved CD1 candidates and independent same-scene
attribution, the owner accepted CD1's simulation behavior as an improvement and
authorized the affected physics-derived baselines to be overwritten. This
acceptance is specific to CD1; later Catto phases still preserve and report
their candidates until the owner accepts each result.

---

## Section 2 — Divergences To Keep

These are places the engine departs from the 2005 paper and is right to. The
third column states what would be lost by "fixing" them back toward Catto,
plus any residual follow-up that remains genuinely open.

| Divergence and site | Why it departs from Catto | What reverting would cost, and residual follow-up |
|---|---|---|
| **I1. Friction clamped as a 2D cone, not two independent axes.** `ContactSolverCommon.h:69-87`. | Catto Section 4.3 says the tangent rows resist motion in the two directions "independently", with separate scalar bounds per Equations 24-25. That is a 2D-example artifact: independent clamping lets diagonal friction reach mu times root two. | Reverting reintroduces a 41 percent friction overshoot on diagonal slip. No residual work; this is simply correct and the source comment already records why. |
| **I2. Three friction bounds selected by contact classification.** `PersistentContactSolver.cpp:720`, `:1082-1087`, `:1299`. Established resting footprints keep Catto's exact constant bound `mu * m_c * g * dt`; contacts without a resting footprint use true Coulomb `mu * lambda_n`; terrain uses `mu * max(lambda_n, support seed)`. | Catto deliberately decoupled friction from normal force and named the cost himself: "box stacking friction is unrealistic because lower boxes slide just as easily as upper boxes." The selector `normalCoupledFriction = !hasRestingFootprint` keeps his bound where it was tuned to work and uses real Coulomb where his artifact is most visible. | Reverting to a single constant bound restores the artifact its author documented. **Residual follow-up (open):** friction model changes discontinuously when a body topples from face support to edge support. Worth characterising — it is a real discontinuity, not a defect, but nothing currently measures it. |
| **I3. Restitution exists at all, including suppression on load-carrying features.** `PersistentContactSolver.cpp:1038`, `:1051`, reasoning at `:1055-1061`. | The paper has no restitution model whatsoever — contact is purely inelastic plus Baumgarte. Everything here is engine-added. The `continuesLoadedContact` gate is a considered fix for warm-started resting contacts re-applying bounce and pumping energy into a settled stack. | Reverting removes bounce from the engine entirely. The suppression gate specifically prevents a known sequential-impulse energy-injection artifact. Residual follow-up is R5 above, which is about the terrain formula only, not about restitution existing. |
| **I4. Terrain gravity support seed.** `PersistentContactSolver.cpp:826-827`, applied as a floor at `:1168-1172`. | Not in the paper, but principled: it seeds the analytically-known support impulse for a body resting on a surface, which is what the converged solution would be. It is warm starting from an oracle rather than from history, and the source is careful to say so at `:816-821`. It also feeds the friction bound, so a body has static friction on its first frame of terrain contact. | Reverting makes grounded bodies sink visibly before the solver converges, and removes first-frame static friction on terrain. Seed is a floor rather than a target, so a light impact can start above what it needs — recoverable, since the normal row clamps at zero and can back it out. No follow-up required. |
| **I5. Sleep, rolling friction and the point-support nudge.** `PersistentContactSolver.cpp:1411-1561`, `:1563-1662`. | Catto Section 9.1 is explicit that his stack used no sleeping and no damping. Sleep here is a performance and determinism requirement at 211 bodies and is gated on support classification rather than raw velocity. The nudge at `:1532-1537` is honest symmetry-breaking: an exact unstable equilibrium is not physical, float exactness can preserve it forever, and the nudge is deterministic and seeded from feature identity. | Reverting costs the ability to run large scenes to rest, and lets authored-exact balances persist indefinitely. **Note the boundary:** the velocity snap inside this same pass is *not* covered by this justification and is R6 above. |
| **I6. Feature-ID-stable warm start with deterministic contact reduction.** `PersistentContactSolver.cpp:196-217`, `:688-700`. | Catto Section 8.1 caches on primitive pair plus contact-point identifier, which this matches. The deterministic reduction — deepest point first, remaining rows chosen to maximise tangent spread, ties broken by feature ID — goes beyond the paper. | Reverting to pair-only caching aliases badly the moment a manifold emits multiple rows, which is the exact failure the 2026-06-01 audit predicted before it was fixed. No follow-up. |
| **I7. Closed-solve energy and momentum oracle.** `ContactEnergyOracle.h`. | No analogue in the paper. Measures the whole solve as one closed mechanical system in double precision, with an explicit separation-work budget for Baumgarte so bias energy is accounted rather than hidden. | Reverting removes the only mechanical guard that would catch a solver change injecting energy. This is the instrument that makes R1 and R6 diagnosable at all. No follow-up. |

---

## Documentation Repairs

Independent of the solver work, and cheap.

| Item | Repair |
|---|---|
| `Agentic/Audits/physics-solver-catto-reference-audit.md` is stale and misleading. It cites `SkullbonezSource/SkullbonezGameModelCollection.cpp`, which no longer exists, and all four of its headline gaps are closed: bounding-radius contacts became real SAT plus reference/incident clipping; pair-only caching became feature-keyed at `PersistentContactSolver.cpp:196-217`; body-space inertia became `usesWorldInertia` for every non-sphere at `PhysicsEngine.cpp:796`; immediate swept impulses were removed per `Stages/PhysicsNarrowphaseStage.cpp:614`. | Delete it, or rewrite it against current source. A reader today would act on false premises. Git history is the archive. |
| Dead `Agentic/Reports/` links. The audit and `Agentic/Plans/WNF/contact-stack-stability-techniques.md:19` both cite report paths under a tree deleted repo-wide on 2026-08-03. | Remove the citations. `python tools/check_related_paths.py --repo .` is the live check for source `Related:` blocks; these are plan and audit prose and are not covered by it. |

---

## Non-Goals

- Do not expand the completed CD0 scope or reorder repairs without a new owner
  ruling.
- Do not refresh a physics CSV, SkullScope, replay, or visual baseline until
  focused evidence attributes every changed value to the approved repair; an
  unexpected difference returns to the owner with measurements.
- Do not raise the production solver iteration cap above 12.
- Do not bundle two repairs into one commit or one measurement.
- Do not import Bullet or Box2D source, or add compatibility abstractions,
  callback packs, service bags, or solver-wide context objects.
- Do not implement CS0-CS6 from the parked stack-stability plan under cover of
  these repairs.
- Do not treat a passing gate as proof a repair was correct; each needs its own
  behavioral evidence.

## Acceptance

A repair is complete when it has: a focused behavioral test that would fail
against the pre-repair code, a baseline transition isolated to the approved
behavior where output moved, the mapped validation gates run with pasted output,
and an independent ownership review of the touched surface.

## Validation Mapping

Solver and joint sources are physics hot paths, and contact row fields are
hashed into replay artifacts at `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1330`,
so replay fidelity is a cumulative gate rather than an alternative one.

| Scope | Required pre-commit gate |
|---|---|
| Any of R1-R6 in `PersistentContactSolver.cpp` or `Ragdoll.cpp` | `tools\validate_physics.bat`, then `tools\validate_replay_visual_fidelity.bat` |
| R3 or R4 (convergence and stepping semantics) | Add `tools\validate_perf.bat`; run `tools\validate_full.bat --plan-completion` only when the entire plan closes |
| New or changed tests in `SkullbonezTests/` | `tools\validate_tests.bat` |
| Documentation repairs only | No validation required |

## Phases

- [x] **CD0 — Owner scope and baseline ratification.** The 2026-08-16 rulings
  settle scope, order, primary repair behavior, and the boundary for isolated
  baseline movement. Unexpected measured effects still return to the owner; the
  included repairs do not require their scope questions to be asked again.
- [x] **CD1 — R1 position projection.** Per-body accumulation with max-based
  reduction under the approved R1 ruling, plus the
  four-row-versus-one-row displacement test.
- [ ] **CD2 — R5 and R6.** One restitution formula across both paths; snap
  responsibility moved to the sleep controller. Land as two commits.
- [ ] **CD3 — R3 termination criterion.** Max-based early-out; sum retained for
  diagnostics only.
- [ ] **CD4 — R2(a) joint warm start only.** Cache the accumulated joint
  impulse on the constraint handle and measure ragdoll sag under load. Stages
  (b) through (d) remain future work regardless of this phase's result.
- [ ] **CD5 — R4 local interval consistency.** Preserve partial-TOI advancement
  and remaining-time integration. Define and test the contact interval used by
  Baumgarte and friction terms. Speculative contacts remain future work.

### CD1 Evidence — 2026-08-17

CD1 now selects the deepest retained row in each contiguous manifold, applies
the existing slop and terrain/object strengths, accumulates inverse-mass-
weighted linear corrections in transaction scratch, and publishes each body
position once. It adds no angular correction or split impulse. The focused
one-row/four-row oracle passed as part of 13 persistent-solver cases / 172
assertions and would fail against the pre-CD1 row-summing implementation.

`tools\validate_tests.bat` passed 579 cases / 2,479,944 assertions. The Debug
physics build and lifecycle smoke passed, then the immutable varied golden
reported the expected physics-visible mismatch: 40,905 lines differ. Against
the preserved T4 candidate rather than the older golden, frames 0-289 are
byte-identical; CD1 first diverges at frame 290 and changes 27,242 later rows
through frame 1199 across 22 stack/drop bodies. Those measurements were retained
before the owner accepted CD1 and authorized the baseline refresh.

The direct CD1 candidate is retained under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD1/`: CORE
`80CB5C1591D987CF63D261DE22F7BD389269781C9741794A84750857379BFB27`,
TESTS `A737E6089D208922F8CC2C48A8AA7F8DA76C8C3291E60993B6987B3F5E75DBAD`,
and varied CSV
`BAB33781538AAAB360B887988ADD88E5C9FE4376F382B0A9B804F58B76993854`.

The replay-visual launcher, Automation build, and 17 focused cases / 75
assertions passed before the immutable causal golden reported
`topology[0].firstFrame` 185 -> 184. The candidate is retained under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD1_REPLAY_VISUAL_FIDELITY/`:
Automation CORE
`D048842BC0B7F0745060C2E211224E8B038C29EEFCED326FAFCF2100B1E2945D`,
Profile TESTS
`128DC837608278B63CB34A206027E3509B68E443A9EC9C190B4990EE8E5E6075`,
report `5E96E0421EC3D7FC6F0C847FF8119659513473C2AB302853D7BD083116B4B6F8`,
and replay artifact
`932DC9FADFBEED901D27743C4D90C2F1849BBCAC7406C0B62B138C1657F53765`.

The independent reviewer initially required same-scene attribution beyond the
golden's first difference. A fresh Automation build of exact pre-CD1 commit
`4bdd2603a` and the CD1 executable then ran the same scene, script, config,
shaders, and 6,800-frame command. Schema/counts, target/branch/source/event
mapping, cameras, topology identity `(id, parentId, depth)`, live replay
packets/events/cursors, generation/restart/readiness/reserve state, and artifact
structure all matched. Both runs proved `predictionSourceSolverHash ==
liveSolverHash` and stable across prediction. Divergence began only in the
Physics-owned private prediction: trajectory fingerprint
`0x70AF9D193F6E10E3 -> 0x1545457413BB5355` and RVPD hash
`8214111599449249960 -> 16376692502313136880`. A second CD1 run produced the
same canonical report projection
`E16AD39C7CA4CAF8E3DE4F809F3E7FACB313B5AF13CA72216FE0CA59D0D465D3`
and byte-identical replay artifact. The pre-CD1 attribution executable, report,
and replay are retained under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD1_REPLAY_ATTRIBUTION/` with
hashes `094668365D071EA0FF61D4A1D847776DC6F08BFD3FAF0BD9D78817CB258B118F`,
`2E972D6DA314C77A35015CF7847C2AB48F169CFA1B98769179B7427A47F3EC25`,
and `B34F1123483E31D8094E0E823536AC2E92D878FD681A985490C618F62BD8DB7A`.
The independent re-review therefore returned **CLEAR**: changed activation
timing and wall outcomes are deterministic downstream R1 Physics behavior, not
a replay correctness regression. The owner subsequently accepted the CD1
behavior and the varied, shooting, space, known-issue, query, replay-visual, and
causal goldens were refreshed from the validated candidate.

All seven ownership inventories are green: 88/88 aggregate rulings, 1/1
extraction-scar ruling, 40/40 complexity rulings, 1,780 build rows / 72 shared
files / 144 ruled divergences / 0 blocking, 599 glossary files / 0 blocking,
and strict reachability 81 rows / 0 blockers; the wide-signature inventory also
has no blocking row. Touched-source comment audit: 4/4 checked, 0 deferred.

## Reference Sites

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/ContactSolverCommon.h`
- `SkullbonezSource/Physics/ContactEnergyOracle.h`
- `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- `SkullbonezSource/Physics/Ragdoll.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf`
- `Agentic/Reference/physics-overview.md`
- `Agentic/Plans/WNF/contact-stack-stability-techniques.md`
