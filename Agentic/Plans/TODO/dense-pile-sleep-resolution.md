# Dense Pile Sleep Resolution

Date: 2026-08-03
Status: BLOCKED — 3/5 phases complete; SR3 candidate rejected, owner decision required
Impact area: Physics contact restitution lifetime, sleep controller, diagnostics, tests
Owner: Physics contact and sleep
Priority: Blocked lane — owner decision required; independent campaign plans continue

## Problem And Evidence

### The fix that caused this, and why it stays

The falling-wall bounce defect was fixed by gating contact restitution on contact
lifetime: a contact row that already carried load last frame does not receive
restitution this frame, so a settling stack cannot keep re-injecting bounce energy
into itself while a genuinely new impact still bounces normally.

The authoritative commit is **`194cbf82` — "Fix contact restitution lifetime and
retain wall striker"** (2026-08-02). It supersedes the broader form introduced in
`12dbb3eb` (Contact Energy ES3, "gate restitution by loaded pair lifetime"), which
itself descends from `63d7e92f` ("suppress persistent object restitution"). The
owner approved the resulting five-golden transition in `43108847`.

The current mechanism lives in `PersistentContactSolver.cpp`:
`InspectPreviousObjectContact()` looks up the row's **exact feature key** in the
persistent contact cache. If that cached row carried load — `accN > 0` or either
tangent above `TOLERANCE` — then `continuesLoadedContact` is true and the
restitution branch is skipped for that row. Terrain rows and elastic mutual-gravity
worlds never enter this path.

**This fix was worth it and is not up for reversal.** The measured result:

| Workload | Before | After |
|---|---|---|
| Four bricks, meaningful vertical reversals | 566 | 0, sleeping 4/4 by frame 132 |
| 200-box wall, dynamic body retention | body loss | all 211 retained |
| 200-box wall, popcorn cycles | repeated | zero, sleeps by frame 3286 under ≤12 iterations |

The owner accepted the five-golden transition specifically on the grounds that the
wall's materially reduced bounce and improved stack stability outweighed a marginal
ground-settling regression. That trade was made deliberately, with evidence, and
this plan does not reopen it.

### The regression this plan owns

The accepted "marginal ground-settling regression" is larger than marginal in dense
piles. Sleep is measurably harder to reach than it was before the restitution
lifetime gate landed.

`SkullbonezData/scenes/box_pile_throw_300.scene.json` is the right instrument: 303
authored objects thrown into a pile on flat terrain, gravity -32, seed 300042,
fixed-step, unlimited frames. It is currently referenced by **no suite, tool, test,
or plan**, so it carries no committed baseline and can be used as a diagnostic
workload without moving a gated artifact.

Current sleep policy for reference: `physics_sleep_linear_speed = 0.5`,
`physics_sleep_angular_speed = 0.3`, `physics_sleep_frames = 30`.

### Leading hypothesis, to be proved or discarded at SR1

The suppression is keyed on **exact feature identity**. The comment deleted by
`194cbf82` when it narrowed the lookup from body-pair prefix to exact feature states
the failure mode precisely:

> Any loaded row under this prefix means the current manifold continues a pair that
> was already carrying support last frame, **even when rocking selects a different
> face feature**.

Narrowing to exact-feature identity removed exactly that coverage. In a dense pile,
a box that rocks slightly re-selects a different face or edge feature between frames.
Each re-selection presents as a contact with no loaded history, so restitution is
admitted, a micro-bounce follows, and the 30-frame quiet counter resets. The same
churn also misses the warm-start cache, because `canUseCachedWarmStart` consumes
`previousObjectContact.exactFeature`, so support impulse is rediscovered from zero
on the frame the feature flips.

If that is the mechanism, the sleep regression and the warm-start miss rate are the
same phenomenon measured two ways, and the fix has to restore lifetime continuity
across feature re-selection **without** restoring the rejected pair-prefix scan.

## Goal

Dense-pile sleep resolution returns to at least its pre-`194cbf82` quality while the
wall bounce fix is fully retained: no renewed bounce on the four-brick or 200-box
workloads, no body loss, no popcorn cycles.

## Non-Goals

- **No reversal of the restitution lifetime gate.** The wall must not regain bounce.
  A candidate that improves sleep by re-admitting restitution on continuing contacts
  is a failed candidate, not a trade.
- **No naive return to the body-pair prefix scan.** That form existed in `12dbb3eb`
  and was rejected on measured performance grounds — the prefix walk scans every
  cached row under a pair on every row precompute. SR2 must state why any candidate
  resembling it does not carry the same cost, or reject it.
- No sleep-threshold tuning as the primary fix. Widening
  `physics_sleep_linear_speed`/`angular_speed` or shortening `physics_sleep_frames`
  would hide the residual motion rather than remove it. Threshold changes are
  admissible only as an explicitly reasoned secondary outcome after SR1 identifies
  the mechanism, never as a substitute for it.
- No deep-tower stacking work. That remains owner-parked under inventory rule 9 and
  is out of scope here.
- No baseline refresh without explicit owner approval, per the standing rule.
- No new solver stage, no position-solver pass, no substep change.

