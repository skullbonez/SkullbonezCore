# Scene-Sized Store Capacity Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `scene-sized-store-capacity` SC0-SC7

## Outcome

The eight-task campaign is complete. Dense Physics storage is committed from
the exact scene body, collider-kind, pair, contact, joint, and diagnostic
quantities before mutation. All retained rows use fail-loud
`PhysicsFixedList` backing, and no fixed-tick path can grow them.

The final independent review found one real closure blocker: the two
`PhysicsExternalForceStage` rows had been converted to fixed lists without a
scene-load capacity commit. An active fixed-body release would therefore fail
at capacity zero, and their memory function incorrectly reported zero bytes.
The stage now reserves both rows through `PhysicsWorld`'s existing body-capacity
seam, reports their committed bytes, and has focused active-release and owner
inventory tests. The remediation recheck returned clear.

No baseline, golden, schema, configuration, replay reserve-owner policy, or
physics result changed.

## Final Census

SC0 reported 93 retained rows. The final from-source census corrected that
historical count to 95 pre-campaign rows: two pre-existing external-force rows
were absent from SC0's declaration search. SC2 then introduced three explicit
per-kind collider-shape stores. Final source therefore has **98 retained
PhysicsFixedList-backed rows**, not 93.

| Owner | Final rows |
|---|---:|
| `PhysicsBodyStore` | 29 |
| `ColliderStore` | 12 |
| `BuoyancySystem` | 1 |
| `PhysicsEngine` | 3 |
| `PhysicsWorld` | 2 |
| `PhysicsExternalForceStage` | 2 |
| `PhysicsSleepController` | 20 |
| `PhysicsNarrowphaseStage` | 7 |
| `PhysicsContactSolverStage` | 10 |
| `PhysicsBroadphaseStage` | 5 |
| `PhysicsForceStage` | 2 |
| `PhysicsTerrainStage` | 2 |
| `PhysicsStepDiagnostics` | 3 |
| **Total** | **98** |

There are zero retained `std::vector` members in the target Physics owners.
The final raw source scan finds 91 `reserve` / `resize` / `assign` sites under
Physics; these include local and repeated operations and are not a store count.
The fixed-tick path contains no capacity-changing call.

## Resident Payload Measurement

The comparable baseline is the complete retained payload owned by the top-level
Physics stores before the campaign: **114,095,732 bytes**. It includes body and
collider identity rows, buoyancy, authored engine rows, and all world/stage
rows. Figures below are committed payload bytes from the final store accounting;
they exclude allocator metadata and small control objects.

| Store owner | Before | 200 bodies | 2,000 bodies | Regression (20 bodies) |
|---|---:|---:|---:|---:|
| `PhysicsBodyStore` | 1,638,400 | 40,000 | 400,000 | 4,000 |
| `ColliderStore` | 59,686,912 | 31,600 | 316,000 | 3,180 |
| `BuoyancySystem` | 163,840 | 4,000 | 40,000 | 400 |
| `PhysicsEngine` retained rows | 29,314,304 | 1,463,200 | 14,632,000 | 146,320 |
| `PhysicsWorld` and stages | 23,292,276 | 10,947,920 | 25,991,656 | 9,192,740 |
| **Total** | **114,095,732** | **12,486,720** | **41,379,656** | **9,346,640** |
| **Reduction** | — | **101,609,012 (89.06%)** | **72,716,076 (63.73%)** | **104,749,092 (91.81%)** |

The 200-body collider payload falls from 59,686,912 bytes to 31,600 bytes; a
sphere no longer carries an inline convex-hull payload. The separate memory-dump
JSON categories are deliberately narrower diagnostic subsets:

| Dump | Body subset | Collider subset | World total | Debug/broadphase subset |
|---|---:|---:|---:|---:|
| 200 bodies | 18,400 | 22,400 | 10,947,920 | 11,379,416 |
| 2,000 bodies | 184,000 | 224,000 | 25,991,656 | 14,959,616 |
| Regression | 1,840 | 2,240 | 9,192,740 | 11,021,396 |

`debug_and_broadphase` is a subset of the world total and must not be added to
it. The historical 574,976,000-byte private working-set sample is corroborating
process evidence only: it was taken at a different frame and body count, so it
is not presented as a direct resident-payload comparison.

