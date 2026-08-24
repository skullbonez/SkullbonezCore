# Deterministic Collision Modes And Ragdoll Unification

Date: 2026-08-22
Status: Active by explicit owner direction. 3/10 phases complete; FP3 active after FP2 closed under motion-eligibility policy version 2.
Impact area: collider local-offset correctness, deterministic Discrete simulation, automatic Swept TOI promotion, linear and angular motion eligibility, ragdoll point joints, joint compliance, shared constraint iteration, late speculative ragdoll contacts, physics baselines, determinism tests, and A/B performance evidence
Owner: Physics contact and joint solver
Priority: Binding first plan; execute FP2-FP9 in strict internal order. This position allocates scarce slots
and orders fan-in; it is not a Runtime or UI predecessor.
Commit name: `RAGDOLL_PHYSICS`

## Owner Direction

The owner explicitly activated this plan on 2026-08-22, assigned the
`RAGDOLL_PHYSICS` commit token, placed it ahead of every existing master-plan
item, and approved FP0 as the first behavior transition. Later phases remain
strictly gated by the acceptance boundary immediately before them.

On 2026-08-23 the owner replaced FP1's thickness-scaled eligibility rule with
motion-eligibility policy version 2. Linear centre travel and angular tip travel
use absolute distances travelled during one Physics tick: `0.1` metres promotes
and `0.075` metres demotes. Promotion equality still promotes, demotion equality
still demotes, and neither decision depends on collider thickness. This owner
direction activates FP2 without changing the authoritative 200-box workload.

Active Physics phases have standing authority to update every golden they
govern without a per-transition prompt, exact phrase, or separate pre-approval.
For this plan that includes Physics, SkullScope, replay, visual, causal, and
performance goldens whose changes derive from the FP behavior. It does not
extend Physics authority to a non-Physics change or permit an unexplained
refresh merely to close a gate.

Every changed golden uses an exact candidate digest and a new append-only bundle
at `Agentic/Plans/Artifacts/ragdoll-physics-unification/<phase>/golden-transitions/<transition-id>/`.
Before replacement, preserve both the exact prior behavior and the new producer:
all executables needed for comparison, their non-system runtime DLLs, and a
schema-1 `manifest.json` binding the owning phase, source commits, old/new golden
hashes, artifact paths/sizes/hashes, dependency-scan commands, and launch
commands. Never overwrite an older bundle. The new producer becomes the prior
behavior executable for the next transition. Source, tests, golden changes, and
the complete bundle land atomically through the automated writer documented in
`Agentic/Plans/Artifacts/README.md`.

The 2026-08-23 FP1 launch audit proved why this is required: a lone retained
executable is not a runnable artifact. FP0/FP1 import `WinPixEventRuntime.dll`,
but their original artifact directories do not contain it. Future transitions
therefore fail closed if any declared executable/DLL hash is missing or differs.

This is a major Physics-system transition, not only a ragdoll feature. Its
non-negotiable order is:

1. Close the current Physics correctness bugs that would invalidate collision,
   determinism, replay, or performance evidence.
2. Establish deterministic, measured per-body collision-path eligibility.
3. Make Discrete the ordinary simulation path while automatically promoting
   sufficiently fast bodies to the retained Swept TOI path for that tick.
4. Prove Discrete correctness and byte-exact determinism before predictive
   ragdoll work begins.
5. Capture an isolated A/B measurement of the Discrete performance improvement.
6. Complete the existing ragdoll joint-solver stages.
7. Add predictive/speculative ragdoll contacts only as a late phase.
8. Prove predictive determinism and capture its isolated A/B performance cost.

Projectiles are not explicitly tagged, authored, or otherwise marked as
continuous. The camera launcher projectile, the fast ball in the 200-box wall
scene, and any future sufficiently fast body begin from the ordinary Discrete
policy and automatically receive Swept TOI when the deterministic per-tick
motion test crosses the promotion threshold. A scene-specific name, gameplay
category, or manual `ContinuousSwept` flag must never be required for correct
promotion.

The active Catto Divergence Repairs work retained the existing partial-time CCD
architecture and limited ragdoll work to stage (a), accumulated impulse caching
and warm start. This future plan preserves that Swept TOI capability while
changing when Physics pays for it.

## Runtime Collision Paths

The three paths are solver decisions, not three authoring obligations:

- **Discrete:** The default path for ordinary bodies. It performs collision
  detection at fixed-step boundaries.
- **Swept TOI:** The retained exact translational continuous path used
  automatically for a body whose linear motion exceeds the per-tick threshold.
  It preserves sub-frame impact and bounce behavior for fast projectiles.
- **Speculative:** A later predictive contact path for ragdolls and articulated
  constraint islands. It is not introduced until Discrete correctness,
  determinism, and performance evidence are closed.

The resolved path is Physics-owned transient state. Do not add an authored
projectile mode or a per-body hot-store field merely for classification. Prefer
a stage-owned, fixed-capacity parallel bitset or byte array. Any proposal to add
a field to `PhysicsBodyRecord` or its hot arrays still requires the repository's
explicit owner ruling explaining why stage-owned storage is insufficient.

## Evidence To Capture Before Registration

Before activating any phase, record fresh evidence against the then-current
tree:

- The active Catto plan's local-interval results, including tunnelling,
  friction, penetration, and performance measurements.
- The current launcher-projectile and 200-box wall fast-ball velocities,
  collider thicknesses, fixed timestep, swept-path behavior, exact impact
  timing, and bounce results.
- Ragdoll stage (a)'s sag result, accumulated-impulse history, joint error by
  axis, iteration count, and contact-versus-joint impulse interaction.
- Current byte-exact physics, replay-fidelity, allocation, determinism, and
  performance gate results. Historical green output does not approve a future
  transition.
- Current per-stage Physics timings and work counts, including awake bodies,
  broadphase candidates, swept candidates, TOI evaluations, narrowphase pairs,
  contact rows, and solver iterations.
