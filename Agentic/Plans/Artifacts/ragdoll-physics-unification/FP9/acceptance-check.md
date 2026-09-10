# FP8 and FP9 acceptance check

Final required validation passes on 2026-09-11. Full05 exits 0 in 1317.204 s:
unchanged Physics CSV, 134-source / 1188-context source design, all six CPU
lanes, 1,027 Profile tests / 3,483,587 assertions, Automation in 438.419 s,
and DX12 in 13.667 s against the accepted screenshots. Exclusive perf07 exits
0 in 102.502 s on rebuilt Profile 2386e9e3, including relative and absolute
budgets, native allocation checks and structural checks. Mapped replay passes
in 370.488 s on Automation 6dbb35b9. The final 4ec3be56 relink changes only COFF
and debug timestamps plus CodeView PDB age; every other executable byte matches.
automation-relink-equivalence.json records that proof. terminal-validation.json
binds the final logs, producers and performance artifacts.

The optional frame-spike diagnostic exits 1 because its recorded
predictionFullHorizonComplete assertion is false. The full script explicitly
classifies this diagnostic as informational; it produced no usable spike
measurement. This failure is retained and is not reported as a diagnostic pass.

FP8 is accepted at 9/10; FP9 acceptance and plan deletion follow in the closure commit.

## FP8 implementation and boundaries

| Requirement | Concrete coverage | Status |
|---|---|---|
| S1: derive articulation from Physics-owned joint membership | PhysicsMotionEligibilityStage transient articulation/path list; TestPhysicsStageState valid-joint restore/removal test | Implemented; current full05 CPU pass |
| S2: reuse linear/angular bounds for conservative candidate coverage | ConvexMotionBounds and cached motion eligibility; angular blade broadphase fixture | Implemented; current full05 CPU pass |
| S3: closest points, normal, signed gap and angular closing bound | ConvexDistance and SpeculativeContacts; distant thin-wall/leg and rotated-pair geometry tests | Implemented; current full05 CPU pass |
| S4-S5: strict closing threshold, unilateral gap braking | TestPersistentContactSolver exact-arrival/separating test and closing-gap impulse test | Implemented; current full05 CPU pass |
| S6: no separated friction or bounce; actual contact restores both | Closing-gap no-friction/bounce/support/release and touching-friction/restitution fixtures | Implemented; current full05 CPU pass |
| S7/A2: one fixed boundary for the whole articulation | TestPhysicsApi fast linked translating limbs and rotating boxes against static/dynamic walls | Implemented; no ragdoll microsteps |
| S8: identity, ordered rows, warm state, restore and replay | TestDeterminism predictive articulation worker/restore/topology fixture; full ordered pipeline/contact/manifold/joint comparisons | Implemented; current full05 CPU pass |
| H1/A3: no near-miss, grazing or mid-air collision | Grazing-plane rejection, exact arrival, out-of-orbit wall/terrain and airborne-tip tests | Covered; current full05 CPU pass |
| H2/A1: tested fast limbs do not cross thin static/dynamic walls | TestPhysicsApi translation/rotation wall fixtures and old/current reveal first-divergence proof | Covered by focused tests and archived behavior evidence |
| H3-H5: contact-only friction, unrelated islands, deterministic angular candidates | Touching-friction fixture, same-tick release/unrelated island fixture, exact candidate and collision-cell comparisons | Covered; current full05 CPU pass |
| A3: long-rest behavior observed honestly | long-rest-current.json: 100 seconds, all 46 supported, both end at 10 asleep, repeated wakes remain | Evidence present; reliable full-pile sleep is not claimed |
| A4: no exact arbitrary-hull rotational TOI claim | Bounded conservative angular reach and speculative rows; no exact rotational TOI implementation | Scope retained |

Full05 passes all six CPU lanes in 146.330 seconds, including 1,027 Profile
cases / 3,483,587 assertions and diagnostic lifetime coverage. Automation passes
in 438.419 seconds on current C++.

## FP9 deterministic and measured behavior