## Prediction Parity

The accepted 200-body full-reveal scenario reports the private prediction-engine
request through `RunReplayPredictionSimulationState::predictionEngine`:

| Measurement | Bytes | MiB |
|---|---:|---:|
| Pre-campaign request | 171,278,688 | 163.344 |
| Final request | 30,467,508 | 29.056 |
| Reduction | 140,811,180 | 134.288 |

That is an **82.21% reduction**. The final request uses 11.35% of the unchanged
256 MiB registered hard cap. The authoritative visual run completed exactly one
prediction generation and retained the complete 2,401-tick visual, causal,
artifact, and determinism oracle.

## Allocation And Governance

- Allocation-policy self-test: pass.
- Repository allocation scan: 462 files, 35 direct-heap findings, 85 dynamic
  STL-member findings, 612 STL-growth findings, zero allowlist errors.
- Strict two-generation Replay probe: pass with exactly two prediction
  generations, `gameplay_violations=0`, and `policy_violations=0`.
- The probe exposed a stale wrapper check that accepted only one of the engine's
  two valid inclusive completion counts. Its final assertion now accepts 180 or
  181 frames while still requiring the frame-180 assertion and exactly two
  generations.
- Aggregate inventory: 1,205 candidates, 11 invariant-signalled, 10 signalled
  for ruling, 10 ruled, zero unruled.
- Extraction inventory: one finding, the existing ruled
  `WorkerPool::indexFn` retain; zero unruled.
- Wide-signature threshold-7 inventory: 387 rows, pass.

## Independent Review

The required hostile ownership review ran twice because the first pass found
the external-force blocker.

| Review | Result | Prompt chars | Elapsed | Follow-up |
|---|---|---:|---:|---|
| `scene-sized-store-capacity-duck-01` | Blocked | 5,093 | ~4 min | Required |
| `scene-sized-store-capacity-duck-02` | Clear | 671 | <1 min | None |

The clear recheck verified the single `PhysicsWorld` reserve authority, both
external-force stage commits, truthful retained-byte accounting, and the active
fixed-body release test. It found no new bag, callback, secondary authority, or
allocation path. Tokens are unavailable from the reviewer transport.

Ownership answers:

1. Aggregate ownership: pass; no capacity aggregate or second transaction was
   introduced.
2. Capability slices: pass; concrete store owners retain their own capacity and
   memory queries.
3. Extraction scars: pass; the inventory remains fully ruled.
4. Rename evasion: pass; no context/service/operand wrapper was introduced.
5. False claims: pass after remediation; the external-force zero-byte claim was
   replaced by the actual committed-byte sum.

## Comment Audit

The campaign touched 106 source, test, and validation-tool files from
`86dc3302` through final source. An automated learning-header audit found zero
missing `File`, `Purpose`, `Summary`, `Glossary`, or `Related` sections.
Ownership, capacity, fixed-tick, and prediction claims were reconciled against
the final implementation; every repository-relative `Related` path resolves.
Checked: 106. Deferred: 0. Unchecked: none.

## Validation

- Focused active fixed-body external-force release: pass, 1 case / 5 assertions.
- Focused monotonic capacity-owner inventory: pass, 1 case / 4,584 assertions.
- Debug build: pass, zero warnings and errors.
- `validate_physics.bat`: pass; 44,401-line regression CSV byte-exact.
- `validate_physics_deep.bat`: pass; deep Physics artifacts exact.
- `validate_perf.bat`: complete; absolute performance budgets pass.
- `validate_full.bat`: default gate passed, including coverage, Runtime/UI,
  DX12, and byte-exact Physics.
- `validate_replay_visual_fidelity.bat`: pass; one engine, one prediction
  generation, all false-pass controls detected.
- `validate_replay_allocation_policy.bat`: pass after the stale completion-count
  assertion was corrected.
- `validate_format.bat`: pass; 570 source files and 318 headers clean, zero dead
  `Related` paths.
- `git diff --check`: pass.

SC0-SC7 are complete at 8/8. The next unblocked plan is
`store-capacity-memory-reporting`, which consumes these runtime capacities and
must not recreate a second capacity authority.