- Current ownership and complexity inventories for the exact solver and
  ragdoll surfaces that would change.

### Registration Evidence — 2026-08-22, `4df765245` on `main`

- `tools\validate_physics.bat` passed in 24.6 seconds. The registered golden
  digest `debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`
  matched; Debug/Profile builds and the byte-exact Physics comparison passed.
- `tools\validate_perf.bat` built Profile, then stopped before measurement on
  33 existing allocation-policy findings outside Physics. FP0 owns no exception
  or allowlist change for those findings.
- `tools\validate_replay_visual_fidelity.bat` built Automation and passed 18/18
  typed-packet and false-pass controls (82 assertions), then the authoritative
  200-box run failed the inherited reveal-0 `header.futureNodeCount` comparison.
  No replay or Physics oracle was refreshed.
- The authoritative run reported 212 admitted bodies, a 4,096-row fixed swept
  overlay, 848 broadphase candidate-pair rows, and byte-capped Physics stores.
  These are current measurements, not capacity allowances or ratchets.
- CodeGraph was current at 1,175 files, 36,520 nodes, and 109,730 edges. Focused
  exploration mapped the collider-offset, solver-snapshot, swept-overlay, and
  exact-cell-identity owners before source editing.
- FP0 is the only registered behavior transition. Threshold constants, Discrete
  A/B artifacts, joint-schema rulings, and speculative-contact evidence belong
  to their later phase activation boundaries and may not be invented in FP0.

---

## Verified PHYS Bug Intake

The 2026-08-22 master bug report identifies ten current Physics findings. Source
review supports all ten. These are not background debt that may be ignored while
changing collision paths: several can manufacture tunnelling, invalid replay
state, false performance evidence, or a fatal exactly in the workloads this
plan intends to expand.

| Finding | Source-supported problem | Plan disposition |
|---|---|---|
| `PHYS-001` | Fixed-step grid membership uses the body origin and a shape radius while the collider contract permits a rotated body-local centre offset. | Blocking FP0 repair. Broadphase membership and sweep bounds must use the conservative world-space collider centre and offset-inclusive extent. |
| `PHYS-002` | Sphere and box terrain probes use the body origin instead of the rotated collider-local centre. | Blocking FP0 repair. Terrain early-outs, sweeps, vertices, poles, and manifolds must share one world-shape transform. |
| `PHYS-003` | Object CCD constructs rays from body origins; sphere sweeps omit local centres and box/hull sweep helpers do not consistently rotate them. | Blocking FP0 repair before any automatic promotion evidence is trusted. |
| `PHYS-004` | Optimized sphere model matrices discard a non-zero local offset while Debug uses the full transform. | Coupled FP0 repair so Profile/Release presentation agrees with the repaired collider transform. |
| `PHYS-005` | Box model matrices add the local offset in world axes instead of rotating it with the body. | Coupled FP0 repair under the same world-shape transform invariant. |
| `PHYS-006` | The default `BoundingSphere` constructor leaves its promised local origin uninitialized. | Blocking FP0 initialization repair and default-descriptor test. |
| `PHYS-007` | Updating only a warmed point joint's bodies does not invalidate its accumulated impulse. | First FP5 repair before replacing the scalar impulse with the vector cache. |
| `PHYS-008` | Solver-snapshot preflight validates version/model/joint topology but not all vector sizes and capacities later mutated by restore. | Blocking FP0 transactional preflight repair before promotion state joins replay snapshots. |
| `PHYS-009` | All fast bodies share a fixed 4,096-row swept-overlay store; valid aggregate demand can fatal, and bounded traversal can leave an uncovered middle path. | Blocking FP0 capacity/fallback repair before automatic promotion can increase swept occupancy. |
| `PHYS-010` | XOR cell keys alias distinct coordinates. Current exact entry coordinates keep this conservative for collision discovery, but key-based buckets/diagnostics merge identity and can add false-positive work. | FP0 exact-identity repair so determinism and performance evidence do not depend on an aliased cell key. |

The bug report itself remains read-only evidence. Each repair needs a focused
test that fails on the current source; broad baseline movement alone is not
proof that the reported trigger was closed.

---

## FP0 — Physics Correctness Prerequisites

### What This Aims To Solve

Automatic mode selection cannot be judged on top of known collider-transform,
snapshot, and broadphase-capacity defects. FP0 establishes the source-of-truth
geometry and state invariants used by every later correctness, determinism, and
performance comparison.

### Proposed Solution

1. Define one shared world-shape transform rule:

   `worldColliderCenter = bodyPosition + bodyOrientation * localColliderOffset`

   Broadphase, terrain, CCD, exact manifolds, queries, and rendering must consume
   that rule or an equivalent owner-local value calculation. No path may treat a
   body-local offset as an unrotated world offset.
2. Define offset-inclusive conservative bounds for grid membership and sweeps.
   The broadphase radius/extent must cover the shape about the body origin even
   when the collider centre is displaced and rotated.
3. Zero-initialize every default shape's local offset and add descriptor-level
   finite/default-value tests.
4. Make Debug, Profile, and Release sphere/box transforms agree bit-for-bit on
   the translation produced from the same body pose and local offset.
5. Make solver-snapshot preflight validate every vector's model-count relation,
   fixed-list ceiling, current committed capacity, cross-vector relationship,
   point-joint topology, and byte cap before mutating any owner. Restore must
   commit atomically or leave the prior state unchanged.
6. Replace the shared swept-overlay failure mode with an owner-ratified bounded
   capacity or deterministic complete-coverage fallback derived from admitted
   scene capacity and measured workloads. A valid admitted scene must neither
   fatal nor silently omit a swept-path suffix. Do not introduce steady-step
   growth.
7. Replace key-only cell identity with an exact coordinate identity or require
   exact coordinate comparison behind the hash. Hash collisions may share a
   table home, but distinct cells must not become one logical bucket or one
   diagnostic identity.
