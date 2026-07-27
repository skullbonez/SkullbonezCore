# Store Capacity Memory Reporting Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `store-capacity-memory-reporting` MR0-MR3

## Outcome

The four-task campaign is complete. All 95 non-Debug / 98 Debug retained
Physics stores are registered reserve owners with concrete sizing reasons.
Each canonical store publishes capacity, live count, per-scene high-water, and
resident bytes into the allocator's fixed registry. The Memory tab renders a
detached resident-byte-descending table, and scene unload/process end emit the
same allocation-free rows for automation.

The required independent review found and then closed a real authority defect:
same-name list instances shared one row but could overwrite or clear each
other. Each row now has one allocator-issued canonical publisher token.
Copies and Replay clones are silent, move construction transfers only after
element construction succeeds, same-owner move assignment transfers authority,
and noncanonical destruction cannot mutate the row. Focused regressions cover
different-capacity clones, throwing moves, aggregate-compatible move
assignment, and a real Replay-phase copy.

No baseline, golden, schema, configuration, Replay growth privilege, or
physics result changed.

## Three-Scene Capacity Witness

The final Profile/DX12 process ran:

1. `physics_scale_200.scene.json`
2. `physics_scale_2000.scene.json`
3. `physics_regression_solver.scene.json` (20 bodies: 15 balls + 5 boxes)

The scale fixtures each perform two configured perf passes, so each has two
unload sections. All five emitted sections declare and contain exactly 95 rows
in resident-byte-descending order. The table below uses the last section for
each scene.

| Scene | End status | Rows | Resident bytes | MiB | Rows at 100% | Rows below 25% | Nonzero-capacity rows at zero peak |
|---|---|---:|---:|---:|---:|---:|---:|
| 200 bodies | `scene_unload` | 95 | 3,907,752 | 3.727 | 61 | 31 | 21 |
| 2,000 bodies | `scene_unload` | 95 | 32,800,688 | 31.281 | 62 | 29 | 21 |
| Regression (20 bodies) | `process_end` | 95 | 32,800,808 | 31.281 | 12 | 81 | 19 |

Selected largest and decision-relevant rows are shown as
`committed capacity / session high-water`:

| Owner | 200 | 2,000 | Regression | Regression resident bytes | Sizing rule |
|---|---:|---:|---:|---:|---|
| `PhysicsEngine.m_authoredBodyDescs` | 200 / 200 | 2,000 / 2,000 | 2,000 / 20 | 14,608,000 | Exact scene body count |
| `PhysicsContactSolverStage.persistentContacts` | 4,800 / 21 | 48,000 / 360 | 48,000 / 13 | 7,680,000 | Four manifold points per candidate pair plus eight terrain points per scene body |
| `PhysicsStepDiagnostics.physicsDebugContacts` | 4,800 / 21 | 48,000 / 360 | 48,000 / 13 | 3,840,000 | Same bounded contact formula |
| `PhysicsForceStage.m_mutualGravityPairForces` | 19,900 / 0 | 130,816 / 0 | 130,816 / 0 | 1,569,792 | Pair count for the first `min(body count, 512)` bodies |
| `PhysicsContactSolverStage.persistentContactCache` | 4,800 / 21 | 48,000 / 360 | 48,000 / 13 | 1,152,000 | Same bounded contact formula |
| `PhysicsNarrowphaseStage.events` | 800 / 0 | 8,000 / 0 | 8,000 / 0 | 832,000 | Minimum of body-pair count and candidate-pair ceiling |
| `PhysicsTerrainStage.contactManifolds` | 200 / 18 | 2,000 / 187 | 2,000 / 6 | 640,000 | Exact scene body count |
| `PhysicsSleepController.m_sleepSupportEdges` | 19,900 / 2 | 32,768 / 136 | 32,768 / 1 | 262,144 | Minimum of body-pair count and candidate-pair ceiling |
| `PhysicsContactSolverStage.pipelineRecords` | 4,096 / 506 | 4,096 / 3,896 | 4,096 / 219 | 229,376 | Fixed 4,096-record pipeline trace ceiling |
| `PhysicsStepDiagnostics.physicsPipelineTrace` | 4,096 / 728 | 4,096 / 4,096 | 4,096 / 244 | 229,376 | Fixed 4,096-record pipeline trace ceiling |
| `ColliderStore.colliders` | 200 / 200 | 2,000 / 2,000 | 2,000 / 20 | 160,000 | Exact scene collider count |
| `PhysicsBodyStore.bodies` | 200 / 200 | 2,000 / 2,000 | 2,000 / 20 | 144,000 | Exact scene body count |

The final raw stdout is
`TestOutput/validation/store_capacity_mr3_final_stdout.log`, SHA-256
`20705B78BCF0AD9C8F499D6D7BB0E0952BAD8C35E516A60A02FA8A8F6E953BBB`.
The executable exited zero with empty stderr.

