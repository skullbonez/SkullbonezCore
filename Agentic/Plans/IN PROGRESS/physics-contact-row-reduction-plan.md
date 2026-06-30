# Physics Contact Row Reduction Plan

Source request: reduce solver work for dense walls of boxes by inspecting whether stable box contacts need all manifold points every frame, then applying Havok/Bullet-style manifold caching/reduction where it is safe.

## Current Status

- Status: Not started.
- Scope: object/object contact row reduction for dense box scenes, especially the 200-body wall benchmark on `codex/physics-wall-cpu-profile-200-bodies`.
- Impact area: physics narrowphase, box/box manifold generation, persistent contact cache, contact solver row loop, sleep support policy, diagnostics, config, and performance validation.
- Primary target: reduce stable resting box/box contact rows without losing impact response, stack stability, warm-start determinism, or byte-exact repeatability for accepted baselines.
- Non-goals: do not replace the Catto-style PGS solver wholesale, do not rewrite broadphase again, do not change terrain contacts in the first pass, and do not reduce rows for high-impact or newly colliding pairs until measured.

## Baseline Facts

- `ObjectContactManifold` can carry up to four points for box/box face contacts.
- `PersistentContactSolver` currently turns every manifold point into one persistent solver row.
- Each row solves normal plus two tangent directions on every PGS iteration.
- Feature IDs and `m_persistentContactCache` already exist, so row reduction can prefer cached/stable contacts instead of picking arbitrary clipped vertices.
- `supportsRestingPolicy`, `normalCoupledFriction`, `manifoldPointCount`, `persistentContactCounts`, and `persistentRestingContactCounts` already provide useful policy hooks.

## The Six Steps

### Step 1 - Cached Two-Point Cap For Resting Box/Box Face Manifolds

Goal: cap stable low-impact box/box face manifolds to two rows while preserving a useful support footprint.

Implementation shape:
- Detect box/box face manifolds with `pointCount > 2`.
- Require low relative speed and non-impact behavior before reducing.
- Prefer points whose feature IDs hit the previous persistent-contact cache.
- If cache evidence is insufficient, keep the deepest point plus the farthest point in the contact patch.
- Keep full four-point manifolds for fresh impacts, high angular speed, high relative speed, and ambiguous support.

Validation focus:
- Row count reduction in the wall benchmark.
- No visible collapse or jitter increase in resting box stacks.
- Warm-start hit rate does not crater.
- `tools\validate_physics.bat` and `tools\validate_perf.bat` before commit.

### Step 2 - Cache-Prioritized Manifold Reduction

Goal: make reduction use the previous frame's solved contacts as the first-class signal.

Implementation shape:
- Apply reduction after `BuildObjectContactManifold` and before appending `PersistentContact` rows, where the solver can access `m_persistentContactCache`.
- Score each candidate point by cache hit, cached normal impulse, penetration, support orientation, and deterministic feature ID.
- Preserve deterministic ordering after selection so byte-exact physics remains explainable.
- Record diagnostics for original manifold count, reduced count, selected feature IDs, and reduction reason.

Validation focus:
- Stable feature IDs remain stable across frames.
- Cache hit count stays flat or improves after the first few frames.
- SkullScope or bounded profiler diagnostics can explain selected/rejected rows without dumping raw traces into handoff text.

### Step 3 - Impulse-Based Row Retirement

Goal: retire rows that consistently carry no support impulse after the pair has settled.

Implementation shape:
- Track, per pair+feature, whether `accN` and tangent impulses remain below tolerance across one or more frames.
- For resting-policy contacts, prefer storing only the top rows per pair by solved normal impulse.
- Keep deterministic tie-breaks by feature ID.
- Do not retire rows for new impacts, fixed-release contact checks, or pairs whose relative velocity indicates renewed motion.

Validation focus:
- Settled wall and stack rows trend down after the first few frames.
- Fixed-contact release behavior still sees meaningful accumulated impulse.
- No delayed tunneling or late separation from retiring a row too aggressively.

### Step 4 - Per-Body Resting Row Budget

Goal: stop dense contact neighborhoods from giving one body unbounded resting rows.

Implementation shape:
- Add a deterministic cap such as `persistent_contact_max_resting_rows_per_body`.
- Apply only to resting-policy object/object rows.
- Rank rows by cache hit, normal impulse, upward support contribution, penetration, and feature ID.
- Keep non-resting/impact rows outside this budget.
- Update `persistentContactCounts` and `persistentRestingContactCounts` from the reduced row set so friction and sleep policy see the final workload.