8. Preserve stable pair ordering, fixed/pre-reserved storage, dependency
   direction, and byte-exact behavior outside the intended repairs.

### FP0 Acceptance

- Focused rotated non-zero-offset fixtures close `PHYS-001` through `PHYS-005`
  across broadphase, terrain, CCD, queries, Debug, Profile, and Release.
- Default-constructed sphere and descriptor fixtures close `PHYS-006` with an
  exact zero local offset and finite shape facts.
- Malformed, mismatched, and over-capacity solver snapshots close `PHYS-008` by
  failing preflight without mutation, fatal, or partial commit.
- Aggregate high-speed-body and long-trajectory fixtures close `PHYS-009`
  without allocation, fatal, or uncovered trajectory cells.
- The reported `(-5,-1,5)` and `(-5,1,-5)` coordinates remain distinct through
  grid lookup, candidate generation, diagnostics, and visualization, closing
  `PHYS-010` without destabilizing cell order.
- Focused repairs, byte-exact Physics, replay fidelity, allocation policy,
  dependency, and mapped Debug/Profile parity gates pass before FP1 begins.

### FP0 Closure Evidence - 2026-08-22

- Shared rotated collider centres and offset-inclusive bounds now agree across
  broadphase, terrain, object CCD, queries, exact manifolds, and rendering.
  Debug/Profile exact-bit Bounds fixtures pass 6/6 cases and 34 assertions in
  each configuration; the Release-linked transform probe matches the same bits.
- Replay solver restore now preflights all dense rows, committed capacities,
  cross-vector counts, contact/key/terrain coherence, sorted-unique warm-start
  keys, point-joint topology, and the byte cap before any owner mutates.
  Duplicate live feature keys collapse to the first row observed by
  `lower_bound`; the production clone fixture passes 612 assertions and the
  exact strict two-generation replay allocation interaction passes.
- Swept overlay admission uses complete-coverage fallback without partial path
  insertion or hot growth. The 256-body simultaneous fallback fixture emits all
  32,640 canonical pairs; the complete SpatialGrid family passes 31/31 cases
  and 9,235 assertions.
- Exact 57-bit spatial keys preserve full-width coordinates through grid,
  narrowphase events, diagnostics, and Runtime visualization. The reported XOR
  aliases remain distinct in the focused grid/visualizer fixture.
- `tools/validate_physics.bat` and `tools/validate_fast.bat --preflight-only`
  pass. The existing 44,401-line Physics golden remains byte-identical at
  SHA-256 `debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`;
  no Physics baseline transition was required. Replay visual controls pass
  18/18 before the separately controlled inherited reveal-0
  `header.futureNodeCount` mismatch.
- The final Debug executable is retained at
  `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP0/` with SHA-256
  `cdefc1b53c3de37c0d75fdd9a423b61aac8df368b45919f8a312cf6dc73cc053`.
- The touched-source comment audit checked 37/37 files with zero deferrals;
  strict glossary inventory reports 999 unique definitions and no drift.

---

## FP1 — Deterministic Motion Eligibility & Instrumentation

### What This Aims To Solve

Swept TOI should be paid for only when a body's predicted fixed-tick motion
crosses one simple absolute travel threshold. The classification must be cheap
enough to run once per non-sleeping body per fixed Physics tick and exact enough
to produce the same result across repetitions and supported worker counts.

Long rotating shapes also need cheap conservative candidate coverage even when
their centre barely moves. This phase covers angular eligibility and broadphase
expansion only. It does not promise exact rotational impact time or a special
rotational collision response.

### Proposed Solution

1. Run one contiguous eligibility pass after force application establishes the
   tick's velocities and before broadphase chooses its candidate paths.
2. Skip fixed and sleeping bodies. Evaluate every awake dynamic body and every
   moving kinematic body exactly once per fixed tick.
3. Resolve and cache the maximum radius from the centre of mass `r_max` when
   collider topology is created or changed. It supplies angular tip travel only;
   linear eligibility consumes no collider-size or thickness fact.
4. Evaluate linear motion without a square root:

   `linearTravelSq = |v|² * dt²`

   Compare it with the exact squared absolute promotion threshold:

   `linearPromoteSq = promoteTravelPerTick²`

5. Evaluate the cheap angular tip-motion bound without a square root:

   `angularTravelSq = |omega|² * r_max² * dt²`

   Angular eligibility expands conservative swept broadphase bounds so a long,
   rapidly rotating blade is not rejected solely because its centre moved
   little. Angular eligibility does not claim exact rotational TOI or response.
6. Use the owner-ratified version-2 values `promoteTravelPerTick = 0.1 metres`
   and `demoteTravelPerTick = 0.075 metres`. Specify equality behavior explicitly
   and retain the previous-tick promotion bit inside the stage-owned
   classification store so near-threshold motion cannot chatter.
7. Define snapshot, restore, topology-change, sleep, wake, body removal, and
   replay behavior for the classification store. Promotion state must never be
   reconstructed through iteration-order-dependent discovery.
8. Publish fixed-capacity diagnostics for evaluated bodies, discrete bodies,
   promoted bodies, angular-expanded bodies, and eligibility-pass duration.
   Diagnostics must add no steady-state allocation.

### FP1 Acceptance

- Below-threshold, exact-threshold, above-threshold, promotion, demotion, sleep,
  wake, removal, and restore fixtures produce exact resolved-path bits.
- Equivalent 0/1/4-worker runs and repeated clean-process runs produce
  byte-identical classification streams.
- Sphere, box, convex-hull, thin-projectile, and long-rotating-blade fixtures
  prove thickness-independent absolute travel and cached `r_max` behavior.
- The angular blade fixture proves conservative broadphase candidate retention
  without claiming impact-time or response correctness.
- The one-pass classification timing and work counts are captured separately
  from the collision work it selects.

### FP1 Closure Evidence - 2026-08-22

