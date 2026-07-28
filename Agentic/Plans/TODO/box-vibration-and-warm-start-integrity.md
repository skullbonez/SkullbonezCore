# Box Vibration And Warm-Start Integrity

Date: 2026-07-29
Owner: skullbonez
State: Not started (investigation complete, regression test preserved)
Ledger tasks: 7 (BV0-BV6)
Branch: nightrunner-29th-JUL-26
PR: TBD

## Goal

Two owner-stated goals, in priority order:

1. **Stop the boxes vibrating.** Slowly toppling brick columns in
   `prediction_ragdoll_wall_200` jitter instead of settling.
2. **Make solver warm starting honest.** Retire the fabricated terrain support
   seed and the contact-identity churn that together mean the Catto warm-start
   cache is not doing the job its citations claim.

These are one plan because they share a root: the solver's per-frame support
state is not surviving from one frame to the next, and two separate mechanisms
paper over that instead of fixing it.

## Non-Goals

- Retuning `contact_restitution_threshold` globally. Terrain restitution and its
  rest/rolling policy are what bring rolling balls to a stop; the owner has
  ruled that path stays as-is.
- Rewriting the solver into soft constraints / TGS / split impulse. That is a
  much larger change and is not required by any evidence here.
- Fixing `unsupported_sleep` diagnostic noise (see Deferred Observations).

## Measurement Instruments — Read This First

**`prediction_ragdoll_wall_200` is a bad instrument for A/B comparison.** It is
200 bodies plus a convex-hull ragdoll struck at 170 u/s. Any behavioral change
diverges it chaotically, so bounce counts between two runs are not a controlled
experiment. This cost real time during the 2026-07-27/28 investigation.

Use it only for *structural rates* measured within one run: cache miss rate,
axis-type switch counts, cold-row fractions, solver iteration counts. Those
survive divergence; end-state energy and per-body bounce counts do not.

**`box_only_rest.scene.json`** (15 boxes, no impact, 1800 frames) is the
controlled instrument. Runs are directly comparable.

**BV0 must add a purpose-built vibration fixture** because neither existing scene
isolates the symptom: `box_only_rest` is already healthy, and the wall is
chaotic. See BV0.

## Measurement Provenance — Line Numbers Have Moved

