# Catto Divergence Repairs

Date: 2026-08-15
Status: **UNRATIFIED — review artifact, not live work. 0/6 phases complete.**
Impact area: Physics contact solver, joints, CCD sequencing, terrain rest
policy, physics baselines, tests, documentation
Owner: Physics contact solver
Priority: Owner review pending

## Registration Status — Read First

This plan is **deliberately not registered in `Agentic/Plans/MASTER-PLAN.md`**.
The master ledger is the authoritative selector for the nightly agent loop, so
an unregistered file in `TODO/` is inert: no run will select a task from it.

It was written as a review document at owner request. Activating it is a
one-line owner action — add it to the MASTER-PLAN ledger with a task count.
Do not self-register it, and do not begin implementation from this file alone.

Every repair below changes physics-visible behavior and therefore changes
byte-exact CSV baselines. Baselines are owner-controlled. No repair may be
implemented-and-rebaselined in one motion; each needs an explicit owner ruling
on that exact baseline transition first.

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
| **R4. CCD advances bodies mid-tick, but the solver still uses the full `dt`.** `PersistentContactSolver.cpp:901` and `:1087`. Catto Section 6 assumes one uniform delta-t across the system; Equations 34-35 are written for it. Here swept narrowphase and terrain advance bodies to time-of-impact and consume per-body fractions of the tick via `m_timeRemaining`, the solve runs on that mixed configuration, then remaining time integrates per body — yet `invDt` and the friction bound `mu * m_c * g * dt` both use the whole tick. | Two options, not to be bundled. Cheap and local: feed per-body remaining time into the bias and friction-bound terms so the numbers mean what Catto's derivation says they mean. Structural and preferred: adopt speculative contacts so bodies are never position-advanced mid-tick — create the contact with positive separation and let the normal row's bias prevent the tunnel, preserving a uniform step. | The solver operates on the system its own mathematics describes. Removes per-body time skew from the contact solve. The friction budget becomes correct for partially-advanced bodies. The structural option likely deletes the TOI-advance path outright, simplifying the tick. |
| **R5. Terrain restitution is divided by manifold point count; object contacts are not.** `PersistentContactSolver.cpp:1033` versus `:1051`. One physical quantity, two formulas. Bounce becomes a function of how many rows the manifold emitted, so a flat landing on four points behaves differently from an edge landing on two, beyond what the physics justifies. | Delete the `/pointCount` division and use one restitution formula on both paths. If the division exists to suppress multi-row bounce amplification, then the real defect is restitution being applied per row: compute the restitution target once per manifold from the pre-solve approach velocity and distribute it, rather than scaling each row. | Bounce height stops depending on manifold cardinality. One restitution formula to reason about and test instead of two. Removes a damping term that is currently disguised as restitution and therefore invisible to anyone tuning damping. |
| **R6. Velocity snapped to zero on hardcoded thresholds inside the solve transaction.** `PersistentContactSolver.cpp:1649-1660` in `ApplyTerrainRestPolicy`. Unconditional energy destruction below 0.05 linear and 0.02 angular, hardcoded while every neighbouring threshold is config-exposed, and applied before writeback where the closed-solve energy oracle cannot attribute it. Catto Section 9.1 is explicit that his stack needed no damping. | Delete the snap and move the responsibility to `PhysicsSleepController`, which already owns quiet-frame counting with configurable thresholds. If a snap is genuinely required to reach sleep, derive its thresholds from the sleep thresholds rather than restating them as literals, and apply it after writeback so `ContactEnergyOracle` can see it. | Removes unaccounted energy destruction from inside the solve. Restores one owner for "is this body quiet" instead of two disagreeing ones. Makes the thresholds tunable and visible. Lets the energy oracle measure the true solve rather than a post-damped one. |

### Repair Sequencing Note

R1, R5 and R6 are local and independently landable. R3 is local but changes
convergence behavior on every scene. R2 and R4 are structural and each deserve
their own owner decision. None of these are the parked stack-stability
experiments — they are correctness repairs to the current solver, and the WNF
plan's non-goal "do not bundle multiple solver techniques into one
experimental result" applies to them equally.

R1's structural alternative (Bullet-style split impulse) is **CS3 in
`Agentic/Plans/WNF/contact-stack-stability-techniques.md`** and stays parked.
Do not import it into this plan.

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

- Do not register this plan in `MASTER-PLAN.md` without an explicit owner decision.
- Do not refresh any physics CSV, SkullScope, replay or visual baseline. Each
  repair needs a separate owner ruling on its exact baseline transition.
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
against the pre-repair code, an owner-approved baseline transition where
behavior moved, the mapped validation gates run with pasted output, and an
independent ownership review of the touched surface.

## Validation Mapping

Solver and joint sources are physics hot paths, and contact row fields are
hashed into replay artifacts at `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1330`,
so replay fidelity is a cumulative gate rather than an alternative one.

| Scope | Required pre-commit gate |
|---|---|
| Any of R1-R6 in `PersistentContactSolver.cpp` or `Ragdoll.cpp` | `tools\validate_physics.bat`, then `tools\validate_replay_visual_fidelity.bat` |
| R3 or R4 (convergence and stepping semantics) | Add `tools\validate_perf.bat`; broad scope makes `tools\validate_full.bat` the safer gate |
| New or changed tests in `SkullbonezTests/` | `tools\validate_tests.bat` |
| Documentation repairs only | No validation required |

## Phases

- [ ] **CD0 — Owner ratification.** Owner decides which of R1-R6 are in scope,
  in what order, and pre-authorizes the baseline transitions each will require.
  Nothing below starts before this.
- [ ] **CD1 — R1 position projection.** Per-body accumulation with max-based
  reduction, plus the four-row-versus-one-row displacement test.
- [ ] **CD2 — R5 and R6.** One restitution formula across both paths; snap
  responsibility moved to the sleep controller. Land as two commits.
- [ ] **CD3 — R3 termination criterion.** Max-based early-out; sum retained for
  diagnostics only.
- [ ] **CD4 — R2 joints.** Sub-steps (a) through (d) in order, each measured
  against ragdoll sag under load. Stop and re-decide after (a) if warm start
  alone closes the gap.
- [ ] **CD5 — R4 stepping.** Owner picks the local or structural option before
  any code moves. Structural option is a pipeline change, not a solver tweak.

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