- One serial dense pass now runs after force/external-force wake publication and
  before broadphase. Policy version 1 ratifies `alphaPromote = 0.5` and
  `alphaDemote = 0.375`; cold equality promotes and hot demotion equality
  demotes. Fixed/sleeping rows clear their bits. The current engine has no
  kinematic body kind, so every representable moving non-fixed body is covered.
- Collider create/update caches exact metre-valued `t_min` and `r_max` geometry
  for spheres, boxes, and convex hulls. The focused sphere/box/hull, threshold,
  hysteresis, sleep/wake/topology-reset/removal, thin-projectile, scaled-normal
  hull, and long-blade/non-finite-fallback family passes 4/4 cases and 53/53
  assertions in both Debug and Profile. The real PhysicsEngine replay
  hysteresis-band continuation passes 1/1 case and 14/14 assertions.
- Angular reach uses resettable SpatialGrid overlay/fallback coverage while
  persistent membership retains the ordinary radius. The long-blade negative
  control rejects the unexpanded pair and the expanded path retains it without
  claiming rotational impact time or response.
- Motion state is a version-4 Physics snapshot tail with exact size/bit
  preflight, cold legacy restore, sparse replay deltas, deterministic hashing,
  v1-v4 query support, and disk checkpoint round-trip. The production replay
  snapshot fixture passes 645/645 assertions; artifact compatibility and the
  strict two-generation replay allocation policy pass.
- Exact 0/1/4-worker comparison passes 36,981/36,981 assertions. Two clean
  Profile processes produced byte-identical test evidence at SHA-256
  `5CFCB874C9AA34488AABD437BB0FCD9B00301189DA50C04C57ACCF3B6599B6E3`.
- The Profile focused success probe measured 3,800 ns for four evaluated rows
  while independently reporting two Discrete, two linear-promoted, and one
  angular-expanded body. Timing remains observational and is excluded from
  deterministic state equality.
- `tools\validate_physics.bat`, replay artifact compatibility, dependency,
  project-filter, ownership, compiled-symbol reachability, and strict replay
  allocation gates pass. The 44,401-line Physics golden remains byte-identical
  at SHA-256 `debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`;
  no Physics baseline transition was required.
- `tools\validate_fast.bat --preflight-only` passes after complete current
  Debug/Profile rebuilds, and the final Profile test executable passes 708/708
  cases with 2,544,123 assertions.
- The final Debug executable and manifest are retained under
  `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP1/`; executable
  SHA-256 is `612461c8dbd48eb8823468a8b06d1fb3f576b5610b6e48610b6b5a870ae7888a`.
- The touched-source comment audit checked 29/29 files with zero deferrals;
  strict glossary inventory reports 999 unique definitions and no drift.

---

## FP2 Owner Resolution — 2026-08-23

The owner confirmed that the authoritative 200-box striker is intentionally far
faster than ordinary scene bodies and selected simple absolute per-tick travel
thresholds: `0.1` metres promotes and `0.075` metres demotes. The checked-in
striker travels `1.41669` metres per 120 Hz tick, so it promotes without a
scene/name exception. Collider thickness no longer participates in either the
linear or angular eligibility comparison. FP2 is active; focused tests and the
phase performance evidence must measure how many additional bodies the simpler
threshold promotes.

The first version-2 checkpoint exposed a fixed SpatialGrid capacity ordering
hazard in the 520-body tornado fixture: angular overlays could consume the
shared 8,192-cell table before later bodies established persistent membership.
Broadphase now commits all maintained persistent rows before admitting transient
overlays, and the regression requires both classifications and actual
overlay/fallback work in serial and parallel execution. On the same machine,
the current 520-body measurement records Physics/Broadphase/GridMaintain means
of `0.9809/0.3028/0.1769 ms` versus `0.8618/0.2322/0.1238 ms` at the accepted
`07c065f65` base; this threshold/capacity checkpoint is intentionally not the
FP4 Discrete A/B ruling. The current tree completes the 2,000-body scale run,
while that base fatals at 8,192 active SpatialGrid cells. `validate_perf` still
stops before runtime measurement on the same 40 static allocation-policy rows
at both revisions, so these direct scale artifacts are evidence, not a green
replacement gate or a baseline refresh.

## FP2 — Discrete Default & Automatic Swept TOI Promotion

### What This Aims To Solve

The majority of bodies should remain on the cheaper Discrete path. Fast bodies
must automatically retain exact translational CCD without projectile tags,
scene-specific exceptions, or manual collision-mode authoring.

### Proposed Solution

1. Make Discrete the ordinary resolved path for awake bodies.
2. When FP1's linear test promotes a body, use the retained Swept TOI path for
   that body during the current fixed tick. Return it through the ratified
   deterministic demotion rule rather than a manual mode change.
3. Apply the eligibility pass after external forces and authored impulses for
   the tick so a body accelerated across the threshold is promoted before
   broadphase and TOI work are selected.
4. Preserve relative-motion broadphase coverage and stable pair ordering. The
   ratified safety margin must prove that two opposing individually Discrete
   bodies cannot together cross the protected minimum-thickness envelope.
5. Preserve exact sub-frame impact, restitution, wakeup, and contact handoff for
   automatically promoted translating bodies.
6. Route angular eligibility only into conservative swept broadphase coverage.
   Exact rotational sweep, rotational TOI, and special blade response remain
   outside this phase.
7. Serialize configuration and stage-owned promotion state required for exact
   replay restore. Do not serialize a redundant authored projectile mode.
8. Delete or explicitly adjudicate the test-only
   `BoundingBox::TestCollision(BoundingSphere, ...)` and
   `BoundingSphere::TestCollision(BoundingBox, ...)` legacy overloads. The live
   object narrowphase routes both concrete orders through
   `SweepSphereAgainstBox`; a symmetry-only unit test must not manufacture
   production reachability for retired swept surface.

### Hazards And Required Tests

