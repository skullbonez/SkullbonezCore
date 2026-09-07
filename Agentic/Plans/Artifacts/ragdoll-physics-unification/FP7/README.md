# FP7 shared contact and joint iteration

The PhysicsContactSolverStage owns contact rows, warm caches and side effects.
ConstraintSolveTransaction owns fixed-step preparation, canonical candidate
ordering, shared solver-body velocities, joint blocks, island convergence,
writeback and publication. The old Ragdoll::SolvePointJoints production pass
is deleted. Ragdoll retains topology and the separate neck angular policy.

## Ownership and storage

| Owner | Retained data | Lifetime and bound |
|---|---|---|
| ConstraintSolveTransaction | Solver bodies and reactivated body identities | Preallocated to scene body capacity |
| ConstraintSolveTransaction | Canonical candidate pairs | Existing scene candidate-pair capacity |
| ConstraintSolveTransaction | PointJointBlock and typed iteration diagnostics | Preallocated to actual scene joint capacity |
| ConstraintIslandSchedule | Parent and convergence arrays | One row per scene body; static anchors do not merge dynamic components |
| PhysicsContactSolverStage | Contact rows, caches, visualization and release effects | Existing per-tick pair and body capacities |
| PhysicsSleepController | Separate joint-wake parent/rank scratch | One row per scene body; retained contact sleep topology is preserved |
| PhysicsTerrainStage | Initial and newly awakened terrain manifolds | At most one eight-point manifold per body |

No PhysicsBodyRecord field, runtime-growth privilege, or replay reserve cap is
added. The combined initial and continuation object-manifold stream shares one
existing per-tick pair budget. Before accepting a manifold, both contact rows
and its two visualization identities must fit; overflow fails before either
publication changes. The above-256-body boundary fixture exercises the capped
case, final acceptance, rejection and unchanged allocation.

## Iteration and release ordering

Both constraint families warm-start before sweeps. Each sweep solves contact
normal/friction rows, terrain rows and joint blocks against shared velocities.
Joint-connected components execute at least eight sweeps; contact-only
components retain their configured minimum. Maximum squared impulse change
controls each independent component rather than a sum over unrelated rows.
Body identity orders roots and pairs, joint handle orders blocks, and the
manifold builder retains its deterministic geometric feature order.

Fixed-contact release suspends before cache publication. Force-aware waking
updates the complete joint-connected component, then zero-horizon broadphase
and terrain queries append missing current-pose contacts. Existing rows retain
impulses and restitution targets. Only newly activated joints clear stale
impulses; unrelated sleeping caches remain exact. Restricted continuation
solves and writes only the released components, without another integration,
position correction, release decision, or repeated warm start. Final cache
publication occurs once. Typed joint diagnostics describe the last block visit,
including island identity; they do not claim a residual recomputed after every
later block in the sweep.

## Evidence

Debug, Profile and Automation builds pass. The expanded focused suite passes
183 cases and 2,552,691 assertions. Coverage includes supported articulation,
impact release from a fixed/sleeping state, newly awakened floor support,
unrelated island exactness, sleeping cache retention, permutation/endpoint
ordering, phase legality, bounded publication, and 100 warm steady-state steps
under the runtime allocation guard. The final changed-source check passes five
sources and twelve compiler contexts. Project-filter and dependency ownership
checks pass. Independent implementation review reports no findings.

The Physics 0/repeat/1/4-worker streams are byte-exact with each other. Their
44,401-line CSV intentionally changes from 03088b30... to 50bca7c0... because
per-component convergence changes contact-only iteration as well as coupled
joint response. The first difference is frame 51, body 15 (base_b), maxImpulse
4.915129 to 4.915102; later trajectories accumulate meaningful differences.
The append-only transition preserves exact old/new producers. This is not a
claim of trajectory equivalence with FP6.

The complete source-design check passes 126 files / 1,150 compiler contexts.
The advisory inventory covers four owning headers across 60 contexts. Its six
wide operations are the explicit contact transaction boundaries (preparation,
terrain rows, writeback and entry/continuation), each below the enforced
12-parameter failure threshold. They borrow separately owned body, collider,
sleep, terrain, joint and diagnostics values; no retained broad context or
callback aggregate is introduced. Independent review accepts these explicit
boundaries. Raw inventory is retained in ownership-inventory.txt.

The static allocation gate reports 95 findings in both the exact FP6 parent
and FP7, with identical normalized identities and no additions. An earlier
FP6 log reported 91 before its final archive fix; the fresh parent comparison
is authoritative here. Updated exact policy sites reflect renamed owners and
the existing non-growing list wrappers, not new allocation permission.

The first full 200-box replay capture passes all 2,401 frames, 201 causal
nodes, all 200 moved/settled wall bricks, durable artifact equality and archive
round-trip checks. The visual transition is 9499cda6... to 22ce605c...; the
causal transition is 128d2bd0... to e3ad2a89..., with exact FP6/FP7 Automation
producers and identical v5 scene inputs in shared-constraints-22ce605c/.

Dynamic gameplay allocation smoke and the selected-path structural regression
pass. The physics benchmark passes absolute budgets and its baseline comparison.
DX12 passes absolute budgets; its initial Render average comparison was +16.5%
against a 15% threshold while total Frame time improved 1.9%. A separate repeat
passes every comparison without refreshing a performance baseline. Both raw
runs remain under TestOutput/ragdoll-physics-unification/FP7/performance/;
summary JSON artifacts are retained here.

The causal performance fixture had a retired UI-switch action and failed before
frame zero. That action is removed from the three affected repository-owned
causal manifests. The corrected fixture completes its actions but exposes an
inherited render allocation violation: 13 allocations of 32 bytes (416 total),
callsite parent RVA 0x15c912. The exact archived FP6 executable reproduces all
13 allocations at the same site with identical corrected scene/script inputs.
This lane remains failed, independently of FP7. Evidence is under the local
causal-perf-fixed/ and causal-perf-parent/ directories. No allocation exception
or relaxed assertion conceals it. Full-plan closure must address that existing
render issue and the 95 static allocation findings, or explicitly report them.

Measurement-only scale runs at 200, 520, 1,000, 2,000 and sleepy-5,000 bodies
all complete and produce analyzable timing artifacts. These are measurements,
not comparisons against ratified per-scale performance baselines.

The independent full native replay repeat and all negative controls pass
(TestOutput/fp7-replay-repeat.log). FP7 is accepted at 8/10; FP8 is next.