**All measurements and every source line number in this plan were taken at
`main` tip `0768593d` on 2026-07-27/28. Main is now `90e4d52f` (PR #137).**

Between those tips, in the files this plan touches:

| File | Lines changed |
|---|---:|
| `Physics/ObjectContactManifold.cpp` | 90 |
| `Physics/PersistentContactSolver.cpp` | 61 |
| `SkullbonezTests/TestObjectContactManifold.cpp` | 26 |

The changes are API modernization (const `RotationMatrix`, explicit
dot-product API) plus commit `20e75dae` "physics: guard persistent-contact key
capacity", which edited the persistent-contact key schema — the exact `makeKey`
body-mask area this plan's feature-ID analysis depends on. That commit's closure
report states physics stayed byte-exact, so the *findings* below should still
hold, but **re-resolve every line number against the current tip before acting
on it**, and re-confirm the feature-ID bit layout against the new key schema
before trusting the decoded values.

Inventory Rule 7 applies: these numbers are dated and scoped, and must not be
reused as current evidence without re-measurement.

## Problem And Evidence

All measurements 2026-07-27/28, `main` tip `0768593d`, Debug build, via
SkullScope traces of `prediction_ragdoll_wall_200` (1200 frames, dt 1/120)
unless stated otherwise.

### The symptom is real and measurable

`prediction_wall_brick_r02_c05` (body 46, column 5 — away from the impact at
columns 9-10), frames 430-461:

- `pos_y` oscillates 7.33823-7.34340, an amplitude of ~0.005
- that amplitude equals `persistent_contact_slop = 0.005` exactly
- `vel_y` flips sign nearly every frame, peaking ±0.25
- 77 downward-to-upward flips across frames 300-1199

Across all 200 bricks: 6,224 flips, worst brick 77. The affected bricks cluster
in columns 2-6 and 14-17 — the slow-topple region, not the impact zone.

### The solver never converges in this scene

`solver_stats` frames 200-1199 (1000 frames):

| Measurement | Value |
|---|---|
| Frames hitting the 12-iteration cap | **1000 of 1000** (`min_iters = 12`) |
| Warm-start cache miss rate | **17.71%** (78,594 / 443,784 lookups) |
| Box-face rows starting cold, frames 400-500 | **23.0%** (8,761 / 38,054) |
| Position-correction rows | 3.87% |

The `iterImpulseSq < 1e-6` early-out never fires once.

### Root cause of the vibration: restitution on non-impact contacts

`contact_restitution_threshold` defaults to `2.0` and is not present in
`engine.cfg`. In a dense pile, a contact row's pre-solve closing speed is
dominated by the rotational `omega x r` term of the contact point, not by the two
bodies genuinely approaching. Measured rows on body 46 carried closing speeds of
1.1, 2.7, 3.3, and 3.8 — several above the threshold. Each one gets a bias of
`-e * vn`, which at brick restitution `0.08` and `vn = 3.8` is ~0.30 u/s of
separating velocity. That pushes the contact apart, it is lost, it re-forms next
frame, and its warm-start cache entry dies on the way.

Decisive experiment — `contact_restitution_threshold` raised to 1e6 so the branch
can never fire (config-only, reverted afterwards):

| | SAT fix only | restitution branch disabled |
|---|---:|---:|
| Brick bounce flips | 6,612 | **596** (-91%) |
| Bricks affected | 198 | 135 |
| Cache miss rate | 12.84% | **2.39%** |
| Solver min iterations | 12 (never converged) | **10** (early-out fires) |

Two corroborations:

- **More iterations made it worse.** At 48 iterations the solver still never
  converged once (`frames_converged_early = 0`) and bouncing rose to 9,515
  flips. A solver that diverges harder with more iterations is being driven by
  an energy source, not merely truncated.
- **A controlled stack is healthy.** `box_only_rest` averages 2.24 solver
  iterations with the early-out firing, 4.62% miss rate, and 23 oscillation
  flips across 1200 frames. There is no general stacking defect.

### Contact identity churn: warm starting cannot work

Feature IDs encode the winning SAT axis type. `EncodeBoxFaceFeature` folds
`referenceIsA` into both `refCode` and `incCode`; `EncodeBoxEdgeFeature` uses a
different feature *kind* entirely. So any change to how SAT resolved a pair
re-keys every row for that pair at once, and every warm-start lookup misses.

Decoded from the trace, the same pair alternating between two feature families:

- `36480` -> `referenceIsA=true`, refFace 3, incFace 2
- `43200` -> `referenceIsA=false`, refFace 2, incFace 3

Same physical contact, reference and incident bodies swapped. Measured per 100
frames on the wall:

| Switch class | Count |
|---|---:|
| Face-A / face-B reference swaps | 1,307 across 222 pairs (5.68% of 23,005 pair-frames) |
| Face / edge switches | 3,531 (1,780 edge->face, 1,751 face->edge) |

Cold-row attribution, frames 400-500: 2,941 of 8,761 cold rows (33.6%) traced
directly to reference swaps.

`AcceptSatAxis` and `AcceptPolyAxis` both compare with a stateless
`tieEpsilon = 1.0e-4f`. That is 50x smaller than the ~0.005 rocking amplitude,
so a rocking stack flips straight through it.

### The terrain warm-start seed is the "ugly hack"

`PersistentContactSolver.cpp:1037` fabricates a support impulse for terrain rows:

```
warmStartTotal = mass * gravityMagnitude * |normal.y| * dt * supportSeedScale
warmStartPerContact = warmStartTotal / pointCount
```

with `TERRAIN_RESTING_SUPPORT_SEED_SCALE = 1.0f` and
`TERRAIN_SHORELINE_SUPPORT_SEED_SCALE = 0.35f`. Defects:

1. **It is a floor, not a seed.** `:1311` clamps `accN` *up* to it after the
   cache read, so it can never decay. A terrain contact that should legitimately
   carry less than full body weight — partly buoyant, leaning on a wall, being
   lifted — is pushed up with full weight anyway.
2. **It contaminates the cache.** `:1741` stores `accN` after the floor was
   applied, and the floor is reapplied on read. The cached "solved" impulse for
   a resting terrain row is structurally incapable of being below the fabricated
   value. Real solution and fudge are indistinguishable.
3. **Fabricated normal force buys real friction.** Terrain friction is
   `mu * max(accN, terrainWarmStart)` at `:1248`, `:1279`, and `:1414` — a floor
   on the friction budget that a body never earned.
4. **Three friction models are live at once.** Terrain `mu * max(accN, seed)`;
   object with resting footprint constant `mu * contactMass * g * dt`; object
   without `mu * accN`. This is audit finding #6 in
   `Agentic/Audits/physics-solver-catto-reference-audit.md:152`, still open.
5. **`|normal.y|` hardcodes vertical -Y gravity**, documented as a hazard at
   `:1030` but not fixed.
6. **It compensates for non-convergence.** The T3 evidence in
   `Agentic/Reports/2026-07-27/terrain-contact-seed-t3.md` measures its benefit
   in a *deliberately one-iteration* fixture, and at full strength the top box of
   a three-box stack gains `+0.011951` upward velocity. The seed over-pushes.

The standing MASTER-PLAN entry parks this as "working, honestly documented, and
baseline-entangled; undertake only with a concrete perf or stacking-stability
motivation." **The vibration is that motivation.**

### Scene facts worth knowing

`prediction_ragdoll_wall_200` is 10 rows x 20 columns. Brick half-extent y is
`1.45` (height 2.90) but row spacing is `2.98`, so **the bricks are authored with
a 0.08 vertical gap** — 16x the contact slop. They are authored `sleeping: true`,
sit motionless at their authored `pos_y` until the ball arrives, and wake at
frame 177. Config: gravity `-50`, slop `0.005`, baumgarte `0.2`, position
correction `0.35`, iterations `12`, contact epsilon `0.05`, brick restitution
`0.08`.

## Hypotheses That Were Tested And Failed

Recorded so nobody spends the time again.

1. **Position-correction overshoot.** `PersistentContactSolver.cpp:1639` applies
   correction once per row against a stale build-time `c.penetration`, with no
   per-pair guard, so a 4-point manifold removes ~140% of the overlap. This is a
   **real latent defect** and is still worth fixing — but it is not this bug. It
   fires on only 3.87% of rows, and every contact row on the vibrating brick had
   `penetration = 0.0`, so the pass never ran for it. Kept as BV5.
2. **Contact-identity churn as the bounce cause.** Fixing it (see WIP below) cut
   face/edge switches 3,531 -> 83 and the miss rate 17.71% -> 12.84%, and did
   **not** reduce bouncing. The churn is largely a *symptom* of restitution
   pushing contacts apart, not the cause.
3. **Solver iteration starvation.** 48 iterations made it worse, not better.

## Recovered Investigation Artifact

The original `stash@{0}` on `main` was named *"WIP: SAT axis-type hysteresis +
object-only restitution suppression + 3 refreshed physics baselines"*. It
contained six files:

| File | Change | State |
|---|---|---|
| `Physics/ObjectContactManifold.cpp` | `SatChallengerMargin` hysteresis, box + hull paths | Built, tested, gates pass |
| `Physics/PersistentContactSolver.cpp` | Restitution suppressed on object/object contacts already in the cache; terrain untouched | **Compiles-unverified, never validated** |
| `Tests/TestObjectContactManifold.cpp` | Contact-identity regression test | Passes; fails 338 assertions without the fix |
| `TestOutput/baselines/physics_regression_varied.csv` | Refreshed | For the SAT change **only** |
| `TestOutput/baselines/physics_known_issue_signatures.json` | Refreshed | For the SAT change **only** |
| `TestOutput/baselines/physics_query_varied.json` | Refreshed | For the SAT change **only** |

**The stash did not apply cleanly to current main.** Verified 2026-07-29:

```
git stash show -p "stash@{0}" > wip.patch
git apply --check --include='SkullbonezSource/*' --include='SkullbonezTests/*' wip.patch
  -> error: patch failed: SkullbonezSource/Physics/ObjectContactManifold.cpp:580
```

It was taken against `0768593d`; both target files have since moved. It was not
force-merged.

**The three baselines are also stale twice over** — they were generated for the
SAT change only (not the restitution change), from a binary at the old tip.

Completed recovery and remaining handling:

1. The reusable contact-identity regression test is preserved on
   `origin/codex/contact-identity-regression-29th-jul-26` at commit `27906417`.
   That branch is intentionally red until BV2 supplies the SAT identity fix; it
   is a recovery artifact, not a merge-ready implementation.
2. Re-derive the two source changes by hand against the current tip. Both are
   small: the SAT change is ~6 functional lines across two call sites, and the
   restitution change is one condition plus an `else` restructure.
3. Cherry-pick or reapply the preserved test during BV2 after re-confirming the
   feature-ID layout against the post-plan-7 key schema. It carries the A/B
   evidence (fails 338 assertions without the SAT fix).
4. The stashed baselines were dropped. Regenerate once, at the end, from the
   final Debug binary.
5. The original stash was dropped only after the preservation branch was
   pushed.

Note the restitution change in the stash is **compiles-unverified**: the last
build attempt failed on a file lock from a background `validate_full`, and it
was never rebuilt. It also had a structural bug during authoring (a broken
`else if` chain that let terrain rows fall into the object branch) which was
fixed but never compiled. Treat it as a sketch.

## Superseded Prior Ruling — Read Before Starting BV3

MASTER-PLAN carries a binding owner ruling dated 2026-07-27 (Round 5 open
decisions, "Plan 13 T3 — terrain contact seed"):

> Replacement is rejected on cost: it would require a full physics and replay
> baseline transition for **no visible gameplay gain**. A principled replacement
> is explicitly not a deferred follow-up row.

That ruling stands on its stated rationale, and its rationale no longer holds.
The owner reported visible box vibration on 2026-07-27 and directed on
2026-07-29 that the warm start stop being "an ugly hack". There is now a visible
gameplay motivation, which is the exact condition the standing MASTER-PLAN entry
named for revisiting this work.

BV3 therefore supersedes the 2026-07-27 ruling on owner direction. It does not
overturn it silently: if the owner would rather keep the seed and take only
BV1/BV2, BV3 can be dropped without affecting the vibration fix, because the
vibration is caused by object/object restitution and not by the terrain seed.

## Design Constraints

- **Terrain restitution is untouchable.** Owner ruling 2026-07-28: terrain
  restitution and its rest/rolling policy are what stop rolling balls. Only the
  object/object branch may change.
- **This plan carries a bounded-divergence allowance.** Unlike the Fresh-Read
  campaign plans, BV1-BV3 deliberately change contact response and *will* move
  physics baselines. Each refresh must follow the Danger Zone rules: regenerate
  only from the final Debug executable and committed config/scenes, then rerun
  the matching gate.
- **Determinism is non-negotiable.** The two deterministic runs inside a single
  `physics_regression_varied.csv` output must remain byte-identical to each
  other. This was verified to hold for the SAT change.
- **No new authority-free aggregates, extraction scars, or migration nouns.**
  Standard AGENTS.md review questions apply.

## Tasks

- [ ] **BV0** — Controlled vibration fixture and T0 harness. See BV0 below.
- [ ] **BV1** — Suppress restitution on persistent object/object contacts.
- [ ] **BV2** — Stabilize SAT axis-type selection.
- [ ] **BV3** — Retire the terrain warm-start seed. Droppable; supersedes a
      prior owner ruling.
- [ ] **BV4** — Re-measure convergence.
- [ ] **BV5** — Position-correction divisor.
- [ ] **BV6** — Independent review, comment audit, closure report.

### BV0 — Build a controlled vibration fixture and record T0

Neither existing scene isolates the symptom. Author a scene that reproduces
box-on-box vibration deterministically without chaos: a small number of directly
stacked boxes with a modest disturbance, no high-energy projectile, no ragdoll.
Target a scene where the pre-change build shows sustained `vel_y` sign flipping
and the solver fails its early-out.

Also record the T0 byte-exact harness: `validate_physics`,
`validate_physics_deep`, `validate_tests`, and the focused oracle list.

Acceptance: a committed scene, a documented vibration metric with its query, and
a T0 report showing the metric is non-zero before any fix.

### BV1 — Suppress restitution on persistent object/object contacts

The bounce fix. In the object branch of the bias computation
(`PersistentContactSolver.cpp`, currently `:1200-1226`), apply restitution only
when the contact was *not* already carrying load last frame. `hasCachedImpulse(
bodyA, bodyB, featureId )` already exists at `:283` and answers exactly that
without reordering the cache lookup.

A suppressed row must fall through to the Baumgarte penetration branch rather
than being left with no bias.

Note the reach limit: the cache only stores rows with `supportsRestingPolicy`
(`:1729`), so edge/corner object contacts never cache and keep full restitution.
That is arguably correct — a corner impact is impact-like — but record it.

Acceptance: BV0 metric drops substantially; terrain rows byte-unchanged in a
terrain-only scene; a focused test pins that a fresh contact still bounces and a
persistent one does not.

### BV2 — Stabilize SAT axis-type selection

The warm-start integrity fix. Give `AcceptSatAxis` and `AcceptPolyAxis` a
hysteresis margin that a challenger must beat *only when it would change the
winning axis type*; within one axis type the comparison stays strict. The WIP
implementation uses `max(1e-4, contactSkin * 0.25)`.

The `0.25` fraction is empirical, not derived. BV2 should either justify it
against the BV0 fixture or replace it with something principled.

Acceptance: face/edge and reference-swap counts near zero on the wall's
structural rates; the regression test in the stash (or its successor) fails
without the change; determinism holds.

### BV3 — Retire the terrain warm-start seed

The "ugly hack" removal, and the reason BV2 comes first: the seed exists to
paper over cold rows at the base of a stack. Once warm starting actually
survives frame to frame, test whether the seed is still load-bearing.

Staged:

1. Decouple friction from the seed. Remove `max(accN, terrainWarmStart)` from
   all three sites and give terrain rows an honest bound. This closes audit
   finding #6 and has an independent justification.
2. Stop flooring `accN` at `:1311`; apply the seed only on a cache miss.
3. Relax cache admission at `:1729` so touching terrain rows cache, not just
   `supportsRestingPolicy` ones, so shoreline contacts warm-start from a real
   solved impulse instead of `0.35 x weight`.
4. If a first-touch seed is still needed, derive it from the row's own
   `normalMass` and the real gravity vector rather than
   `mass * g * |normal.y| / pointCount`. Terrain rows must also start
   incrementing `m_persistentContactCounts` so multi-support bodies stop
   double-counting.

Acceptance: `TERRAIN_SHORELINE_SUPPORT_SEED_SCALE` deleted or justified with
fresh evidence; the 58 assertions in the T3 fixture updated rather than deleted;
no shoreline bobbing regression.

### BV4 — Re-measure convergence

With BV1-BV3 landed, re-run the wall and the BV0 fixture. If the solver now
reaches its early-out, record it. If it still never converges, that is a new
finding and gets its own plan — do not raise the iteration count to hide it
(48 iterations made things worse).

### BV5 — Position-correction divisor

Fix the latent defect found and set aside: divide the per-row correction by
`c.manifoldPointCount` (already populated for both terrain and object rows at
`:934` and `:1065`), or otherwise guard against N rows each removing the full
overlap against a stale penetration. The terrain restitution path already
divides by point count at `:1197`; position correction divides nowhere.

### BV6 — Independent review, comment audit, closure report

Independent ownership review answering the five AGENTS.md review questions.
Closure report under `Agentic/Reports/<date>/` with the final measurements and
every baseline that moved.

Whole-file comment audit against `Agentic/Reference/comment-style-guide.md`.
Expected scope — reconcile against a fresh `git ls-files` inventory at closure,
since the task list may grow:

- [ ] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.h`
- [ ] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [ ] `SkullbonezTests/TestObjectContactManifold.cpp`
- [ ] `SkullbonezTests/TestPersistentContactSolver.cpp`
- [ ] any new scene or fixture added by BV0

Comments that currently assert the terrain seed's behavior must be corrected in
the same commit that changes it, not left describing the previous owner.

## Validation

Mapped gates, cumulative:

| Scope | Gate |
|---|---|
| `ObjectContactManifold*`, `PersistentContactSolver*` | `validate_physics`, then `validate_physics_deep` |
| `SkullbonezTests/*` | `validate_tests` |
| New scene under `SkullbonezData/scenes/` | `validate_full` |
| Any baseline refresh | regenerate from final Debug binary, rerun the matching gate |
| Final | `validate_full` |

Known baseline impact from the SAT change alone (measured): only
`physics_regression_varied.csv` (14,784 of 44,401 lines, all small 4th-decimal
perturbations), `physics_known_issue_signatures.json` (the
`stacking_stability_watch` row only), and `physics_query_varied.json`. Bullet
sweeps, shooting volley, and three-body chaos stayed byte-exact.

Two observations to carry forward when judging a baseline refresh:

- Stacking A/B on the known-issue scene was neutral: settles at frame 590 with
  the SAT fix vs 564 without, 8 oscillations vs 9.
- The bench scene ends with 5.7x residual energy (3,465 vs 608 at frame 1199).
  Verified as chaotic divergence, not injection: the two runs are bit-identical
  through frame 100, diverge at ~150 where box contacts begin, and both dissipate
  monotonically. The end-state gap is one ball collision that happens in one run
  and not the other.

## Deferred Observations

- **`unsupported_sleep` diagnostic noise.** 35,375 high-severity events, all 200
  bricks every frame from 0 to ~177. Not a physics bug: scene-authored
  `sleeping: true` bodies have `sleep_supported = 0` and a degenerate self-island
  (`island_id = body_id + 1`) because they never went through the runtime sleep
  path. It is a false positive on authored sleep state that swamps the event
  channel. Worth its own small plan.
- **Authored 0.08 brick gap** in `prediction_ragdoll_wall_200` (spacing 2.98 vs
  height 2.90), 16x the contact slop. Bricks free-fall it on wake. Probably
  intentional anti-interpenetration authoring, but it means the wall never starts
  in contact.

## SkullScope Artifacts

Investigation traces are regenerable and were left in `Debug\` (~3.9 GB):
`ragdoll_wall`, `wall_fixed`, `wall_fixed2`, `wall_iter48`, `wall_norest`,
`rest_fixon`, `bench_fixon`, `bench_fixoff`, plus `.sqlite` caches. Delete
freely.

Trace command shape:

```
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene <scene> --physics-diag <out.ndjson> --frames 1200 --vsync off
```

Useful queries are recorded in the 2026-07-27/28 session; the load-bearing ones
are `solver_stats` aggregates, per-pair feature-id transition counts via
`lag()` window functions over `contacts`, and `vel_y` sign-flip counts over
`bodies`.

## Related

- `Agentic/Reports/2026-07-27/terrain-legacy-contact-seed-remediation-closure.md`
  — ratified the terrain seed without changing behavior; BV3 supersedes it.
- `Agentic/Audits/physics-solver-catto-reference-audit.md` — finding #6, the
  three live friction models.
- `Agentic/Reference/physics-query-reference.md` — SkullScope workflow.
- `Agentic/Reports/2026-07-29/contact-solve-phase-ownership-closure.md` — also edits
  `PersistentContactSolver.cpp`; that plan is strictly byte-exact and this one is
  not, so they must not run concurrently.