- Near-threshold bodies must not alternate paths because of comparison or
  worker-order drift.
- Two bodies approaching in opposite directions must not tunnel while both are
  individually below the promotion threshold.
- A ball that begins Discrete, receives a collision impulse above the promotion
  threshold from a fast ball, and then reaches a thin wall must promote on the
  following classification pass and use Swept TOI instead of tunnelling through
  the wall.
- Fast rotation must retain a broadphase candidate for a long hull corner, but
  no test may pretend that candidate retention proves rotational response.
- Static friction and resting contact behavior must remain Discrete and must
  not inherit swept-only policy.
- Automatically promoted bodies must not drop frames through an unbounded
  candidate expansion or TOI loop.
- Replay capture and restore must reproduce promotion, demotion, impacts, and
  resolved paths byte-identically.
- Strict compiled-symbol reachability must remove both temporary
  sphere-box-overload `repair-plan` rows by deleting/adjudicating their retired
  surface and preserving focused coverage at the live specialized sweep owner.

### FP2 Acceptance

- Ordinary slow-moving scenes resolve the majority of awake body-ticks as
  Discrete, with measured counts rather than an invented percentage target.
- The camera-fired `launcher_projectile` automatically promotes at every
  supported launcher speed that exceeds the ratified threshold; no projectile
  tag or explicit continuous mode exists.
- The fast ball in the authoritative 200-box wall workload automatically
  promotes and preserves its expected cascade, impact timing, and bounce
  behavior; no scene-specific exception exists.
- A generic non-projectile body with the same shape and velocity resolves to
  the same collision path, proving classification is Physics-owned rather than
  gameplay-owned.
- Fast bouncy bodies preserve the existing exact Swept TOI baseline behavior.
- The collision-driven promotion fixture proves the target ball starts
  Discrete, is accelerated past `0.1` metres per tick by another ball, publishes
  the promoted bit on the next tick, and collides with a thin wall without
  tunnelling.

### FP2 Closure - 2026-08-24

The phase-local legacy-surface review deleted the test-only
`BoundingBox::TestCollision(BoundingSphere, ...)` and
`BoundingSphere::TestCollision(BoundingBox, ...)` overloads, the unused
`TestShapeCollision` dispatcher, and the symmetry-only unit-test calls. Both
live shape orders still converge on `SweepSphereAgainstBox` in
`ObjectContactManifold`; the focused Bounds and Object CCD families pass 5/5
and 3/3 cases respectively, including the both-order thin-wall witness.

Fresh Automation, Debug, and Profile builds pass with zero warnings/errors.
Strict compiled reachability now reports 87 current ruled rows and no blocking
diagnostics after the two deleted repair rows were removed. Dependency,
ownership, formatting, Related-path, and the broader focused promotion,
eligibility, replay, wake, launcher, terrain, and SpatialGrid witnesses pass.
Independent review found zero remaining implementation findings after one
comment-truth correction.

FP2 is closed. The standing Physics-plan automated override accepted the exact
archived transitions, while the artifact manifest retains the prior and new
producer executables, launch DLLs, hashes, and FP1-versus-FP2 200-box evidence.
The core, deep, and replay-visual gates pass. The replay witness completes one
2,401-tick prediction generation, moves all 200 wall bricks, publishes all 200
causal nodes, and rejects the visual, causal, artifact, trajectory-count,
determinism, and settled-majority false-pass controls. The exact visual and
causal baseline digests are recorded in the FP2 artifact manifest. FP3 is now
the active Physics phase.

---

## FP3 — Discrete Correctness & Determinism Closure

### What This Aims To Solve

Deterministic Discrete simulation is the plan's highest-priority acceptance
contract. Predictive ragdoll implementation is forbidden until this phase has
closed independently.

### Required Proof

1. Run repeated clean-process byte-exact Physics comparisons with identical
   authored state and inputs.
2. Prove exact body state, contact identities, pair order, manifold order,
   solver rows, sleep state, promotion bits, and replay state across 0/1/4
   workers.
3. Cover all threshold boundaries, including exact equality, one representable
   float below and above, force-driven promotion, collision-driven wake, sleep,
   demotion, snapshot/restore, and topology replacement.
4. Prove stable Discrete pair and constraint ordering through ordinary scene
   size changes and worker partition changes.
5. Run the varied 211-body Physics regression, the 200-box wall cascade,
   projectile fixtures, dense contacts, terrain, and jointed bodies.
6. Treat every byte difference as a behavior transition requiring explanation,
   focused evidence, and the archived automated lane. Do not refresh a golden
   merely to close this phase.

### FP3 Acceptance

- All focused and repository byte-exact determinism oracles pass.
- Serial, one-worker, and four-worker results are byte-identical for the new
  Discrete-default simulation and automatic-promotion stream.
- Camera projectiles and the 200-box fast ball prove deterministic promotion
  and Swept TOI behavior.
- No speculative ragdoll row, predictive contact path, or predictive
  classification has entered the implementation.
- The owner explicitly records that Discrete determinism is closed before FP4
  or any later predictive phase proceeds.

---

## FP4 — Discrete Performance A/B & Owner Ruling

### What This Aims To Solve

A green general performance gate cannot attribute improvement to Discrete. This
phase measures the benefit of avoiding unnecessary swept work with an isolated,
same-workload A/B comparison.

### Required A/B Design

1. Add a validation-only runtime selector that can force the then-current
   legacy swept eligibility behavior for variant A or enable the new automatic
   Discrete classification for variant B. It must not become an authored scene
   option or a shipping per-body mode.
2. Run both variants from the same executable, fixed timestep, authored scene,
   inputs, worker count, frame range, renderer settings, and machine session.
3. Alternate warm A/B/A/B runs and retain every raw timing artifact rather than
   selecting only the fastest sample.
4. Report classification time, broadphase time and pairs, swept candidates, TOI
   evaluations and iterations, narrowphase time and pairs, solver time and rows,
   total Physics average/p95/p99/max, allocation counters, and final state hash.
