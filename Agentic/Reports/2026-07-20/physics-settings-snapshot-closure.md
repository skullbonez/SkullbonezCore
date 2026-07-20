# Physics Settings Snapshot Closure

Date: 2026-07-20
Branch: `nightrunner-20th-july`
Plan: `physics-settings-snapshot` (C0-C3, 4/4)

## Outcome

The plan is complete. `PhysicsEngine::ApplyRuntimeConfig` is the sole cold
Core-to-Physics stamp boundary. It stores one 27-field
`PhysicsRuntimeSettings` value composed from eight Physics-domain sub-values;
fixed-step Physics code consumes that snapshot or narrower values and no longer
names `EngineConfig`, `Core/Config.h`, `PhysicsExecutionConfig`, or
`PhysicsSleepConfig`.

The snapshot contains only simulation material, body, solver, terrain, sleep,
broadphase, execution, and gravity values. It owns no service, callback,
pointer, reference, dynamic storage, or compatibility reach-back. The omitted
`physicsMaterial.spinFrictionCoeff` remains omitted because no Physics owner
consumes it.

Retained implementation commits:

- `b2020145` — inventory all 27 provenance fields and existing transforms.
- `7bbd1ddc` — introduce and thread the Physics-owned settings snapshot.
- `0e2f1530` — add exhaustive field-faithfulness and initial clamp tests.

## Exact Provenance And Edge Proofs

The final source reconciliation found exactly 27 snapshot scalar/switch fields,
27 assignments in `PhysicsEngine::RuntimeSettingsFromConfig`, and 27 mapping
assertions in `TestPhysicsStageState.cpp`. Default, distinct custom, and seven
one-hot execution-switch cases pin every source edge; a raw
`PhysicsRuntimeSettings()` value is also checked against default
`EngineConfig` so pre-stamp owner defaults cannot drift.

The closure-tip negative search:

```text
rg -n "EngineConfig|Core/Config\.h|PhysicsExecutionConfig|PhysicsSleepConfig" SkullbonezSource/Physics
```

returns only the forward declaration and cold `ApplyRuntimeConfig` /
`RuntimeSettingsFromConfig` declarations and definitions in
`PhysicsEngine.h/.cpp`, plus the converter's single `Core/Config.h` include.
No fixed-step signature or stage/solver file retains a Core-config edge.

## Clamp Semantics

The first independent review found that the initial terrain-only C2 probe could
pass without exercising object solver slop, Baumgarte, or position-correction
bounds. It also found that raw snapshot defaults were not directly checked and
shared Boolean patterns could hide swapped execution mappings. Closure stopped
for those findings.

The correction collects the historical contact guards into
`PersistentContactSolver::ResolveStepPolicy`, once per solve. Solver code now
consumes the resolved values for object slop/Baumgarte, `[0,1]` position
correction, terrain slop/Baumgarte, non-negative maximum bias, and at-least-one
iteration. The bounds are unchanged from their former inline expressions, and
direct tests pin invalid lower/upper values plus valid passthrough. Sleep keeps
its distinct cold `[0,255]` and fixed-step `[1,255]` frame semantics;
broadphase, friction, restitution/contact, rolling-friction, and gravity
transforms remain exact field substitutions at their prior use sites.

Three intermediate strengthened-test runs failed while the masked fixture was
being replaced; the final typed-policy test is the retained proof. No production
behavior or authored/baseline value changed during that diagnosis.

## Comment Quality

All four source-bearing files touched during C3 were inspected against
`Agentic/Reference/comment-style-guide.md`: `PersistentContactSolver.h/.cpp`,
`TestPersistentContactSolver.cpp`, and `TestPhysicsStageState.cpp`. Four files
are checked, zero are deferred. Learning headers and nearby invariant/API
comments explain step-policy vocabulary, single normalization ownership,
pre-stamp defaults, and byte-exact determinism risk.

Checklist record: this report's Comment Quality section; checked 4, deferred 0,
unchecked 0.

## Independent Review

One independent read-only rubber-duck review covered the C0 provenance table,
C1 production change, C2 tests, final source, exact searches, and validation.
Its first pass reported three blocking test-confidence findings: inactive
terrain-only solver inputs, untested raw defaults, and non-unique Boolean
patterns. After the fixes, the same reviewer reported no blocking or
non-blocking issues.

The final review confirmed the 27-field record is a compact domain value rather
than a catch-all bag, all historical bounds are preserved at one authoritative
per-solve seam, every consumer uses that seam, raw defaults and Boolean
provenance are uniquely pinned, and no allocation, service, callback,
inheritance, or cross-layer authority was introduced.

## Validation

The desktop tool surface could not expose a separate visible console, so all
output was mirrored under `TestOutput/logs/`.

- `tools\validate_tests.bat` after review fixes — 12.59s; 328/328 cases and
  61,341/61,341 assertions passed.
- `tools\validate_physics.bat` — 79.13s; zero warnings/errors and the
  44,401-line varied-scene CSV matched byte-for-byte.
- `tools\validate_perf.bat` — 108.54s; allocation checks, absolute budgets,
  DX12 comparison, and physics-benchmark comparison passed with no regression.
- `tools\validate_full.bat` — 140.74s; 718/718 production and 96/96 test
  project/filter rows, all CPU/coverage and five runtime lanes, zero DX12
  validation errors, accepted images, and byte-exact physics passed.

Logs:

- `TestOutput/logs/c3_review_fixes_validate_tests_4.log`
- `TestOutput/logs/c3_validate_physics.log`
- `TestOutput/logs/c3_validate_perf.log`
- `TestOutput/logs/c3_validate_full.log`

No baseline, golden, scene, shader, screenshot, replay artifact, authored data,
or config schema changed.

## Handoff

The completed four-task plan leaves the active/future ledger under inventory
rule 4, reducing the denominator from 44 to 40. Start
`run-execute-deaccretion` X0 next.