Validation focus:
- Bodies in the 200-wall scene do not accumulate pathological row counts.
- Sleep support edges still form credible islands.
- Stacks do not lose necessary side constraints when lateral support matters.

### Step 5 - Friction Row Reduction

Goal: reduce the tangent-solve multiplier without necessarily dropping every normal support row.

Implementation shape:
- For stable box/box face contacts, solve normal rows at the selected count but run tangent friction on fewer representative rows.
- Candidate policies: strongest cached row only, centroid/deepest row, or two spread rows for broad support patches.
- Keep full friction for high relative tangential velocity, impacts, and contacts without stable cache evidence.
- Keep `normalCoupledFriction` behavior explicit so non-resting contacts remain conservative.

Validation focus:
- `SolveRows` time falls more than row-count-only reduction would predict.
- Resting walls do not begin slow sliding.
- Rolling/sliding gameplay cases still feel plausible.

### Step 6 - State Ladder For Contact Detail

Goal: make contact detail adaptive rather than permanently full or permanently reduced.

Implementation shape:
- Add a pair/contact state ladder:
  - Full detail for new, impact, or high-speed contact.
  - Reduced detail after a small stable age threshold.
  - Minimal detail or skip once both bodies are sleep-eligible/sleeping and already represented by sleep island state.
- Define promotion rules that immediately restore detail on velocity spikes, angular motion, contact normal changes, penetration growth, or cache churn.
- Prefer config-backed thresholds so tuning changes are explicit.

Validation focus:
- No quality drop during the first collision frame.
- Resting workload declines over time.
- Waking a sleeping or near-sleeping pile restores enough detail before visible motion diverges.

## Suggested Implementation Order

1. Instrument row counts per pair/body for the 200-wall scene without changing behavior.
2. Implement Step 1 and Step 2 together as the smallest likely win.
3. Measure before/after with three stable Profile samples, matching the broadphase workflow.
4. Add Step 3 only if cache-selected rows still leave obvious zero-impulse waste.
5. Add Step 4 only if dense neighborhoods still over-budget individual bodies.
6. Treat Step 5 and Step 6 as higher-risk follow-up slices after the first row cap proves stable.

## Likely Files To Inspect

- `SkullbonezSource/Physics/ObjectContactManifold.h`
- `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- `SkullbonezSource/Physics/PersistentContactSolver.h`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Core/Config.*`
- `SkullbonezData/engine.cfg`
- `Agentic/Reference/physics-query-reference.md`
- `Agentic/Skills/skore-skullscope/skill.md`

## Measurement Plan

- Capture current row-count, cache-hit, solver-iteration, and `SolveRows` evidence before behavior changes.
- Use the 200-body wall scene from the broadphase profiling work for repeatable Profile samples.
- Run at least three before and three after samples before claiming a performance win.
- Compare:
  - `Frame/Physics/Narrowphase/PersistentContacts`
  - `Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds/AddRows`
  - `Frame/Physics/Narrowphase/PersistentContacts/SolveRows`
  - solver row count
  - cache hits/misses
  - warm-started rows
  - position-correction rows/max
- Use SkullScope bounded queries if row distribution or pair-level behavior needs investigation.

## Validation Plan

- This documentation-only plan requires no repository validation.
- Any source implementation touching physics/collision/solver files requires `tools\validate_physics.bat`.
- Any performance-sensitive hot-path implementation requires `tools\validate_perf.bat`.
- Changes to `Config*` or `SkullbonezData/engine.cfg` physics defaults require `tools\validate_physics.bat`.
- Before PR-bound handoff, run the comment-style audit for touched source-bearing files.
- For each implementation slice, preserve exact before/after sample commands and summary metrics.

## Risks

- Reducing the wrong two points can remove torque support and make boxes rock or tip.
- Cache-first selection can preserve stale points if feature IDs are stable but contact geometry drifted.
- Per-body budgets can accidentally remove lateral bracing in a compressed wall.
- Friction reduction can make resting stacks slide even when normal support looks fine.
- State-ladder thresholds can introduce nondeterministic-looking behavior if promotion/demotion rules are not entirely deterministic.
- Any accepted behavior change may require an intentional physics baseline update from final Debug artifacts.

## Stop Conditions

- Stop if a reduced manifold changes first-impact behavior before the pair is stable.
- Stop if byte-exact determinism fails for unchanged baselines.
- Stop if the wall benchmark improves but simple box stacks visibly degrade.
- Stop before changing global solver defaults without before/after samples and validation evidence.