5. Include slow-body-heavy, mixed-speed, launcher-projectile, 200-box wall,
   dense-contact, and supported scale-matrix workloads.
6. Require behavioral equivalence where the resolved collision path is intended
   to be equivalent. Separately identify accepted, evidence-backed differences
   caused by removing unnecessary swept work.

### FP4 Acceptance

- The measured Discrete improvement and classification overhead are stated
  separately for every workload.
- Raw A and B artifacts and state hashes are retained for review.
- No claimed improvement relies only on a committed baseline captured on a
  different machine or source tree.
- The owner rules whether the measured improvement justifies the transition.
- FP8 predictive work remains unauthorized until FP3 and FP4 are both closed.

---

## FP5 — Ragdoll Stage (b): Three-Degree-Of-Freedom Point Joint

### What This Aims To Solve

The current joint is one scalar row along the present anchor-error direction.
It primarily corrects distance and cannot simultaneously constrain all three
components of anchor-relative velocity. Near zero error the preferred axis also
becomes poorly defined. Ragdoll stage (a) adds temporal coherence to that
scalar model, but warm start alone does not turn it into a true point-to-point
constraint.

Stage (b) aims to pin the two world-space anchors to coincidence in all three
linear dimensions while still allowing the bodies to rotate around the joint.

### Proposed Solution

1. Build the two anchor arms from each centre of mass and compute anchor
   velocities including angular velocity cross arm.
2. Close `PHYS-007` first: changing either bound body invalidates the existing
   scalar accumulated impulse before any warm start can reach the new pair. Add
   body-only rebind, anchor-only update, solver-only update, and unchanged-row
   preservation fixtures.
3. Form the 3-by-3 point-constraint effective-mass matrix from inverse masses,
   world-space inverse inertias, and the two skew-symmetric anchor-arm terms.
4. Solve one vector impulse for the three coupled axes. Use deterministic,
   explicitly guarded singular handling for fixed or nearly singular bodies;
   do not hide failure behind an unconstrained matrix inverse.
5. Replace the scalar cached impulse introduced by ragdoll stage (a) with an
   accumulated vector impulse owned by the stable constraint handle. Define the
   coordinate space and lifetime of that cache and warm-start both linear and
   angular body velocity from it.
6. Clamp only against a physically named joint-force or impulse limit if such a
   limit is part of the authored constraint. Do not independently clamp the
   three vector components, which would make the result basis-dependent.
7. Preserve a focused diagnostic trail containing vector error, vector impulse,
   effective-mass conditioning, and per-iteration residual without allocating
   in the solver hot path.

### FP5 Acceptance

- A joint under off-axis load controls all three anchor-error components rather
  than merely preserving anchor distance.
- The result is invariant under equivalent world-axis rotations of the fixture.
- Fixed/dynamic, dynamic/dynamic, zero-length error, and near-singular inertia
  cases have focused tests.
- Ragdoll sag, jitter, energy, and iteration cost are measured against both the
  pre-stage-(a) solver and the completed stage-(a) state.
- Every Physics or replay golden transition is exact-digest, behavior-explained,
  and bound to an append-only old/new launch bundle before commit.

---

## FP6 — Ragdoll Stage (c): Explicit Softness Model

### What This Aims To Solve

The current expression `(relativeVelocity + biasSpeed) * (1 + damping)` mixes
positional recovery and damping without stable physical units. Its apparent
stiffness changes with iteration count and fixed-step duration, and the
`damping` value is not recognizably a damping ratio, decay rate, or force.

Stage (c) aims to make joint stiffness and damping explicit, tunable, and
predictable across the supported fixed-step envelope.

### Proposed Solution

1. Use FP5 measurements to choose one named model rather than blending two:
   - a hard point constraint with bounded Baumgarte positional bias; or
   - a soft constraint expressed through natural frequency and damping ratio,
     converted to solver bias and compliance for the current `dt`.
2. For a soft constraint, add compliance to the 3-by-3 effective-mass solve in a
   unit-consistent form and include the accumulated impulse term required by the
   formulation. Document every coefficient's units and limiting behavior as
   stiffness approaches zero or a hard constraint.
3. Apply damping to relative anchor motion through the constraint equation, not
   as a post-solve velocity multiplier or hidden energy sink.
4. If authored settings change meaning, use the repository's versioned schema
   migration process. Do not silently reinterpret existing scene values.
5. Measure step-size and substep sensitivity at the supported fixed steps, and
   record whether the chosen parameters represent frequency/damping ratio,
   compliance, or direct solver coefficients.

### FP6 Acceptance

- Joint parameters have documented units and one owner.
- Equivalent physical settings produce bounded, explained behavior across the
  supported fixed-step sizes and iteration counts.
- Energy decay is attributable to the selected damping model and visible to
  diagnostics.
- Ragdoll load, free swing, impact recovery, and long-rest tests distinguish the
  new model from the removed ad-hoc multiplier.
- Any authored-data migration retains its separate schema ruling; an exact
  golden transition uses the archived automated Physics lane.

---

## FP7 — Ragdoll Stage (d): Shared Contact And Joint Iteration

### What This Aims To Solve

Contacts currently solve first and point joints solve in a separate pass. A
body that is both supported by contact and constrained by a joint can be pushed
by one family and pulled back by the other without either family seeing the
newest impulse until the next outer step. The result can be
iteration-order-dependent softness, sag, or contact/joint fighting.

Stage (d) aims for each PGS sweep to observe both contact and joint responses,
so one convergence process owns the coupled constrained system.

### Proposed Solution

1. Introduce a concrete constraint-solve transaction that owns preparation,
   warm start, iteration order, convergence observation, writeback, and phase
   legality. Do not introduce a solver-wide context bag, callback pack,
   polymorphic row hierarchy, or owner reach-back.