## Phases

- [x] **SR0 — Reproduce and quantify the regression.** Three clean detached
  Debug builds pin pre-gate `9b12badb`, pair-prefix `12dbb3eb`, and exact-
  feature `194cbf82`. Common 6,800-frame full traces plus 20,000-frame CSV
  sleep censors show the pair-prefix pile permanently sleeps 330/330 at frame
  8,513, while pre-gate and exact-feature remain oscillatory past 20,000. The
  exact-feature state retains four-brick permanent sleep at frame 132 and wall
  permanent sleep at frame 3,286 with zero tail kinetic energy. The repeatable
  measurement tool, full sleep/energy tables, provenance correction, and
  witness hashes are recorded in
  `../../Reports/2026-08-03/dense-pile-sleep-resolution-sr0.md`.

  Original work order: build the scene into a
  repeatable fixed-step diagnostic run and record sleep behaviour at three points in
  history: before `63d7e92f`, at `12dbb3eb` (pair-prefix form), and at `194cbf82`
  (current exact-feature form). For each, record time-to-first-sleep, time-to-quiescence,
  the count of bodies never sleeping, sleep/wake oscillation counts per body, and total
  kinetic energy over time. Record the same three-point history for the four-brick and
  200-box wall workloads so the retained benefit is quantified beside the regression.
  The deliverable is a table showing exactly what was gained and what was lost; do not
  proceed on the assertion alone.

- [x] **SR1 — Diagnose the mechanism.** The pair/exact traces prove exact-feature
  lifetime is the initiating defect: frame 24 is the first material divergence,
  where the same reselected feature solves 2.221948 normal impulse under exact
  lifetime versus 1.979763 under pair lifetime. Frames 1,200–6,799 contain
  13,082 exact false-lifetime rows and 66 cache-eligible exact-only restitution
  admissions carrying 509.278851 solved normal impulse; ten same-frame endpoint
  quiet-counter resets correlate with those admissions. Both histories still
  use exact warm-start compatibility, and exact
  has a slightly higher aggregate cache-hit rate, so broad cache degradation is
  rejected as the primary mechanism. Solver-cap saturation is a downstream
  amplifier present in both histories; reconstructed support-footprint
  classification has zero late-window flips and is rejected as unstable. Full
  queries, cost accounting, and the SR2 boundary are recorded in
  `../../Reports/2026-08-03/dense-pile-sleep-resolution-sr1.md`.

  Original work order: using `--physics-diag` and
  `tools\physics_query.bat` on the SR0 run, measure per-frame: contact feature churn
  rate for settling bodies, warm-start cache hit/miss ratio, count of rows where
  `continuesLoadedContact` is false but the pair was in contact last frame, and the
  distribution of restitution impulses applied after frame N. Test the leading
  hypothesis against at least two alternatives — residual solver non-convergence at
  the iteration cap, and support-classification instability in
  `hasRestingFootprint`/`supportsRestingPolicy` — and state which mechanism the
  evidence supports. Follow the SkullScope cost-reporting rule: print every query and
  its data-size accounting. If the evidence contradicts the feature-churn hypothesis,
  say so plainly and let SR2 design against what was actually measured.

- [x] **SR2 — Design candidate mechanisms and record the ruling.** Choose the
  half-quiet adjacent pair witness. The exact cache insertion point proves a
  same-pair loaded row with at most two constant neighbor checks and zero
  retained bytes, while exact identity remains the only warm-start authority.
  Pair continuity suppresses restitution only when both bodies have completed
  at least half the configured quiet window. The predicate selects eight dense-
  pile rows carrying 67.425641 solved normal impulse and zero rows in the
  accepted four-brick and wall traces. Global pair lifetime, a pair hash,
  manifold identity changes, and retiring-patch transfer are rejected with
  memory, replay, geometry, and baseline risks recorded in
  `../../Reports/2026-08-03/dense-pile-sleep-resolution-sr2.md`.

  Original work order: propose at least
  three candidates against the SR1 mechanism. If feature churn is confirmed, the
  design space includes: making the manifold's feature selection stable across
  sub-slop rocking so the exact-feature lookup keeps hitting; carrying a small
  per-pair lifetime token alongside the exact key so continuity survives re-selection
  at O(1) rather than by prefix scan; or seeding a re-selected feature from the
  retiring one when the manifold itself knows the two describe the same support
  patch. For each candidate state its cost per row, whether it preserves byte-exact
  wall behaviour, and whether it is reachable without a baseline transition. Record
  the chosen mechanism and why the rejected ones lose. Note that the first candidate
  overlaps the completed narrowphase coverage work — NM2 pinned feature-ID stability
  for sub-slop box and brick-hull frame pairs, and a 300-box thrown pile is a far
  harsher test of the same property, so an SR1 finding of churn is also a finding
  that NM2's coverage did not reach this regime.

  **SR3 falsifier result:** the selected half-quiet witness failed its complete
  20,000-frame oracle and is no longer an approved implementation candidate.
  It ended at 327/330 sleeping with 5,441 wake oscillations and no permanent
  all-sleep frame. Live reconstruction found only seven qualifying rows; the
  frame-24 initiating divergence has zero quiet counters. The attempted source,
  tests, and ruling changes were removed. The costed alternatives, rejected
  post-run scene fingerprint, independent review, and exact unblock condition
  are recorded in
  `../../Reports/2026-08-03/dense-pile-sleep-resolution-sr3-blocker.md`.

