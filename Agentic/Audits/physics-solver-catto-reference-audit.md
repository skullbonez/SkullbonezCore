# Physics Solver Audit Against Catto Iterative Dynamics Reference

**Date:** 2026-06-01
**Status:** Draft for review
**Reference:** `Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf`
**Current edit type:** Documentation only
**Primary future impact areas:** Physics, broadphase, performance, tests

## Purpose

This audit checks the current SkullbonezCore solver against Erin Catto's GDC 2005
iterative dynamics paper before further optimisation work. The goal is to avoid
optimising code paths that should first be corrected, unified, or replaced.

The short version:

- The terrain solver is already close to a Catto-style projected Gauss-Seidel
  contact solver.
- The object-object path is only partially aligned. It still mixes immediate
  swept collision response, pair-only persistent contacts, bounding-radius
  contact geometry, and scalar body mutation inside the solver loop.
- The next work should establish the correct solver shape before heavy
  optimisation.

## Reference Model Summary

The paper's relevant design points are:

| Topic | Reference guidance |
|-------|--------------------|
| Constraint storage | Pairwise constraints can be stored sparsely as two body indices plus two 6-vector Jacobian blocks, giving linear time and space. |
| Contact normal | The normal constraint uses contact-point velocity and clamps the normal multiplier so contacts push but do not pull. |
| Bias | Penetration is handled with Baumgarte velocity bias derived from the position error. |
| Friction | Two tangent constraints are solved per contact. Catto's paper uses a simplified friction bound based on assigned contact mass and gravity. |
| World inertia | Rotational inertia is transformed to world coordinates as orientation changes. |
| Time stepping | Fixed time steps are recommended when repeatability matters. Positions are integrated after solving velocities. |
| Solver | Projected Gauss-Seidel clamps accumulated multipliers, applies the actual delta, and can avoid forming the full matrix. |
| Contact caching | Cached multipliers should be keyed by interaction pair and contact-point identifier, not only by body pair. |
| Box stacking | Stable stacks depend on manifolds, redundant contact handling, static friction, and contact caching. The paper's stack example does not rely on sleeping or damping. |

## Current Code Alignment

| Area | Fit with reference | Notes |
|------|--------------------|-------|
| Terrain PGS shape | Good | The former terrain response path built contact rows, computed normal/tangent effective masses, accumulated impulses, clamped normal impulses, and had adaptive early-out. |
| Terrain box inertia | Good | Terrain response uses world-space inverse inertia for boxes. |
| Fixed-step support | Good | `PHYSICS_FIXED_DT` exists and physics validation uses fixed-step scenes. |
| Persistent contact concept | Partial | `SolvePersistentObjectContacts` creates persistent rows and caches impulses across frames. |
| Friction approximation | Partial | Persistent contacts use a Catto-like constant friction limit, but terrain and immediate contacts use different friction budgets. |
| Sparse/vector-friendly layout | Partial | Terrain response keeps one body's state in locals/SSE registers. Persistent object contacts mutate full `GameModel` state repeatedly instead of using compact solver body arrays. |

## Key Gaps Before Optimisation

### 1. Object-object contact geometry is not ready for deep optimisation

Current code still uses bounding radii and a center-to-center normal for mixed and persistent
object contacts:

- `SkullbonezSource/SkullbonezGameModelCollection.cpp:514`
- Former terrain solver source, row solve loop.

Catto's box-stacking section depends on real contact manifolds. A single bounding-radius contact
can be acceptable as a broadphase or fallback approximation, but optimising it heavily risks
locking in the wrong object-contact model.

Suggestion:

1. Keep terrain contacts as the first optimisation target only if terrain remains the measured cost.
2. For object-object work, implement real sphere-sphere, sphere-box, and OBB-OBB contact manifolds
   before spending time on advanced solver micro-optimisation.
3. Preserve the bounding-radius path only as an early reject or temporary fallback.

### 2. Immediate collision response and persistent contacts are split

`RunSolverPhysics` does an immediate swept collision response and then runs
`SolvePersistentObjectContacts`:

- Immediate response calls: `SkullbonezSource/SkullbonezGameModelCollection.cpp:857`,
  `SkullbonezSource/SkullbonezGameModelCollection.cpp:875`,
  `SkullbonezSource/SkullbonezGameModelCollection.cpp:902`
- Persistent pass: `SkullbonezSource/SkullbonezGameModelCollection.cpp:940`