2. Keep contact scalar rows and joint 3-by-3 blocks in compact typed arrays. A
   deterministic schedule may visit typed arrays through tagged indices or
   explicit loops; the shared property is the sweep and body-velocity state,
   not a lowest-common-denominator data bag.
3. Build deterministic islands or another explicit ownership boundary so only
   constraints connected through bodies share convergence authority. Preserve
   stable ordering by body identity, constraint handle, contact pair, and
   feature identity.
4. Warm-start both families before iteration. During every sweep, apply contact
   normal/friction rows and joint blocks against the same current body-velocity
   scratch, respecting contact normal-to-friction dependencies.
5. Define convergence across scalar contact impulses and vector joint impulses
   without summing unrelated row counts into a scene-size-dependent criterion.
6. Publish typed diagnostics that identify family, owner, row/block identity,
   residual, and impulse while preserving zero steady-state allocation.
7. Delete the old post-contact ragdoll solve pass once equivalence and intended
   behavioral differences are proven. A forwarding wrapper would leave the
   split authority in place and does not close this phase.

### FP7 Acceptance

- Focused fixtures show that a contacted, joint-constrained body converges as
  one system rather than alternating between two independent passes.
- Joint stiffness and contact support are stable under deterministic constraint
  ordering, supported worker counts, and ordinary scene-size changes.
- The solve transaction enforces legal phase order and passes the repository's
  aggregate, extraction-scar, wide-signature, complexity, and ownership review.
- Hot-path storage remains fixed or pre-reserved with no new runtime allocation
  privilege.
- Physics, replay visual fidelity, and performance gates pass after any
  archived exact golden transition.

---

## FP8 — Late Predictive Ragdoll Speculative Contacts

### What This Aims To Solve

Fast articulated limbs can tunnel under Discrete collision. Running a whole
ragdoll network through micro-substepped Swept TOI can desynchronize its joint
network and create disproportionate CPU cost. Only after the Discrete system
and joint solver are independently proven may ragdolls receive uniform-step
predictive/speculative contacts.

### Proposed Solution

1. Derive ragdoll/articulated eligibility from stable Physics-owned constraint
   membership or another explicit Physics-owned articulation identity. Do not
   require individual limb or projectile collision-mode authoring.
2. Reuse FP1's cached linear and angular motion bounds to extend broadphase
   coverage along conservative relative motion.
3. Compute closest points, separating normal, signed separation `s`, and a
   conservative angular closing bound for speculative candidates.
4. For positive separation `s` and closing normal velocity `v_n < 0`, admit a
   speculative row only if `s + v_n * dt < 0` under the ratified conservative
   motion bound.
5. Target `v_n >= -s / dt` and clamp the impulse unilaterally so speculative
   contacts brake closing motion but never pull separating bodies together.
6. Disable friction while separated to prevent ghost drag. Fire restitution
   only at actual touching contact so a predicted row does not manufacture a
   bounce.
7. Solve speculative contacts at the same fixed-step boundary as the entire
   articulated joint network. Do not introduce micro-substeps.
8. Preserve deterministic feature identity, row ordering, warm-start lifetime,
   snapshot/restore, and replay publication.

### Hazards And Required Tests

- Near misses and grazing trajectories must not produce ghost collisions or
  mid-air deflection.
- Fast limbs must not tunnel through thin static or dynamic walls.
- Static friction must engage only on physical contact.
- Speculative rows must not perturb unrelated Discrete islands.
- Conservative angular bounds may retain extra candidates, but their admission
  and rejection must be deterministic.

### FP8 Acceptance

- Ragdoll limbs swinging at the ratified high-speed fixture do not tunnel
  through thin static or dynamic walls.
- All limbs remain on the same fixed-step boundary without ragdoll-specific
  substepping.
- Near-miss, grazing, resting-friction, impact-restitution, and long-rest
  fixtures prove the intended predictive boundaries.
- No exact arbitrary-hull rotational TOI or special helicopter-blade response
  is claimed or added.

---

## FP9 — Predictive Determinism, Performance A/B & Plan Closure

### What This Aims To Solve

Predictive ragdoll contacts must be deterministic, and their CPU cost must be
measured independently of both the earlier Discrete improvement and the joint
solver changes.

### Required Determinism Proof

1. Repeat clean-process and 0/1/4-worker comparisons with speculative contacts
   active.
2. Compare exact speculative candidate identity, row order, separation,
   impulses, body state, joint state, sleep state, and replay state.
3. Cover threshold crossings, grazing rejection, contact transition,
   cancellation, snapshot/restore, topology changes, and repeated prediction
   generations.
4. Require byte-exact equality; epsilon-only comparisons do not close the
   deterministic contract.

### Required A/B Design

1. Add a validation-only selector that runs the exact same completed ragdoll
   solver with speculative contacts disabled for variant A and enabled for
   variant B. It must not become an authored gameplay option.
2. Use the same executable, fixed timestep, ragdoll topology, initial state,
   inputs, worker count, frame range, renderer settings, and machine session.
3. Alternate warm A/B/A/B runs and retain all raw results.
4. Report speculative eligibility time, expanded candidates, admitted rows,
   narrowphase time, solver time and iterations, total Physics average/p95/p99/
   max, allocations, final state hash, tunnelling result, joint error, and
   resting jitter.
5. Separate the measured predictive cost from FP4's Discrete gain. Do not report
   only a net end-state number that hides either contribution.

### FP9 Acceptance

- Predictive ragdolls are byte-exact across repetitions and 0/1/4 workers.
- The speculative-off/on CPU and memory cost is stated independently for every
  ragdoll workload.
- The A/B evidence reports both cost and the correctness benefit; a cheaper
  tunnelling variant is not treated as a valid performance win.
- Physics, replay visual fidelity, allocation, performance, and full validation
  pass after behavior evidence and any archived exact golden transition.
- The owner accepts the final balance of Discrete gain, predictive cost,
  deterministic behavior, and ragdoll stability.