- [ ] **SR3 — Implement and prove both sides.** Land the chosen mechanism. Prove the
  wall fix is fully retained: four-brick reversals stay at zero with 4/4 sleeping,
  the 200-box workload retains all 211 bodies with zero popcorn cycles, and the
  approved Physics, known-issue, SkullScope, and Replay goldens are unchanged. Prove
  the sleep regression is repaired: the SR0 pile metrics reach or beat the
  pre-`63d7e92f` numbers. Add focused coverage to
  `SkullbonezTests/TestPersistentContactSolver.cpp` pinning restitution lifetime
  across a deliberate feature re-selection, and extend the sleep controller coverage
  added by the narrowphase plan for the corresponding wake path. If any golden must
  move, stop and obtain explicit owner approval before proceeding — do not fold a
  baseline transition into this phase.

- [ ] **SR4 — Gate the workload and close.** Add `box_pile_throw_300` to
  `tools/check_contact_energy_scenes.py` alongside the existing tower, vibration, and
  200-box wall scenes so the restored sleep quality has a permanent semantic gate with
  planted-failure controls, rather than living only in this plan's report. Run
  `tools\validate_physics.bat`, `tools\validate_physics_deep.bat`,
  `tools\validate_tests.bat`, `tools\validate_perf.bat`, and `tools\validate_full.bat`.
  Audit every touched source-bearing file. Obtain an independent read-only review that
  specifically checks that the wall fix is intact, that no candidate re-admitted
  restitution on continuing contacts, and that the new gate can actually fail.

## Dependencies And Decisions

- Runs first in the remaining campaign. Render Graph Transition Coverage closed
  at RG4 and was deleted under ledger rule 4; its permanent closure evidence is
  `../../Reports/2026-08-03/render-graph-transition-coverage-closure.md`.
  Comment Vocabulary Audit and Source Modernization Sweep follow this plan, and
  neither has a dependency on it.
- `194cbf82` is authoritative and stays. This plan changes how contact **lifetime
  continuity** is established, never whether continuing contacts are suppressed.
- The pair-prefix scan is a known-rejected mechanism, not an unexplored option. SR2
  must clear that bar explicitly rather than rediscovering it.
- `box_pile_throw_300.scene.json` has no committed baseline today. SR0 may use it
  freely as a diagnostic; SR4 is what makes its behaviour a permanent contract.
- Physics baselines remain owner-controlled. A passing determinism check, a plan
  allowance, or an agent-authored report is not approval.
- If SR1 finds the regression is not repairable without a golden transition, the
  correct outcome is to stop at SR2 with a costed owner decision packet, not to
  implement a change that forces a baseline the owner has not approved.
- **Current blocker:** SR3's reviewed candidate failed, and every remaining
  policy-shaped route needs owner authority for a new hysteresis/tuning rule,
  retained pair state, or geometry/replay transition. The verified count remains
  3/5. SR4 depends on SR3; the two later campaign plans do not. Unblock only when
  the Physics contact/sleep owner selects an option and transition scope from
  `../../Reports/2026-08-03/dense-pile-sleep-resolution-sr3-blocker.md`, or parks
  this plan under inventory rule 9.

## Acceptance

The plan closes when dense-pile sleep resolution meets or beats the pre-`63d7e92f`
measurements from SR0, the four-brick and 200-box wall benefits from `194cbf82` are
fully retained with unchanged goldens, the repair mechanism is recorded with its
rejected alternatives, focused tests pin restitution lifetime across feature
re-selection, `box_pile_throw_300` is a gated semantic workload with working
planted-failure controls, all mapped gates pass, and independent review confirms no
continuing contact re-admitted restitution.

## Validation

- SR0 three-point historical comparison across pile, four-brick, and 200-box workloads
- SkullScope feature-churn, warm-start hit-rate, and restitution-after-settle queries
  with full cost accounting
- `tools\validate_physics.bat` — byte-exact, no baseline movement
- `tools\validate_physics_deep.bat` — known-issue and SkullScope goldens unchanged
- `tools\validate_tests.bat`
- `tools\validate_perf.bat`
- `tools\validate_full.bat` at the closing gate
- `python tools\check_contact_energy_scenes.py` including the new pile gate and its
  planted controls
- Touched-source comment audit
- Independent read-only review

## Related

- `../../../SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `../../../SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- `../../../SkullbonezSource/Physics/ObjectContactManifold.cpp`
- `../../../SkullbonezData/scenes/box_pile_throw_300.scene.json`
- `../../../tools/check_contact_energy_scenes.py`
- `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-closure.md`
- `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es5.md`
- `../../Reports/2026-08-02/narrowphase-manifold-sleep-coverage-closure.md`
- `../../Reports/2026-08-03/render-graph-transition-coverage-closure.md`