## Capacity Handoff

The evidence distinguishes retained high-water storage from an incorrect
sizing rule:

- The authored-body, collider, and body rows are exact and reached 100% in the
  2,000-body scene. Their 2,000-entry backing remains resident in the later
  20-body scene because process-lifetime backing grows monotonically. Reducing
  that 14.9 MiB class requires an explicit shrink/recycle policy, not a smaller
  scene sizing formula.
- The three large persistent-contact rows use only 360 of 48,000 entries at
  2,000 bodies and retain 12,672,000 bytes together. They are the strongest
  candidate for a future evidence-driven bound, but require collision-heavy
  fixtures before changing the correctness reserve.
- `m_mutualGravityPairForces` retains 1,569,792 bytes with zero peak in all
  three fixtures. A mutual-gravity-enabled witness must decide whether the
  pair formula is necessary or the backing should be conditional.
- `PhysicsNarrowphaseStage.events` and the support-edge rows are similarly
  conservative in these fixtures; a dense-contact fixture is required before
  reducing their pair-derived bounds.
- The 4,096-record `physicsPipelineTrace` reaches 100% at 2,000 bodies. It must
  not be reduced from this evidence.

The six largest regression rows account for 29,681,792 bytes, or 90.49% of the
retained payload. Capacity corrections remain a future owner decision, as the
plan specified.

## Allocation And Authority

- Warmed unload logging under a guarded `SteadyGameplay` phase: zero allocation
  violations.
- Warmed Memory-tab drawing under a guarded `Render` phase: zero allocation
  violations.
- Final performance allocation guard: 339,409 total allocations during the
  full run, `gameplay_violations=0`.
- Allocation-policy checker self-test: pass.
- Repository allocation scan: 462 files, 35 direct-heap findings, 85 dynamic
  STL-member findings, 612 STL-growth findings, zero allowlist errors.
- All 95 production rows use 13 concrete count/formula reasons. Independent
  review found no reason that merely restates its owner name.
- Canonical publisher ownership is allocator-token-based. Copy construction
  never claims, move construction transfers after success, same-owner move
  assignment hands off to a silent destination, and destruction releases only
  the matching token.

## UI Visual QA

Screenshot-driven review used the scripted 200-body Memory-tab scene and the
repository UI validator. The first capture exposed header bleed and
owner-column crowding. The table was clipped to fully visible rows, the panel
background was bounded to content, and the owner label width was increased.
The final capture,
`TestOutput/validation/mr2_capacity_memory.bmp`, shows sorted real capacity
values without overlap or bleed. The scrolled table measured 261/2,048 draw
commands and 1,453/16,384 text bytes with no overflow.

## Independent Review

The required hostile review ran three passes:

| Pass | Result | Material finding |
|---|---|---|
| Initial | Blocked | Same-name instances could overwrite or zero one shared row; Replay suppression lacked direct evidence |
| Remediation check | Blocked | Throwing move could orphan a token, aggregate move handoff was incomplete, and allocation probes used unguarded phases |
| Final recheck | Clear | Delayed/explicit token transfer, clone/Replay/throwing-move tests, and guarded allocation probes close the findings |

Residual risk is limited to the stated first-publisher lifetime invariant.
Production's live Physics engine outlives ordinary silent copies and Replay
clones. The final reviewer found no blocking issue.

## Comment Audit

MR0-MR3 touched 32 source/test files from `108309db` through final source. All
32 contain `File`, `Purpose`, `Summary`, `Glossary`, and `Related` learning
headers. Capacity-session, detached-snapshot, publisher-token, copy/move, and
allocation claims were reconciled against the final implementation. The
repository format gate verified all 1,516 repository-relative `Related` paths.
Checked: 32. Deferred: 0. Unchecked: none.

## Validation

- Focused `PhysicsFixedList`: pass, 8 cases / 94 assertions.
- Focused Memory capacity table: pass, 1 case / 11 assertions.
- Unit umbrella: pass, 415 cases / 2,409,555 assertions.
- Debug and Profile builds: pass, zero warnings and errors.
- `validate_full.bat`: default gate passed, including format, ownership,
  coverage, Runtime/UI, DX12, and 44,401-line byte-exact Physics validation.
- `validate_perf.bat`: complete; absolute budgets and both regression reports
  pass. `Frame/Render` is -2.4% in DX12 and +0.3% in Physics Bench.
- UI boundary: pass; 11 detached production surfaces without Runtime/Rendering.
- Dependency graph: 27 include rules, one content rule, one project rule, zero
  findings.
- Three-scene witness: exit 0, empty stderr, five sorted 95-row sections.
- Format: 570 source files and 318 headers clean.
- `git diff --check`: pass.

MR0-MR3 are complete at 4/4. The next unblocked live plan is
`ceremonial-aggregate-elimination`.