## Candidate Phase Order

- [x] **FP0 — Physics correctness prerequisites.** Close the verified collider
  offset, snapshot, swept-overlay, and grid-key findings before measuring or
  changing collision paths.
- [x] **FP1 — Deterministic motion eligibility and instrumentation.** One cheap
  classification per awake/moving body per fixed tick, including conservative
  angular broadphase eligibility.
- [x] **FP2 — Discrete default and automatic Swept TOI promotion.** No projectile
  tags or scene-specific continuous modes.
- [ ] **FP3 — Discrete correctness and determinism closure.** Mandatory blocker
  before any predictive implementation.
- [ ] **FP4 — Discrete performance A/B and owner ruling.** Isolate and record the
  performance improvement.
- [ ] **FP5 — Ragdoll 3-DOF point joint.** Pin linear anchor coincidence with a
  3-by-3 effective mass and vector warm start.
- [ ] **FP6 — Explicit ragdoll softness.** Use principled frequency/damping or
  compliance across timestep and iteration variations.
- [ ] **FP7 — Shared contact/joint iteration.** Unify deterministic PGS sweeps.
- [ ] **FP8 — Late predictive ragdoll contacts.** Add uniform-step speculative
  contacts only after every prior proof is closed.
- [ ] **FP9 — Predictive determinism, performance A/B, and closure.** Isolate the
  cost and prove byte-exact behavior.

The phases are strictly ordered and each carries a stop-and-review boundary.
FP1 must not begin while an FP0 finding remains open. FP8 must not be registered,
implemented, or partially scaffolded before FP0 through FP7 are closed. In
particular, no speculative row or ragdoll predictive classification belongs in
the Discrete transition.

## Non-Goals While Unregistered

This historical gate no longer applies to FP0-FP9 after the explicit
2026-08-22 activation and baseline-transition authorization above. It remains
the rule for any future phase or extension that has not been registered.

- Do not select or implement any checkbox in this file.
- Do not add this file to `Agentic/Plans/MASTER-PLAN.md` or binding order without
  a new explicit owner direction.
- Do not refresh Physics, replay, SkullScope, visual, or performance baselines.
- Do not discard the retained Swept TOI continuous collision path.
- Do not add projectile tags, projectile collision modes, scene-specific CCD
  exceptions, or manual continuous authoring requirements.
- Do not implement exact rotational TOI for arbitrary convex hulls.
- Do not claim special collision response for helicopter blades or other long
  rotating machinery; this plan adds only cheap angular eligibility and
  conservative broadphase coverage before the later speculative ragdoll work.
- Do not import Box2D, Bullet, or another engine's source. Primary literature
  may guide derivation, but repository code owns its contracts.

## Validation Map After Registration

| Future change | Required pre-commit evidence |
|---|---|
| FP0 correctness prerequisites | One focused regression per `PHYS-001` through `PHYS-006` and `PHYS-008` through `PHYS-010`; Debug/Profile transform parity; byte-exact Physics and replay gates; allocation and dependency gates |
| FP1 eligibility and instrumentation | Focused threshold/cache/state tests; repeated and 0/1/4-worker classification checks; focused performance measurement; `tools\validate_physics.bat` |
| FP2 Discrete default and automatic promotion | Focused Discrete/Swept TOI, launcher-projectile, 200-box fast-ball, opposing-body, sleep/wake, replay, and angular-candidate tests; `tools\validate_physics.bat`; `tools\validate_physics_deep.bat`; `tools\validate_replay_visual_fidelity.bat` |
| FP3 Discrete determinism closure | Repeated clean-process and 0/1/4-worker byte-exact oracles; varied Physics regression; replay fidelity; dependency and allocation gates; no predictive source present |
| FP4 Discrete A/B | Same-executable alternating A/B artifacts; state hashes; `tools\validate_perf.bat`; owner performance ruling |
| FP5 or FP6 ragdoll joints | `PHYS-007` body-rebind warm-start regression; focused ragdoll and solver tests; `tools\validate_physics.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_perf.bat` |
| FP7 shared solver | Focused contact/joint and ordering tests; ownership inventories; `tools\validate_physics.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_perf.bat` |
| FP8 speculative ragdolls | Focused tunnelling, grazing, friction, restitution, angular-bound, replay, and deterministic-order tests; mapped Physics and replay gates |
| FP9 predictive closure | Repeated and 0/1/4-worker predictive oracles; same-executable alternating speculative-off/on A/B; allocation and performance gates; `tools\validate_full.bat --plan-completion` |
| Authored joint schema change | Versioned migration tests; `tools\migrate_data_formats.py --check`; mapped Physics and full gates |
| Documentation-only refinement | No validation required |

## Registration And Closure Conditions

To activate work, register only the next strictly ordered phase or an exact
contiguous phase group the owner intends to execute. The registered plan must
add current evidence, exact task counts, a commit token, binding order,
per-transition baseline rulings, deterministic threshold constants and units,
A/B artifact paths, and any exception inventory the then-current repository
contract requires.

The registered implementation plan must preserve the separation between:

1. classification overhead;
2. Discrete performance improvement;
3. ragdoll joint-solver cost; and
4. speculative predictive cost.

No net aggregate performance number substitutes for these four measurements.
This file may be deleted once its candidate work has moved into registered
plans or been explicitly declined. Git history remains the archive.

## Reference Sites

- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/BoundingSphere.cpp`
- `SkullbonezSource/Physics/BoundingBox.cpp`
- `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- `SkullbonezSource/Physics/SpatialGrid.cpp`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- `SkullbonezSource/Physics/Ragdoll.cpp`
- `SkullbonezSource/Physics/PhysicsHandles.h`
- `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- `SkullbonezTests/TestDeterminism.cpp`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `tools/validate_perf.bat`
- `tools/validate_replay_visual_fidelity.bat`
- `Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf`
- `Agentic/Reference/physics-overview.md`