Catto's model is cleaner: generate constraints, solve them together, then integrate.
Separate impact and resting paths can fight each other, duplicate work, and make profiling
misleading.

Suggestion:

1. Move toward one contact generation phase and one solver phase.
2. Treat high-speed impact contacts as rows with restitution bias rather than a separate
   response function.
3. Keep swept detection if needed for tunnelling, but emit solver contacts instead of directly
   mutating velocities.

### 3. Contact cache identity is too weak

Current persistent cache entries key by body pair:

- `SkullbonezSource/SkullbonezGameModelCollection.h:75`
- Lookup loop at `SkullbonezSource/SkullbonezGameModelCollection.cpp:670`

Catto's cache requires an interaction pair plus a contact-point identifier. Pair-only caching is
only safe while each pair has exactly one stable contact. It will alias badly once box manifolds
produce multiple rows.

Suggestion:

1. Extend cache keys with a feature id.
2. For spheres, the feature id can be a simple constant per pair.
3. For box contacts, use incident/reference feature labels when available.
4. For transitional work, local-space contact position can be used with a tolerance, but treat it
   as lower quality than feature labels.
5. Replace the linear cache scan with sorted keys or a small fixed hash table after the identifier
   model is correct.

### 4. Persistent object solving does not use a solver body cache

`SolvePersistentObjectContacts` applies impulses by reading and writing `GameModel` velocity and
angular velocity for every row and iteration:

- `SkullbonezSource/SkullbonezGameModelCollection.cpp:493`
- Iteration rows at `SkullbonezSource/SkullbonezGameModelCollection.cpp:694`

The paper's sparse formulation is designed around compact body blocks and constraint rows. The
current persistent path is harder to reason about and harder to optimise.

Suggestion:

1. Add per-step `SolverBodyState` arrays for awake bodies.
2. Store linear velocity, angular velocity, inverse mass, and world inverse inertia once.
3. Run PGS against cached body states.
4. Write back to `GameModel` once after solving.
5. Only then consider SIMD or SoA packing for object contacts.

### 5. Box inverse inertia is inconsistent in object contacts

Terrain and mixed immediate response transform box inverse inertia into world space. The persistent
object contact pass uses component-wise inverse inertia:

- Persistent lambda: `SkullbonezSource/SkullbonezGameModelCollection.cpp:485`
- Former terrain world inertia path.

Catto's equations use world-space inertia. This is a correctness issue first and a performance issue
second.

Suggestion:

1. Precompute world inverse inertia per solver body.
2. Use the same function for terrain, immediate impact contacts, and persistent contacts.
3. Add tilted-box contact tests before and after this change.

### 6. Friction model is inconsistent across paths

The paper uses two tangent constraints with a simplified constant bound:

- bound = `mu * contactMass * gravity`

Current code uses at least three variants:

- Terrain: `mu * max(accN, gravityWarmStart)` in the former terrain solver path.
- Persistent contacts: `mu * contactMass * gravity * dt` at `SkullbonezSource/SkullbonezGameModelCollection.cpp:665`
- Immediate mixed contacts: `mu * normalImpulse` in the former immediate response path.

All three can be defensible, but using them together makes behavior and optimisation measurements
harder to interpret.

Suggestion:

1. Pick one friction model for the unified solver.
2. If matching Catto is the priority, use the constant contact-mass gravity bound first.
3. If Coulomb normal-force friction is preferred, document that this intentionally differs from
   the paper and validate slope, stack, and sliding cases explicitly.
4. Clamp two tangent impulses as a vector cone if the chosen model is Coulomb-like.

### 7. Position correction is partly outside the constraint model

Catto handles penetration through velocity bias. Current code also applies direct position
projection:

- Former terrain projection path.
- Persistent projection: `SkullbonezSource/SkullbonezGameModelCollection.cpp:744`
- Static overlap projection: `SkullbonezSource/SkullbonezGameModel.cpp:278`

This may be pragmatic, but it can hide solver drift and inject behavior that is not represented in
the constraint rows.

Suggestion:

1. Keep direct projection only as a measured transitional safety.
2. Add a test mode that reports how much projection is applied each frame.
3. Prefer split impulse or capped Baumgarte once the shared solver is stable.
4. Do not optimise projection-heavy scenes until it is clear the projection is not masking solver
   instability.

### 8. Sleeping can hide solver weaknesses

The current solver uses sleeping:

- `SkullbonezSource/SkullbonezGameModelCollection.cpp:796`
- Sleep transition at `SkullbonezSource/SkullbonezGameModelCollection.cpp:962`

Catto's stack result explicitly demonstrates stability without sleeping or damping. Sleeping is a
valid production optimisation, but it is a poor audit tool because it can hide creeping, jitter, or
weak support forces.

Suggestion:

1. Add a debug/config option to disable sleeping for physics validation scenes.
2. Add stack, slope, and mass-ratio scenes that run with sleeping disabled.
3. Use sleeping-enabled scenes for performance validation only after the no-sleep solver behavior is
   acceptable.

### 9. Visual roll alignment and angular clamps are outside the solver

Current terrain response still mutates orientation directly for visual pole alignment:

- Former terrain visual roll-alignment path.

Angular velocity is also throttled in normal force application:

- `SkullbonezSource/SkullbonezRigidBody.cpp:192`

These may be necessary while the solver is still being stabilized, but they are not part of Catto's
constraint model and can make the solver appear better than it is.

Suggestion:

1. Keep both behind diagnostics/config while auditing.
2. Track how often each path fires.
3. Remove or debug-gate them after contact rows, friction, and caching are corrected.

### 10. Time stepping is close, but collision response still consumes per-body frame slices

The code has fixed-step support, but `RunSolverPhysics` still uses per-body `m_timeRemaining`,
collision-time advancement, and terrain response consuming the rest of the frame:

- `SkullbonezSource/SkullbonezGameModelCollection.cpp:238`
- `SkullbonezSource/SkullbonezGameModelCollection.cpp:891`
- `SkullbonezSource/SkullbonezGameModelCollection.cpp:932`

Catto's time-step model is: apply forces, solve constraints for new velocities, then integrate
positions. The current frame-slice approach is a hybrid of continuous collision response and
iterative constraint solving.

Suggestion:

1. Decide whether the solver should be primarily discrete/speculative or swept/event based.
2. If using Catto as the reference, prefer a discrete contact generation pass plus optional
   speculative contacts for tunnelling.
3. Keep swept collision detection only where it emits contact rows into the unified solver.

## Recommended Pre-Optimisation Work

Do these before significant performance tuning:

1. Add no-sleep validation scenes for stack, slope, dense sphere contacts, and mass-ratio cases.
2. Add instrumentation for:
   - number of generated contact rows
   - number of projected position corrections
   - cache hit/miss rate
   - solver iterations used before early-out
   - visual alignment and angular clamp activations
3. Define one canonical solver row format and body-state cache.
4. Use world inverse inertia consistently for every object-contact path.
5. Replace pair-only cache identity with pair plus feature/contact id.
6. Choose and document one friction model.
7. Route immediate contacts and persistent contacts through the same solver.

## Optimisation Implications

The previous optimisation plan should be interpreted in this order:

1. First, align the solver architecture with the reference.
2. Then optimise the shared body-state/contact-row loop.
3. Then optimise broadphase and sleeping around the corrected solver.

In practical terms, avoid spending much time micro-optimising:

- `SphereVsSphereLinear`
- `SphereVsSphereAngular`
- pair-only persistent cache lookup
- bounding-radius mixed contact response
- direct position projection paths

These paths are likely to be deleted or heavily changed if the solver is made more Catto-aligned.

## Validation Guidance For Future Code Changes

For this Markdown audit:

```bat
tools\validate_fast.bat
```

For implementation work:

| Change | Validation |
|--------|------------|
| Solver row/body cache changes | `tools\validate_physics.bat` and `tools\validate_perf.bat` |
| `SkullbonezGameModelCollection*` changes | Prefer `tools\validate_full.bat` because repo mapping includes renderer and perf coverage |
| Contact manifold or broadphase changes | `tools\validate_physics.bat` and `tools\validate_perf.bat` |
| Removing roll alignment or changing orientation behavior | `tools\validate_physics.bat` plus renderer validation if visual baselines move |

## Bottom Line

The right optimisation target is not the current object-object solver exactly as written. The right
target is a unified Catto-style contact pipeline with:

- real contact manifolds,
- stable contact identifiers,
- cached solver body state,
- consistent world inverse inertia,
- one friction model,
- one PGS kernel,
- diagnostics that prove sleeping and visual shims are not hiding solver errors.

Once that is in place, the hot loop optimisation work becomes much safer and more durable.