| Requirement | Concrete evidence | Status |
|---|---|---|
| D1: clean repeated processes and 0/1/4 workers | native-workers-allocation-clean.json, b9df producer, 360 ticks, w0/repeat/w1/w4 | Exact BODY/PRES/HASH/SCHK comparisons pass; allocations/exits pass |
| D2: candidates, rows, separation, impulses, bodies, joints, sleep and replay | CheckEngineWorkerDeterministicStateEqual compares candidate pairs, cell keys, eligibility, body/sleep stores, contact/manifold/joint order, pipeline bytes and production solver hash | Direct unit coverage complements native serialized-state comparison |
| D3: thresholds, grazing, touching, restore, topology and cancellation/generations | Predictive worker fixture plus prediction-generations-current.json | Published target 6 bytes repeat across generations 1/4/6 after cancellation 3 and target 16 generation 5 |
| D4: byte equality rather than epsilon-only proof | Native chunk-byte comparisons and memcmp-based typed unit checks; generation payload SHA-256 and exact byte counts | Exact checks present |
| AB1: diagnostic selector only, shared completed solver | physics.speculative_validation; selector-phase-current.json | Unpaused request rejected, paused selector applied; no authored gameplay option |
| AB2-AB3: same executable/inputs and alternating warm runs | sleep-ab-current.json, pile-ab-current.json, water-ab-current.json: A1/B1/A2/B2, b9df, four workers, 120 Hz, 1200 ticks, 240 warmup | A/A and B/B exact; all twelve native exits and allocation guards pass |
| AB4: cost, counters, allocations, final hashes, joint error and jitter | Per-run performance average/p95/p99/max, speculative candidate/row/eligibility counters, solver iterations, pre-correction joint error, serialized hashes, final-second RMS motion | Fields present for each workload; tunnelling benefit comes from the explicit wall fixtures and reveal proof |
| AB5/A2-A3: separate predictive CPU/memory cost and correctness benefit | performance-disposition.md and the three current A/B reports | Cost separated from FP4; no claim that disabling collision protection is a valid speed win |
| A1: exact predictive worker behavior | Native and unit deterministic coverage above | Evidence present; current full05 CPU pass |
| A4: current Physics gate | Full05 log, VALIDATE_PHYSICS: ALL PASSED, accepted SHA 50bca7c0f2c420832c4fd99b1812f4db48d88cfadb4d475622a3d3bd3465a1c1 | Pass, unchanged CSV |
| A4: mapped replay visual/causal gate after governed transition | predictive-contacts-2ac5f033 mapped-validation evidence, 384.578 s | Pass on current producer: attempt 02 exits 0 in 370.488 s; 2401 ticks, durable replay and all negative controls pass |
| A4: allocation gate | Exact static policy 681 sources / 925 growth findings / zero errors; native guards | Pass; no new gameplay growth privilege |
| A4: full performance gate | Reviewed timing transition performance-references-8f40ec6e; current perf07 exits 0 in 102.502 s | Pass; exact old/new producers retained, thresholds unchanged |
| A4: full-plan gate | Full05 passes Physics, CPU, Automation and accepted DX12 references | Pass, 1317.204 s; optional spike diagnostic failure retained |
| A5: final cost/stability decision | performance-disposition.md records reversible provisional retention for fast-impact benefit and limits | Retain predictive contacts for demonstrated fast-impact benefit; reliable pile sleep remains unresolved |

## User-requested preservation and handoff

The approved replay transition retains exact old/new producers, goldens,
dependency scans, first-divergence evidence and mapped results in a new append-only
FP8 bundle. The prior Physics CSV remains untouched. Old Debug/Profile/Automation
executables from task startup and subsequent diagnostic producers remain archived.
No existing bundle has been rewritten.

reference-provenance.json verifies the exact prior first-party producer and
matching hashes for both timing references. The original producer of the older
UI screenshot references is unavailable. The user explicitly accepted the
three reviewed replacements with that disclosed preservation exception; exact
old/new PNGs and the available current producer remain archived. The source, baselines and exact retained producers land together in the FP8 commit.
The live ledger's earlier unrelated open session remains untouched; its reported
telemetry/model limitation is still a handoff requirement.
