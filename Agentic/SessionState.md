# SkullbonezCore Session State

Date: 2026-08-01

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-1st-AUG-26`; push target `origin/nightrunner-1st-AUG-26`. |
| Current baseline | Main tip `b314480e`; PR #141 merged the 30 July Night Runner branch. |
| Current objective | Execute Vector Frame Contract Closure VF0's complete frame/caller census and pre-change artifact prediction; Angular Impulse Frame Correctness AI4 is owner-accepted and closed. |
| Active/future progress | 16/21 (76%). Solver Diagnostic Hot-Path Cost is 4/4; Runtime Contract Hygiene 3/3; Engine Glossary Consolidation 4/4; Angular Impulse Frame Correctness 5/5; Vector Frame Contract Closure 0/5. |
| Build configuration parity closure | BP0-BP5 are complete. The five-project census reports 1,640 compile rows, zero dropped inheritance, 122 exact intentional-difference fingerprints, and zero diagnostics. The four shared JSON TUs have no unvalidated external accessor; tests now compile them with production `JSON_NOEXCEPTION` semantics. All seven third-party overrides inherit the FP contract. Fast, direct checker, CPU, and full gates pass byte-exact physics; comment audit is 2/2 and independent review is clear. Evidence: `Agentic/Reports/2026-07-30/build-configuration-parity-closure.md`. |
| Maths surface reachability closure | MR0-MR3 are complete. `GeometricMath` retains only its two production-reachable operations; eight dead definitions are removed and the triangle-normal helper is internal. The corrected compiler/source inventory reports 407/407 exact repair rulings (299 no-reference, 60 test-only, 41 own-TU-only, 7 both), zero diagnostics, and a registered four-phase remediation owner. Fast, direct reachability, coverage, and full gates pass with 465 cases / 2,423,881 assertions and byte-exact Physics; comment audit is 7/7 and final review is clear. Evidence: `Agentic/Reports/2026-07-30/maths-surface-reachability-closure.md`. |
| Inverse-trig domain guards closure | TD0-TD3 are complete. One shared `ClampUnit` policy now guards every reachable inverse-trig domain that is not proven by construction; Camera uses an explicit zero-up fallback, and Matrix/Editor handle antiparallel normals without NaNs. The current reachability inventory is 407/407 exact rows (298 no-reference, 61 test-only, 41 own-TU-only, 7 both) with zero diagnostics. Focused finite-output regressions, 469 cases / 2,423,935 assertions, coverage, full validation, and 44,401-line byte-exact Physics pass; comment audit is 12/12 and final review is clear. Evidence: `Agentic/Reports/2026-07-30/inverse-trig-domain-guards-closure.md`. |
| Retirement diagnostic honesty closure | DH0-DH1 are complete. The DX12 retirement owner now retains a monotonic peak and one coherent release/fence snapshot, rejects diagnostic reset while rows remain live, and reports owner/phase/current/peak facts at exhaustion. The readback audit removed its structurally tautological high-water. Focused coverage passes 2 cases / 44 assertions; the complete suite passes 471 cases / 2,423,979 assertions. DX12 validation reports zero errors and clean captures, graphics stress exits 0 after 61.093 seconds, full validation and 44,401-line byte-exact Physics pass, comment audit is 5/5, and final review is clear. Evidence: `Agentic/Reports/2026-07-30/retirement-diagnostic-honesty-closure.md`. |
| Unreachable symbol remediation closure | UR0-UR3 are complete. The mandatory Automation/Debug/Profile compiler graph corrects the provisional 407 rows to 246 adjudicated rows; 181 unreachable ordinary functions are removed and 79 exact retain-owner rulings remain with zero diagnostics. Automation-only Director, editor, terrain, and launcher APIs were restored after the full gate proved their roots. Fast, coverage, all six CPU lanes, Automation smoke, full validation, and 44,401-line byte-exact Physics pass; comment checklist is 172/172 with zero deferred and final review is clear. Evidence: `Agentic/Reports/2026-07-30/unreachable-symbol-remediation-closure.md`. |
| Solver diagnostic hot-path cost HP0 | The complete producer/consumer census finds all 16 pipeline stages live in Debug/Profile/Release, corrects the provisional record size from 44 to 56 bytes, and proves full ordered records remain required by solver Replay snapshots/hash/artifacts, prediction, SkullScope, and the pipeline overlay. A bounded two-run `perf_1000` trace retained 296,714 records per 180-frame run with zero saturation and 866 pipeline-only `sqrtf` calls; the complete two-pass Profile scene measured `SolveRows` at 0.120002 ms mean. Evidence: `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp0-census.md`. |
| Solver diagnostic hot-path cost HP1 | One Physics-owned recorder now preserves the exact saturated 4,096-event count while retaining ordered payloads only for Replay capture, the pipeline overlay, Debug SkullScope, and default direct/prediction engines. The original reserve identity and 229,376-byte backing remain unchanged. Focused Profile/Debug mode equality and field-faithfulness coverage passes; `validate_fast` passes 455 cases / 2,423,400 assertions with clean dependency, ownership, and reachability inventories. Comment audit is 13/13. Evidence: `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp1-recorder.md`. |
| Solver diagnostic hot-path cost HP2 | Count-only execution now constructs no pipeline payload: stage mode dispatch is compile-time or hoisted, narrowphase/terrain payload optionals stay disengaged, counts submit in stage batches, and the solver omits trace-only position loads and its diagnostic `sqrtf`. Automation/Debug/Profile reachability, 456 cases / 2,424,707 assertions, `validate_fast`, 17/17 comment audit, and independent re-review pass. Evidence: `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp2-payload.md`. |
| Solver diagnostic hot-path cost closure | Physics and every approved Replay visual/causal value remain exact without a golden refresh; the enabled 348,925,625-byte trace matches HP0 byte-for-byte. Allocation policy and both bounded 4,096-row full-record reserves remain unchanged. The same Profile workload improves mean `Frame/Physics` 6.71%, persistent contacts 33.65%, and `SolveRows` 40.90%. Tests, Physics, Replay visual, allocation, performance, and full validation pass. Evidence: `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-closure.md`. |
| Runtime contract hygiene CH0 | Direct frame-phase boundaries are status-free, `RequestPhaseFailure` is the sole frame failure channel and rejects success through Lane F, and a focused 1-case / 5-assertion regression proves message exit code zero preserves phase failure. Profile compilation, the complete repository gate, a 7/7 comment audit, exact current-body ownership rulings, and independent review pass. Evidence: `Agentic/Reports/2026-07-31/runtime-contract-hygiene-ch0-exit-contract.md`. |
| Runtime contract hygiene CH1 | `Quaternion.h` now documents only live methods and states the normalized world-axis/radians contract for `RotateAboutAxis`; a Debug-only assertion enforces it without changing Release production arithmetic. The 22-site audit found production callers valid and corrected two test helpers covering four non-unit fixture axes. Formatting, Debug compilation, 28 cases / 2,302 assertions, and a 4/4 comment audit pass. Evidence: `Agentic/Reports/2026-07-31/runtime-contract-hygiene-ch1-quaternion.md`. |
| Runtime contract hygiene closure | CH0-CH2 are complete. `PhysicsFixedList` now rejects element types that cannot move without throwing, preserves its trivial byte-transfer path, and directly relocates non-trivial rows under the compile-time contract. Engine source contains zero throw expressions. Focused relocation passes 1 case / 12 assertions; all 457 tests / 2,424,712 assertions, byte-exact Physics, allocation policy, formatting, the 652.3-second full gate, a 2/2 comment audit, and independent review pass. Evidence: `Agentic/Reports/2026-07-31/runtime-contract-hygiene-closure.md`. |
| Engine glossary consolidation GC0 | The corrected tracked source scope is 575 files; the provisional 576 included `SkullbonezSource/AGENTS.md`. The complete term-to-file census records 2,172 definitions / 1,285 terms: 321 shared, 964 local, 264 drifted shared, and 57 exact shared. All repeated terms are classified shared, including explicit `Model row hint` and `Published prefix` rulings. The source-of-truth checklist contains 575 unique unchecked rows. This phase is documentation-only. Evidence: `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc0-inventory.md`. |
| Engine glossary consolidation GC1 | The shared glossary reproduces all 321 GC0 owner adjudications exactly. The new repeatable inventory reproduces the 575-file / 2,172-definition / 1,285-term census, reports 321 multi-file terms and 264 wording drifts, and matches 321 exact current migration rulings with zero diagnostics. Comment/audit/reviewer rules and `validate_fast` now enforce the split. Direct checks, the complete 457-case / 2,424,712-assertion gate, a 2/2 tool comment audit, and independent re-review pass. Evidence: `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc1-standard.md`. |
| Engine glossary consolidation GC2 | All 1,208 source sites for the 321 shared terms are removed; all 964 local term/file pairs remain exact; and all 447 affected files cite the canonical glossary. The complete scan removes 107 filler instances, correcting the provisional 77. Strict inventory reports 964 unique local definitions with zero shared terms, drift, rulings, or diagnostics. Every non-comment source suffix is byte-identical; the 575-row checklist is 458 checked and 117 explicitly deferred to GC3. Automation refresh, 457 cases / 2,424,712 assertions, direct inventories, and independent review pass. Evidence: `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc2-pass.md`. |
| Engine glossary consolidation closure | GC0-GC3 are complete. All 117 conservative basename-led candidates are adjudicated: 83 informative clauses retain their ownership content without the filename subject and 34 templated UI headers state concrete responsibilities. Zero basename-led summaries remain across 575 files; the checklist is 575/575 with zero deferred; all 117 source suffixes are unchanged; strict inventories, Automation refresh, 457 cases / 2,424,712 assertions, post-gate direct proofs, and independent closure review pass. Evidence: `Agentic/Reports/2026-07-31/engine-glossary-consolidation-closure.md`. |
| Angular impulse frame correctness AI0 | All three torque-to-angular-velocity paths and every direct pending-impulse caller are censused. The application point is ruled a world-space center-relative offset. A focused `should_fail` test records gameplay `(-2.35, 0.96, 0.55)` versus contact `(-1.13982, 0.808092, 0.55)` for a rotated anisotropic box. The pre-change oracle predicts zero committed baseline bytes because mapped authored impulses are spheres, generated anisotropic boxes start at identity, and the launcher fixture targets a sphere. Evidence: `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai0-census.md`. |
| Angular impulse frame correctness AI1 | Mutual gravity now emits canonical compact worker prefixes and reduces one linear active-pair list without clearing or scanning the full triangular extent. Worker-count fixtures remain bit-exact, all Physics and deep-Physics artifacts remain byte-exact, and the same Profile scene improves Reduce mean 14.19%, median 15.58%, and P95 5.04%. Fast, allocation, performance, and ownership gates pass; comment audit is 3/3 and independent review is clear. Evidence: `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai1-gravity-reduce.md`. |
| Angular impulse frame correctness AI2 | One shared inertia-frame helper now serves pending gameplay, world-force, and contact impulses, and the application contract is named as a world-space center-relative offset throughout the command/store path. The rotated anisotropic cross-path case and exact isotropic sphere case pass; 453 unit cases / 2,422,921 assertions, core Physics, and deep Physics are green with no baseline refresh. Owner direction removed the redundant extra `at_rest` frame assertion because the deep gate already hashes its complete 54,001-line CSV; SHA-256 remains `0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984`. Comment audit is 9/9. Evidence: `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai2-impulse.md`. |
| Angular impulse frame correctness AI3 | The investigation-only sweep found no remaining incorrect direct inertia division and confirmed current public descriptor consumers agree on their coordinate frames. It registered Vector Frame Contract Closure for the mixed-frame anisotropic angular-drag clamp, ambiguous authored impulse-offset schema and sole absolute-position outlier, test-only `VectorReflect` convention, and explicit public frame documentation. No source or artifact changed. Evidence: `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai3-convention-sweep.md`. |
| Angular impulse frame correctness closure | AI0-AI4 are complete with explicit owner acceptance. Unit, Physics/deep-Physics, performance, allocation, interaction, DX12 screenshot, existing-generation Replay visual/causal, all seven strict inventories, and the post-acceptance 494.8-second full gate support AI0's prediction with no committed baseline refresh. Independent review found no blocker and the owner accepted the known Replay CRLF/LF provenance limitation plus the shared-helper test-isolation caveat. Evidence: `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-closure.md`. |
| Persistent-contact convergence closure | CE0-CE3 are complete. The owner approved retaining the current stopping criterion because the fixed-capacity per-iteration trace and controlled object-only chain prove honest row-level non-convergence rather than stale accounting. Three 1,200-frame wall traces are byte-identical; focused Profile tests, all four ownership inventories, format, 465 cases / 2,423,885 assertions, byte-exact and deep Physics, performance, full validation, 9/9 comment audit, exact query regression, and independent review pass. No behavior or baseline changed. Evidence: `Agentic/Reports/2026-07-30/persistent-contact-convergence-early-out-closure.md`. |
| Validation-gate integrity closure | V0-V5 are complete at 6/6 and the TODO left the live inventory under rule 4. Exact-commit hosted run 30505659321 passes on `47a95da0` with pinned LLVM tooling, 465 cases / 2,423,885 assertions, coverage, and the full CPU umbrella. `main` strictly requires `Mandatory CPU lane (Windows hosted)`. Owner rulings: merge queues and repository-ownership changes are retired, and anything needing a graphics card is local-only validation. The GitHub DX12 workflow and runner infrastructure are removed. Evidence: `Agentic/Reports/2026-07-30/validation-gate-integrity-closure.md`. |
| Persistent-contact convergence CE0 | The post-Box wall remains at 12 iterations in 1,000/1,000 measured frames. Existing diagnostics describe 565,635 final rows but export only 23.8325% of required iteration records before the pipeline cap and omit their scalar deltas. CE1 needs one bounded per-iteration aggregate; no uncapped row trace is justified. Evidence: `Agentic/Reports/2026-07-29/persistent-contact-convergence-early-out-ce0.md`. |
| Pre-536 physics oracle restoration | Owner direction supersedes the Box Vibration And Warm-Start Integrity closure. BV1 restitution suppression, BV2 SAT hysteresis, BV3 terrain row-derived warm start, and BV5 manifold correction division are removed; all four `536e0a60` goldens are restored byte-for-byte from its parent. A fresh Debug executable produces two identical 44,401-line varied runs matching the restored oracle on every line. Evidence: `Agentic/Reports/2026-07-31/pre-536-physics-oracle-restoration.md`. |
| Validation for box vibration BV5 | One-point/four-point coverage pins one 0.16 total correction budget, a 0.04 four-point row maximum, and equal 0.08 body displacement. Persistent-contact coverage passes 12 cases / 174 assertions; all unit coverage passes 463 cases / 2,423,855 assertions. The controlled fixture remains zero-flip/one-iteration/zero-miss and repeats byte-identically. Physics/deep Physics stop at the inspected deferred transition: 35,091 varied-scene lines and 28 shooting lines differ, all bullet sweeps and three-body chaos stay exact, paired varied-scene runs match, all ten shooting targets react, only the stacking known-issue hash moves beyond BV3, and no changed CSV contains NaN/Inf. No baseline was regenerated. Evidence: `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv5.md`. |
| Validation for box vibration BV4 | The controlled fixture retains zero flips, zero cap-bound frames, one minimum iteration, zero misses, and BV2's exact 842,927-byte CSV hash across two fresh Debug runs. The wall remains at 12 minimum/average/maximum iterations for all 1,000 measured frames despite 41,901 / 580,467 misses (7.218498%) and 557,764 / 582,238 warm-started rows (95.796564%). The required separate convergence finding is queued as plan 9; BV5 remains viable. Evidence: `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv4.md`. |
| Validation for box vibration BV3 | Fixed terrain seed scales and all accumulated-impulse floors are deleted. The two-point shoreline fixture caches both rows, stays outside resting policy, holds first-frame residual vertical speed below 8.5% of input, and improves on reuse. Persistent-contact coverage passes 11 cases / 167 assertions; all unit coverage passes 462 cases / 2,423,848 assertions. Physics/deep Physics stop at the inspected deferred transition: 35,093 varied-scene lines and 28 shooting lines differ, all bullet sweeps and three-body chaos stay exact, paired varied-scene runs are identical, all ten shooting targets react, and no changed CSV contains NaN/Inf. No baseline was regenerated. Evidence: `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv3.md`. |
| Validation for box vibration BV2 | Current-tip wall frames 400-500 move from 1,604 face/edge switches and 326 reference swaps to 116 and 281; slow-topple columns improve 754/197 to 54/127. The recovered rocking oracle reproduces all 338 failures without BV2, fails 174 assertions at a 5% contact-band margin, and passes at 10%+, while the wall evidence justifies retaining 25%. The BV0/BV1 metric remains zero flips, zero cap frames, one minimum iteration, and zero cache misses; repeat CSVs are byte-identical. Six focused manifold cases / 2,064 assertions and all 461 unit cases / 2,423,809 assertions pass. Physics/deep Physics stop only at the planned cumulative 14,834-line golden mismatch while five deep outputs stay byte-exact. No baseline was regenerated. Evidence: `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv2.md`. |
| Validation for box vibration BV1 | The exact BV0 metric falls 566→0 flips, affected bricks 4→0, cap-bound frames 900→0, and minimum iterations 12→1; repeat Debug CSVs are byte-identical. A focused fresh/persistent object oracle proves cached load falls through to the exact Baumgarte bias. The one-ball terrain-only pre/post CSV is byte-identical. The initial 458-case unit gate passes; the review-added cache-reach oracle passes in the final 10-case / 128-assertion solver filter. Physics/deep Physics reach only the planned stale `physics_regression_varied.csv` mismatch while five deep outputs remain exact; no baseline was regenerated before the cumulative final Debug refresh. Evidence: `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv1.md`. |
| Validation for box vibration BV0 | `box_vibration_t0.scene.json` records 566 meaningful vertical-velocity flips across four supported bricks over frames 300-1199; all 900 frames hit the 12-iteration cap. Repeat Debug CSVs are byte-identical. Focused manifold/solver oracles, Physics, deep Physics, all 457 unit cases / 2,422,070 assertions, and the full gate pass without baseline movement. The exact query, current line/key map, cumulative oracle list, and SkullScope accounting are in `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv0-t0.md`. |
| Validation for quaternion normalization | Textbook Hamilton multiplication, active orientation matrices, scene v3, replay v5, and prediction archive v3 are live with one-time legacy migration and bitwise conjugation round-trip proof. Owner visual acceptance preceded baseline regeneration. All CSV identities and finite-state/peak-energy bounds are preserved; Replay retains 2,401 ticks and 200 causal nodes; accepted DX12 images retain 1784x961 RGB geometry. SpatialGrid's gate-exposed generated-scene reserve defect is closed with measured `8 * bodies + 1024` backing and exact fatal tests. Every mapped gate, 41/41 comment audit, and independent review pass. Evidence: `Agentic/Reports/2026-07-29/quaternion-convention-normalization-closure.md`. |
| Quaternion normalization QN3 | Scene v3, replay v5, and prediction archive v3 store canonical Hamilton components; legacy readers conjugate xyz once and replay verifies historical hashes before migration. All 23 acceptance scenes are schema v3, with structural proof that only versions/orientations moved. Migration/self-tests, 452 unit cases, scene-parser tests, the six-stage CPU umbrella, dependency preflight, and a full Profile app build pass. Comment audit is 13/13. No baseline or golden artifact changed. |
| Quaternion normalization QN2 | Matrix4 now emits the canonical active basis, and all four authored/editor Euler constructors preserve their established scene-space behavior. The child placement composition is algebraically neutral and remains unchanged. Matrix4 passes 9/9 cases / 218 assertions, Quaternion passes 15/15 / 66, and `validate_tests` passes all 452 cases. Formatting and the 5/5 QN2 comment audit pass. No baseline, golden, schema, config, or committed runtime artifact changed. |
| Quaternion normalization QN1 | Canonical Hamilton multiplication, active orientation matrices, and positive-sine world-axis updates are implemented. The focused Profile build and all 15 `Quaternion*` cases / 66 assertions pass unchanged at the behavioral layer. The broad unit gate has one planned QN2 Matrix4 conversion failure (two assertions); the remaining 451 cases pass. Formatting and the 3/3 touched-source comment audit pass. No baseline, golden, schema, config, or committed runtime artifact changed. |
| Quaternion normalization QN0 | Owner decision: proceed. Live source has 35 production orientation-matrix consumers, nine transpose consumers, zero axis-helper callers, four Euler composers plus one child-composition site, and a 23-scene migration set (22 raw-orientation scenes plus the Euler-authored buoyancy fixture). Five representation-independent characterization cases pass in the pre-change Profile binary; the `Quaternion*` filter totals 15 cases and 66 assertions. Evidence: `Agentic/Reports/2026-07-29/quaternion-convention-normalization-qn0-census.md`. No baseline, golden, schema, config, or committed runtime artifact changed. |
| Validation for runtime include closure | TUs above 200 headers fall 17 to 16, maximum closure falls 255 to 248, and `PhysicsWorld.h`/`SpatialGrid.h` reach zero non-Physics TUs. Debug/Profile rebuild samples pass at 44.860/45.358 seconds with no speedup claimed. Full, Physics, DX12, graphics-stress, and 2,401-tick replay-visual gates pass without baseline refresh. Replay reserve accounting counts world debug/broadphase storage once. Comment audit is 30/30 and final independent review is clear. Evidence: `Agentic/Reports/2026-07-29/runtime-include-closure-reduction-closure.md`. |
| Validation for runtime include closure IC2 | `PhysicsEngine` retains sole `PhysicsWorld` authority through one fixed-size owner allocation and out-of-line destruction; the pointer stays private and engine-side body iteration binds the world before looping. `PhysicsDiagnosticsView.h` is a concrete Physics value contract, not a forwarding or declaration umbrella. `UI.h`'s corrected direct census is ten tab headers: profiler remains and nine redundant direct edges are removed. Allocation/dependency scans, 788/788 project/filter items, focused Profile, final `validate_fast`, and the 7/7 IC2 comment audit pass. The allocation allowlist gained one exact PhysicsEngine owner row; no baseline, golden, config, schema, or runtime artifact changed. |
| Validation for runtime include closure IC1 | Six value-only consumers now include `PhysicsApi.h`; the other 27 classified files deliberately retain the concrete engine command/query contract. `ReplayPredictionIsolatedSimulation` destroys its incomplete `unique_ptr<PhysicsEngine>` out of line at the concrete owner. The first Profile build corrected IC0's overly literal 3/30 storage-owner classification and selected `PhysicsEngine.h -> PhysicsWorld.h`. The corrected focused Profile solution and `validate_fast` pass; fast reports 447/447 tests and 2,421,986 assertions. Two shifted aggregate-ruling site lines were refreshed as exact-current evidence. Comment audit is 8/8. No baseline, golden, config, schema, allowlist, or runtime artifact changed. |
| Runtime include closure IC0 census | Documentation and measurement only: the current 254-TU/317-header graph reproduces 17 TUs above 200, 26 above 150, 62 above 100, lower median 38, maximum 255, and all six named fan-in rows. The corrected 33-file classification is 27 concrete engine-contract users and six value-only consumers. IC2 will break `PhysicsEngine.h -> PhysicsWorld.h` without moving ownership or adding a hot-loop pointer chase. Clean full rebuilds pass in 43.066 seconds Debug x64 and 44.614 seconds Profile x64. Evidence: `Agentic/Reports/2026-07-29/runtime-include-closure-reduction-ic0-census.md`. No repository validation was required. |
| Validation for broadphase capacity closure | `SpatialGrid` measures 623,256 inline bytes in Debug/Profile/Release. At 300 admitted bodies, inline plus registered backing falls from 8,535,792 to 839,264 bytes per grid: 7,696,528 bytes saved, 90.167708%, and 10.170568x smaller. All nine owners report SceneLoad-only capacity with zero Replay growth; owning memory totals include backing exactly once. All CPU passes 447/447 tests and 2,421,986 assertions; deep Physics, performance, allocation policy, format, all ownership inventories, and full validation pass. Comment audit is 9/9 and independent review reports zero blockers. No baseline, golden, config, schema, allowlist, or runtime artifact changed. Evidence: `Agentic/Reports/2026-07-29/broadphase-capacity-right-sizing-closure.md`. |
| Validation for broadphase capacity BC2 | The remaining seven `SpatialGrid` arrays now use registered storage: exact admitted body rows, four-per-body candidate staging, and an accurately labelled fixed 4,096-row swept-overlay ceiling. Additional live admission preserves existing membership topology through grow-only suffix construction. Focused checks pass SpatialGrid 20/20 with 8,590 assertions, fixed-list/reserve 21/21, fatal contracts 1/1 with 231, the 2,000-body owner census 1/1 with 6,320, and determinism 3/3 with 30,897. Physics is byte-exact across 44,401 rows. The final unchanged idle performance run passes both lanes with process memory lower by 5.18-6.22 MiB; two earlier runs alternated isolated marginal misses between lanes and did not reproduce. Allocation policy scans 463 files with zero allowlist errors; format, all four ownership inventories, and the 7/7 touched-file comment audit pass. No baseline, golden, config, schema, allowlist, or runtime artifact changed. |
| Validation for broadphase capacity BC1 | `SpatialGrid.entries` and `pairSeen` now reserve registered backing first inside the existing SceneLoad stage boundary. Final capacities are `8 * bodies + 32` persistent rows and exact triangular pair words; the duplicated 4,096-row blanket is gone, while the retained 32-row spill covers BC3's measured oversized-shape maximum and compile-time ceilings remain. Clear/cell-size/frame transitions retain backing and high-water, and no Replay privilege exists. Focused checks pass 19/19 SpatialGrid cases with 8,545 assertions, fatal contracts 1/1 with 221, reserve allocator 20/20 with 212, determinism, and the 2,000-body owner census with 5,648 assertions. Physics remains byte-exact across 44,401 rows and performance passes. Format, complexity 40/40 at 400/6, aggregates 85/85, the one unrelated ruled extraction scar, and all 12+ signatures pass. Comment audit is 7/7 touched files. No baseline, golden, config, schema, allowlist, or runtime artifact changed. |
| Broadphase capacity BC0 census | Documentation-only: Debug, Profile, and Release all measure `SpatialGrid` as 8,535,792 bytes with 8-byte alignment. Current commitment is identical at 300, 4,000, and 8,192 admitted bodies: 8,535,792 bytes for one grid and 17,071,584 for live plus Replay prediction. Arrays targeted for registered storage are 7,913,120 bytes and the retained inline topology/state core is 622,672. The exact SceneLoad reservation chain and cold-clear/frame-begin interactions are recorded. `overlayEntries` is a fixed 4,096-row transient-work ceiling rather than an honest body-count formula. Evidence: `Agentic/Reports/2026-07-29/broadphase-capacity-right-sizing-bc0-census.md`. No source, baseline, golden, schema, config, or runtime artifact changed; no repository validation was required. |
| Validation for collision-hull closure | Canonical resolved-path plus exact authored-scale identity reduces the three acceptance scenes from 911,352 to 387,504 committed hull bytes, saving 523,848 bytes (57.4803%). The 18-run/42,120-frame matrix measures no material object-manifold timing change. Deep Physics, idle-machine performance, the single authoritative Replay visual run, and full validation pass; full reports 441/441 tests and 2,420,993 assertions with byte-exact Physics. Comment audit is 18/18 and independent review closes with zero blockers after three raw-log references were corrected. No baseline, golden, config, schema, allowlist, or committed runtime artifact changed. Evidence: `Agentic/Reports/2026-07-29/collision-hull-shape-instancing-closure.md`. |
| Collision-hull HS0 census and decisions | Documentation-only: all 145 committed scenes expand to 557 authored hull colliders in 19 nonzero scenes and 25 used normalized paths. The prior 33 figure was a raw direct-token count that normalizes to 22 direct paths; asset expansion adds three used paths. The binding identity is normalized resolved path plus exact X/Y/Z authored-scale bits, with unproved variants non-shareable. Identity rows persist until scene clear; mid-scene destruction does not refcount or compact them. Exact create/replace/destroy/reserve/rebind/Replay-clone paths and the const-consumer mutation audit are in `Agentic/Reports/2026-07-29/collision-hull-shape-instancing-hs0-census.md`. No source, baseline, golden, schema, config, or runtime artifact changed; no repository validation was required. |
| Validation for collision-hull HS1 | `ColliderStore` now shares canonical path-plus-scale hull variants, retains stable hull rows until clear, never overwrites shared geometry on replacement, and clones identity rows before replay rebinding. Editor delete/undo preserves proven identity; arbitrary transformed variants remain unique. Acceptance-scene probes commit 12/12/12, 20/20/20, and 20/20/20 capacity/live/high-water rows. A first full test run exposed a Profile-only large-return hazard that defaulted by-value box/hull variants to sphere after the descriptor grew, plus the expected one-row capacity census increase; borrowing/copying the source variant and updating the owner census repaired both. `validate_physics` remains byte-exact and `validate_tests` passes 440/440 cases with 2,420,846 assertions. Complexity passes 40/40 after re-ratifying two edited current bodies; aggregates pass 85/85; the only extraction scar is the unrelated ruled WorkerPool row; all 12+ signatures are ruled. Comment audit is 15/15 touched source files with three untouched plan-scope files deferred to HS2/HS3. No baseline, golden, schema, allowlist, config, or committed runtime artifact changed. |
| Validation for collision-hull HS2 | Direct store coverage proves normalized identity reuse, exact scale-bit distinction, conservative unique fallback, retained rows through destroy/create, and six expected rows for a mixed shared/unique set. The production Replay seed proves two hull colliders share one source row and one destination-owned cloned row that survives source destruction. Focused checks pass 44/44, 407/407, 31/31, 30/30, and 140/140 assertions. `validate_physics` remains byte-exact; `validate_tests` passes 441/441 cases and 2,420,993 assertions. Format, dependency, complexity 40/40 at 400/6, aggregates 85/85, the one unrelated ruled extraction scar, and all 12+ signature rulings pass. `TestPhysicsHandles.cpp` was re-audited; three untouched plan-scope files remain for HS3 closure review. No baseline, golden, schema, allowlist, config, or committed runtime artifact changed. |
| Validation for contact-solve closure | The guarded transaction owns all thirteen construction, solve, and post-solve phases; `Solve` is a 108-body-line/depth-3 sequencer and all eighteen profiler strings remain unchanged. `ParseAction` is a 21-body-line/depth-3 ordered dispatch over 34 parsers; all 33 non-assert bodies and 79 assertion branches are token-equivalent to the parent source. Complexity passes 40/40 at the ratified 400/6 triggers with no repair rows, aggregates pass 85/85, the only extraction scar is the unrelated ruled WorkerPool row, and every 12+ signature is ruled. All CPU, deep Physics, performance, and full gates pass; full reports 440/440 tests and 2,420,840 assertions. Physics remains byte-exact to the CS0 SHA-256 `8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387`. Comment audit is 9/9 and independent review has zero findings. No baseline, golden, config, schema, allowlist, or committed runtime artifact changed. |
| Validation for contact-solve CS3 | The guarded transaction now owns all eight solve/post-solve phase bodies and `Solve` is a 108-body-line, depth-3 sequencer. Focused Profile filters pass 8/108 persistent-contact, 4/345 object-manifold, and 3/30,897 determinism cases/assertions. All eighteen profiler strings remain unchanged. Format and ownership inventories pass: complexity 41/41 at the ratified 400/6 triggers, aggregates 85/85, zero extraction scars, and every 12+ signature ruled. Final `tools\validate_physics.bat` and `tools\validate_perf.bat` pass; the 88,802-row / 12,660,434-byte output remains byte-identical to CS0 at SHA-256 `8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387`. The first compile exposed one unused terrain-stage borrow; removing it closed the local defect without changing behavior. |
| Validation for contact-solve CS2 | The guarded transaction now owns `BodySetup`, `BuildManifolds`, `TerrainRows`, and `Precompute` phase advancement and bodies while retaining no store, span, stage, or diagnostics borrow. Focused Profile build and persistent-contact, object-manifold, and determinism filters pass. Format and ownership inventories pass: complexity 42/42 at the ratified 400/6 triggers, aggregates 85/85, zero extraction scars, and every 12+ signature ruled. Final `tools\validate_physics.bat` and `tools\validate_perf.bat` pass; the 88,802-row / 12,660,434-byte output remains byte-identical to CS0 at SHA-256 `8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387`. No baseline, golden, allowlist, or committed runtime artifact changed. |
| Validation for contact-solve CS1 | The non-copyable stage-retained transaction owns its phase cursor, solver-body working set, inverse-inertia path, and impulse application without retaining caller borrows. The focused exhaustive matrix passes 1 case / 1,602 assertions; format and all four ownership inventories pass, including function complexity at 6,317 recognized definitions and 40/40 current rulings. Final `tools\validate_perf.bat` passes in 157.9 seconds. Final `tools\validate_physics.bat` passes in 28.4 seconds, and the 88,802-row / 12,660,434-byte output remains byte-identical to CS0 at SHA-256 `8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387`. Allocation-policy iterations replaced rejected STL-growth spellings with `PhysicsFixedList::ResetDefault`; no allowlist or baseline changed. A short wrapper timeout and one immediate compiler-file collision were non-terminal infrastructure events; the unchanged gate passed after the owned processes drained. Comment audit is 5/5 touched source-bearing files. |
| Validation for contact-solve CS0 | `tools\validate_physics.bat` passed in 24.4 seconds from the final Debug executable with zero build warnings/errors; both generated 44,401-row runs matched the committed baseline. The preserved ignored two-run artifact has SHA-256 `8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387`. Census evidence records 28/28 closures, all cross-phase state, thirteen exact phase read/write sets, and the authority CS1 must own. A five-second wrapper timeout and its brief DXC DLL lock were recorded as non-terminal infrastructure events; the unchanged gate then passed. No source, baseline, golden, or committed runtime artifact changed. |
| Validation for function-complexity closure | The owner ratified 400 inclusive body lines and brace depth 6 as independent qualitative-review triggers. The current tree reports 6,285 recognized definitions and 40/40 exact current-body rulings: 38 retain-owner and two repair-plan rows routed to contact-solve phase ownership. Direct self-test passes in 1.0 seconds, strict scan in 27.7 seconds, and `validate_fast` in 177.3 seconds with format, metadata, dependencies, all four ownership inventories, Profile x64 build, and tests clear. Independent review ended with zero blockers after fail-closed path and CLI-mode hardening. Comment audit is 3/3 with zero deferred; no behavior, baseline, golden, or runtime artifact changed. |
| Validation for broadphase guard closure | Both radix paths derive from `MAX_SCENE_OBJECTS`; triangular identity/reset arithmetic is wide before guarded narrowing; exact cell coordinates remain `int` and the int16 visualization projection saturates to [-32,768, 32,767]. The ceiling test exercises unfiltered and production-filtered emission through body 8,191: focused SpatialGrid passes 16/16 cases and 8,524 assertions. `validate_tests` passes in 21.8 seconds with 439/439 tests and 2,419,238 assertions; `validate_physics` passes in 59.76 seconds with all 44,401 rows byte-exact; `validate_perf` passes in 91.2 seconds with both absolute budgets and baseline comparisons clear. Format, all three ownership inventories, 3/3 comment audit, and follow-up independent review pass. No baseline was refreshed. |
| Campaign registration 2026-07-29 | Documentation-only: seven fresh-read plans written to `Plans/TODO/` and the owner-supplied box-vibration plan registered as plan 8; ledger denominator 0 → 28 → 35; campaign section, plan-8 ordering consequences, and execution priority added to `MASTER-PLAN.md`. No source, data, or baseline changed, so no repository validation was required. Plan 1 fixes a silent determinism hazard (broadphase radix digit widths unbound from `MAX_SCENE_OBJECTS`). Plans 7 and 8 are the only baseline-moving plans: plan 7 is owner-gated on hands-on visual acceptance, plan 8 carries a bounded-divergence allowance. |
| Validation for warm-start key capacity | The unchanged `0x7fff` body mask is now one shared Physics key-schema value with a compile-time `MAX_SCENE_OBJECTS` guard. Final formatted source passes `validate_physics` in 112.4 seconds with standalone/runtime-handle smoke and a byte-exact 44,401-line regression. `validate_perf` passes in 90.8 seconds: DX12 Physics/solver averages are 0.2204/0.0253 ms and focused-bench averages are 0.0731/0.0271 ms; both comparisons report no regressions. Format, dependency direction, all ownership inventories, 3/3 comment audit, and independent review are clear. No baseline was refreshed. |
| Validation for dependency proof closure | Planted rule drift invalidates the old proof; all marker failures remain fail-closed; every Runtime allow row rejects a future package; four isolated parser fixtures pin macro, continuation, quoted, angle, and local-first behavior; and exact project cases cover required-only, missing-required, required-plus-Core, required-plus-Tests, Git discovery, both MSBuild item kinds, and all four governed suffixes. Follow-up independent review is clear and the comment audit is 1/1. The 3.23-second dependency gate, 232.84-second fast gate, and 370.38-second full gate pass with 438/438 tests, 2,419,221 assertions, zero DX12 errors, accepted images, and byte-exact 44,401-line Physics. No baseline was refreshed. |
| Validation for dependency proof DP1 | The existing dependency checker owns one deterministic marked `AGENTS.md` projection, with fail-closed marker topology, current/stale checking, byte-preserving writes, and escaped rule-controlled Markdown. Its generated tables replace the 21-row Runtime matrix and all 27 mechanical `rg` proofs while retaining one explicitly qualitative Rendering search. Six frame-view allowances are exact files; valid exact edges pass and the pseudo-descendant fixture fails. App closed-world, Camera App-prefix, UI-to-Rendering, and residual textual-parser semantics are explicit. Self-tests pass 27 include rules / 47 negative edges, two negative content fixtures, one project fixture, and the generated-proof cases; the proof check and dependency gate pass with zero repository findings. Focused Python syntax/diff-whitespace checks and all three ownership inventories pass. Touched-tool comment audit is 1/1 with zero deferred files. No baseline was refreshed. |
| Validation for dependency proof DP0 | Documentation-only comparison: 27 include rules, one content rule, one project rule, the 21-row Runtime table, 28 hand-written commands (27 mechanical and one qualitative), 46 negative include fixtures, two content fixtures, and one compound project fixture are reconciled row by row. The report identifies the over-broad Input proof, every Runtime regex's future-package hole, the frame-view self-include hole, UI prose/proof omission of Rendering, compound project-fixture false pass, selected-example fixture coverage, textual-include/resolver limits, path-casing behavior, prefix-to-exact migration with intentional descendant rejection, and a single-checker deterministic Markdown/freshness design. Its generated block owns the matrix and all mechanical commands; markers fail closed, writes preserve outside bytes, and escaping is deterministic. No repository validation was required. |
| Validation for terrain fixture closure | The seven-run flat/deep reconstruction witness covers flat-to-flat, flat-to-deep, deep-to-flat, and deep-to-deep predecessors and passes 92/92 focused assertions. Randomized determinism seeds 28072026 and 731942 each pass 24/24 cases and 2,385,028 assertions. Final-source tests pass 438/438 cases and 2,419,221 assertions; format, 787/787 project/filter rows, dependency direction, all ownership inventories, byte-exact 44,401-line Physics, deep Physics, and the 284.23-second full gate pass. Comment audit is 4/4; follow-up independent review is clear. No baseline, golden, config, schema, performance artifact, layout, or SoA storage changed. |
| Validation for terrain fixture TF1 | All four shared terrain statics, the terrain-bearing default, and nullable helper branch are deleted. All 18 terrain-bearing determinism cases use per-test config/terrain/heap-engine invariant owners; comparison engines own independent fixtures. Related prediction and terrain-coverage cases use per-case owners, and the startup lifecycle probe now has owner-safe destruction order. Profile build, focused 23-case determinism and two related cases, 437/437 tests with 2,419,129 assertions, standalone lifecycle smoke, dependency graph, and all three ownership inventories pass. Exact formatting passes for 4/4 touched source files; the global format gate is blocked only by two owner warm-start files. Comment audit is 4/4. No baseline, golden, config, schema, performance artifact, layout, or SoA storage changed. |
| Validation for terrain fixture TF0 | Documentation-only census: 55 tracked test source/header paths contain four shared function-local terrains and one terrain-bearing default argument. `TestDeterminism.cpp` reaches 18 cases (17 flat, one deep), with one related shared case each in `TestPhysicsHandles.cpp` and `TestTerrain.cpp`. A separate non-test startup probe declares its heap engine before config/terrain and therefore destroys the retained-view owner first. `PhysicsWorld` retains the copied terrain view across `Clear`; config/terrain/heap-engine declaration order is binding for both TF1 surfaces. Analytic test terrains add no terrain heap/cache allocation. No baseline refresh or repository validation was required. |
| Validation for vector dot-product closure | The configuration-complete census is 173 ambiguous uses across 36 files: 172 exact `Dot` call-site rewrites plus deletion of the OrbitalMechanics adapter and its one overload-body use. One shared definition and 179 calls remain, reconciling as 172 migrations, five existing Orbital named calls, and two permanent cancellation witnesses. Profile/Debug compilation, 437/437 tests and 2,419,129 assertions, 787/787 project/filter rows, dependency direction, all ownership inventories, byte-exact 44,401-line Physics, deep Physics/SkullScope, performance, one-generation Replay visual fidelity, DX12, and full validation pass. Comment audit is 36/36; independent review blockers were resolved. No baseline, golden, config, schema, or performance artifact was refreshed. |
| Vector dot-product VD0/VD1 correction | VD0's Profile-preprocessed AST census found 171 rows across 34 files and VD1 migrated its 170 call sites plus deleted the adapter-body use. VD2's full Debug build exposed two additional `_DEBUG`-only rows in `SkullScope.cpp` and `LauncherTools.cpp`; the dated corrections in both reports supersede the old completeness claim with 173 uses across 36 files. |
| Validation for SbResult closure | The result is 16 bytes and pointer-aligned; the fixed 256-slot store is 159,760 bytes. Exact 511-byte diagnostics, copy/move/identity, full-capacity, stale/foreign, cross-thread, no-allocation, high-water, lease/generation/re-entry Lane F, five DX12 epoch, and ImGui status-lifetime proofs pass. The corrected final capacity census is 221 publications plus 31 retained members, 252/256 with four slots of conservative headroom, one App store, and no multiplicative container/queue/worker path. Automation now uses one owner-held store and no report-input courier. Final-source tests pass 436/436 cases and 2,419,127 assertions; format, metadata, dependency, ownership inventories, Automation, performance, 60.576-second graphics stress, and the 355.5-second full gate pass without baseline refresh. Comment audit 18/18; independent review accepted. |
| Validation for SbResult SR2 | At the SR2 checkpoint, MSVC x64 measured the migrated result at 16 bytes and the App-composed 256-slot store at 159,760 bytes. The explicit no-wrapper API was migrated across production and tests; SR2 reported 221 producer expressions plus 29 result-member sites, 250/256, with one production store and no result container/queue or hidden store. Focused success, formatting, lifetime, generation, capacity, exact concurrent owner/message bytes, stale/cross-store copy-out, owner overflow, double release, active-lease store destruction, and application-exit lease tests passed. Every normal `WinMain` exit reported active/session-high-water/capacity after store accessors unlocked and while the App store was alive; the production worker-self-test exit reported 0/0/256. Clean-worktree format, project filters, dependency graph, and `validate_fast` passed; all three ownership inventories passed; touched-source comment audit was 166/166 after the Automation-only closure. The closure row above supersedes SR2's capacity arithmetic and broad-proof status. |
| Validation for SbResult SR1 | Documentation-only decision: one App-composed 256-slot (~160 KiB) immutable store, 16-byte pointer/token result, last-lease reclamation, 56-bit generation stale-handle protection, allocation-free synchronized failure publication, deterministic Lane F exhaustion/overflow, and explicit no-wrapper API migration. SR2 corrected the original spelling-sensitive 176+30 bound to 220+30, or 250/256, under verified no recursion/re-entry, worker publication, or result containers/queues; direct diagnostic pointers last only for the same live unmoved/unassigned lease and escaping use copies bounded bytes. The focused matrix includes test-only concurrent publication within capacity and a double-release Lane F child probe. No repository validation or baseline refresh required. |
| Validation for SbResult SR0 | Current-source census: 177 returning definitions, 200 production success constructions, 220 production failures after SR2's multiline-aware correction, 30 direct stored fields, 74 direct diagnostic-consumer lines in 32 files, and zero CPU-thread/deferred-queue result handoffs. MSVC x64 measures `SbResult` at 528 bytes and preserves the exact 511-byte payload. Detached-clean-worktree `validate_perf` passes in 154 seconds without baseline refresh. |
| Validation for Physics fixed-list closure | Compile-time owner non-transferability, production-adapter construction/seed, deep collider rebinding after source destruction, bit-exact next-step parity, and missing/wrong phase-owner fatal probes pass. Final-source `validate_tests` passes 422/422 cases and 2,410,618 assertions; allocation self-test/repository scan, `validate_physics`, strict Replay allocation, the single authoritative visual-fidelity run, `validate_perf`, and `validate_full` pass in the isolated worktree. Inventories are 86/86 aggregate rulings, 1/1 extraction-scar ruling, and all 12+ signatures ruled. Comment audit is 5/5; independent review returned zero blockers. No baseline was refreshed. |
| Validation for Replay restore RG2 | Clean-worktree Profile build (45.9 s) and `validate_tests` (14.8 s rerun; 423/423 cases and 2,410,303 assertions) pass. Wide-signature strict scan is 28/28 ruled, aggregate scan is 86/86 ruled, and extraction-scar scan is 1/1 ruled. Comment audit is 8/8 with zero deferred files. |
| Validation for Replay restore closure | Independent review found one terminal-state false pass; transaction-owned branch/rollback proof and child fatal probes corrected it, and repeat review found zero blockers. Final-source `validate_fast` (211.8 s), `validate_tests` (35.6 s; 424/424 cases and 2,410,325 assertions), Replay visual fidelity (439.8 s), and `validate_full` (408.7 s) pass. All inventories pass; comment audit is 9/9. No baseline was refreshed. |
| Validation for Physics body hot layout | Clean-worktree `validate_physics` (109.1 s), `validate_perf` (91.5 s), and corrected `validate_full` (431.1 s) pass. Inventories are 86/86 aggregate rulings, 1/1 extraction-scar ruling, and zero signatures over 12. Comment audit is 1/1; independent review found zero blockers. No baseline was refreshed. |
| Validation for principal feedback response | Final-source `validate_fast` (205.5 s), `validate_tests` (15.0 s), `validate_physics` (27.2 s), `validate_perf` (90.6 s), `validate_replay_visual_fidelity` (394.4 s), `validate_dx12_renderer` (53.5 s), and `validate_full` (310.6 s) pass. No baseline was refreshed. Ownership inventories report 86/86 aggregate rulings and 1/1 extraction-scar ruling; comment audit is 9/9. Independent review found one stale master-plan ruling and two missing Quaternion goldens; all were corrected before validation, leaving zero blockers. |
| UI ruling | Legacy remains the default. ImGui is explicit `--dev-ui imgui`; atomic hot swap is allowed, simultaneous Legacy/ImGui activation is forbidden. |
| Last broad local gate | Plan 12 `tools\validate_full.bat` passes in 337 seconds: 421/421 tests and 2,410,274 assertions, CPU/coverage, DX12 renderer/runtime lanes, and byte-exact Physics. `validate_perf.bat` and independent review also pass with zero blockers. |
| Validation for coverage reorganization | Direct coverage and all six CPU lanes pass; 418/418 doctests and 2,410,159 assertions, ten unchanged subsystem percentages, 114/114 project/filter rows, zero gate-named test files, and clear independent review. |
| Validation for governance G0-G4 | `tools\validate_fast.bat` passes in 112.9 s: aggregate 1,205 candidates / 10 signalled / 10 ruled / 0 unruled, scars 89 / 89 / 0, zero build warnings/errors. `tools\validate_all_cpu_tests.bat` passes in 60.4 s: all six lanes, 402 doctests / 2,403,914 assertions, and every coverage floor. Independent review ended `ZERO BLOCKERS`; comment audit 29/29. |
| Validation for scene capacity SC0-SC1 | Current-source declaration census 43 fixed + 50 vector = 93 rows and 112,042,496-byte Debug payload lower bound. SC1 passes 408 doctests / 2,403,974 assertions, `validate_fast`, `validate_perf`, `validate_full`, byte-exact physics regression, standalone smoke, allocation guard, allocation-policy scans, aggregate governance, and a 32/32 touched-source comment audit. Independent review ended `ZERO BLOCKERS`. |
| Validation for scene capacity SC2 | `ColliderRecord` is 80 bytes, per-kind backing relocation passes 40/40 focused assertions, and zero-hull capacity remains zero. `validate_physics`, `validate_perf`, and `validate_full` pass with byte-exact physics and unchanged DX12/physics budgets; all 44 touched source files were comment-audited. Independent review ended `ZERO BLOCKERS`. |
| Validation for scene capacity SC3 | Exact scene topology commits before mutation; 300→200 yields zero growth and 300→2,000 yields 67 unique owner events. Logical joint shrink, 9,000-row fatal, editor ragdoll, and pinned mixed-generation RNG probes pass. `validate_fast`, `validate_physics`, `validate_perf`, and `validate_full` pass; 410 doctests / 2,406,382 assertions, byte-exact physics, accepted DX12, 17/17 comment audit. Independent review ended `ZERO BLOCKERS`. |
| Validation for scene capacity SC4 | All 24 contact, narrowphase, broadphase, and force rows use scene-committed fixed lists; Debug contact tick growth is deleted. Focused Debug capacity coverage, allocation policy, `validate_fast`, `validate_physics`, and `validate_perf` pass with byte-exact 44,401-line physics output; 22/22 touched source files comment-audited. |
| Validation for extraction-scar remediation | All 88 repairs are gone; the scanner reports only the unchanged WorkerPool retain. Fast, Physics, deep Physics, performance, and full gates pass; 410 doctests / 2,406,382 assertions, exact SkullScope JSON, byte-exact 44,401-line Physics CSV, 14/14 comment audit. Independent review ended `ZERO BLOCKERS`. |
| Validation for render-backend service-bag removal | The eleven-pointer bag is deleted; capture is required and optional capability presence is explicit. Focused policy 5/5, fast, three consecutive DX12, one-minute graphics stress, full, and performance gates pass. Independent ownership review ended clear. |
| Validation for prior edits | N26: Replay scrub 17/17 and 75 assertions, focused preview 2/2 and 24 assertions, format, fast, allocation, dependency, performance, full, and 60.83-second graphics stress pass; comment audit is 24/24. |

## Live Queue

The Gate Blind Spot Campaign is active at 16/21 (76%). Solver Diagnostic
Hot-Path Cost HP0-HP3 are complete with exact artifacts and a recorded Profile
win without refreshing a golden. Runtime Contract Hygiene CH0-CH2 are complete
with its exit, Quaternion, and zero-throw contracts closed. Engine Glossary
Consolidation is complete at 4/4 with the 575-file checklist closed and zero
basename-led summaries. Angular Impulse Frame Correctness is complete at 5/5
with owner acceptance and a zero-delta closure rerun. Vector Frame Contract
Closure is active at 0/5; VF0 is the binding next phase.

The Claim Integrity Campaign is complete. Build Configuration Parity, Maths
Surface Reachability, Inverse-Trig Domain Guards, Retirement Diagnostic
Honesty, and Unreachable Symbol Remediation all closed and left the live ledger
under rule 4.

The Principal Engineer Feedback Campaign is complete and has no live plan in
the active/future ledger. Physics body layout, Replay restore/wide-signature
governance, PhysicsFixedList copy semantics, compact SbResult success values,
explicit vector dot products, isolated deterministic terrain fixtures, and
generated dependency proofs all closed and left the ledger under rule 4. DP2
now pins planted proof drift, future Runtime package rejection, isolated
residual-parser behavior, and exact project ownership through the production
evaluators; independent review and every mapped gate are clear. Permanent
evidence is
`Agentic/Reports/2026-07-28/dependency-proof-generation-closure.md`. The
bounded registration response removes the exact Replay pure forwarder,
publishes the anti-Hamilton/
transposed-matrix quaternion
contract in the public header, makes orientation conversion const, gives the
identity values one inline definition, and enables first-party IDE warning
errors. The owner selected the current SoA for this campaign and approved the
200, 520, 1,000, 2,000, and sleepy-5,000 witness matrix. The owner also approved
qualitative review of all exact-12 signatures, owner-level fixed-list clones,
bounded owner-managed SbResult diagnostics without a compatibility wrapper, and
one-shot deletion of vector-vector `operator*`. The warm-start key guard is
accepted and closed with byte-exact Physics, passing Physics/solver performance,
and clear review. Permanent evidence is
`Agentic/Reports/2026-07-28/persistent-contact-key-capacity-closure.md`; no
baseline was refreshed.

Architecture Follow-Up Campaign Round 5 is complete at 7/7 phases, registered
2026-07-26 from the same-day from-source architecture review of
`nightrunner-26th-JUL-26` at tip `35f6de4e` (review read only source and tests;
no plans, reports, or git history). The owner added plan 14 on 2026-07-27.
`governance-shape-to-judgment-conversion` closed G0-G4 on 2026-07-27 and left
the live ledger under rule 4. Its amended `AGENTS.md` / review-skill rules are
now every sibling plan's closure test. Two inventories gate `validate_fast`
step 4/8 on an unruled-fails/ruled-passes contract with no frozen count.
Permanent closure evidence is
`Agentic/Reports/2026-07-27/governance-shape-to-judgment-conversion-closure.md`.
`render-backend-service-bag-removal` deleted the eleven-pointer view, made
capture required, made optional capability presence explicit at startup
composition, and closed RB0-RB3 with clear independent review and unchanged
baselines. `runtime-frame-view-retirement` FV0 measured the corrected current
surface. The owner-ratified coordinator exception resolved FV1 without a source
rewrite: the six named `Run` methods already coordinate through direct member
reach and delegate concrete operands only. FV2 then deleted all four frame
views and both emptied forwarding headers. FV3 removed the broad wrappers
rejected by its first independent review, reconciled comments, and closed with
clear review plus every mapped gate passing unchanged. Plan 5 then left the
live ledger under rule 4. Plan 8 SR0
ruled every operation to a concrete owner, existing GV transaction, or pure
domain policy. SR1 moved the state-owning operations and deleted seven emptied
implementation units. SR2 eliminated the residual filler names, assigned the
scene-path queue, session state, and lifecycle ledger to `SceneSession`, and
removed the `Runtime()` escape hatch. SR3 closed with a 60/60 comment audit,
clear independent review, and unchanged Physics/DX12 baselines; plan 8 then
left the live ledger under rule 4. Plan 9 OC0 fixed the eight-edge operator
command phase order, every same-frame winner, operation destination, and
acceptance-ledger consumer. OC1 installed the non-copyable value-only
transaction, named the arbitration invariant in its header, and proved every
legal edge plus all 82 illegal calls from reachable phases. OC2 moved all
operations behind the transaction, unified the acceptance ledger, deleted the
seven legacy result records, and removed all 71 `RunInternal` rows. OC3 closed
with clear independent review, a 37/37 comment audit, the complete full gate,
and clean one-minute graphics stress. Plan 9 then left the live ledger under
rule 4; plan 10 CG0 is binding.
`scene-sized-store-capacity` SC0 corrected the dense-store census from the
review's 65 rows to 93 current rows and measured a 112,042,496-byte Debug
payload lower bound per engine. SC1 then introduced registered runtime backing,
preserved the separately governed ReplayPrediction exception, and closed the
container defects. SC2 split collider payloads by shape kind and reduced the
hot row from 7,228 to 80 bytes. SC3 then committed exact body, shape-kind, and
joint topology before mutation while preserving monotonic backing, generated RNG,
and Replay's existing owner. Plan 7 then removed all 88 repair-ruled extraction
scars, preserved the sole WorkerPool retain, and left the live ledger under
rule 4. SC4 converted the 24 hot contact, narrowphase, broadphase, and force rows
and deleted contact-tick growth. Plan 10 CG0 assigned all five gate-named tests
to existing subsystem owners, measured 231 assertions, and recorded the ten
direct coverage baselines. CG1 deleted the gate-named file and preserved
418/418 cases, 2,410,159 assertions, and every exact coverage percentage. CG2
installed the permanent rule, passed all six CPU lanes, and closed with clear
independent review; plan 10 then left the live ledger under rule 4. Plan 11 AF0
now confines the candidate-header magic read to a table-based MSVC exception
guard, proves an inaccessible predecessor page cannot fault global delete, and
adds no registry, lock, allocation, system call, or extra header read to the
owned path. Tests, format, allocation policy, and the full performance gate pass
without regression. AF1 then installed the process-lifetime counter, the
ratified Debug/Profile fatal and Release counted/reporting CRT fallback,
checked allocation-size arithmetic, and joined the existing memory diagnostics
without changing the zero-count UI fingerprint. Profile tests pass 418/418 and
2,410,177 assertions; focused Release proof passes 122/122. AF2 independent
review found two magic-only provenance bypasses; the candidate now uses a
guarded whole-header copy and process-specific ownership cookie, and Profile
passes 418/418 with 2,410,186 assertions. Plan 11 reached 2/3 when genuine
provenance proved incompatible with literal instruction identity. The owner
retained the safe cookie/header snapshot and replaced that clause with a clean
`validate_perf.bat` no-regression contract. AF2 then closed with guarded
whole-header provenance, zero foreign frees across performance and graphics
stress, clean policy scans, all 421 tests, the default full gate, and
independent review at `ZERO BLOCKERS`. Plan 11 left the live inventory under
rule 4. The owner
also allowed direct member reach in the `Run` phase coordinator while delegated
operations remain concrete and at or below 12 parameters, unblocking plan 5.
Plan 14 NA0 re-measured 1,167 discovered types and 84 bounded
legacy-suffix/no-invariant gated rows. All 84 already carry CA0 rulings and the
transitional verdict count is zero. Strict mode, source-drift checking, and the
required fixtures pass. Plan 12 SR0 measured 176 named returning definitions
plus one trailing-return lambda,
retained the protected 511-byte failure payload, and selected sentinel-only
success construction. SR1 implemented it without changing failure storage or
call sites. SR2 corrected the initial census blocker and closed with clear
repeat review plus passing tests, performance, and full validation. Plan 13
subsequently ran last and closed.
NA1 armed strict mode in `validate_fast`, recorded the
permanent ownership rule and name-scope residual, and observed the gate reject a
planted seven-member `FooFrameContext`. Final-source `validate_fast` passes;
NA2 retired the transition and closed independently found qualified/decorated
head, function-pointer, attribute, specialization, collision, bitfield,
anonymous-union, inheritance, nested-storage, and zero-recognized-member
bypasses. The final inventory is 1,176 discovered / 86 gated / 86 ruled / zero
unruled, ambiguous, or transitional. All ruling reasons are concrete,
independent review is clear, `validate_fast` passes, and all six CPU lanes pass
with 418/418 tests and 2,410,186 assertions. Plan 14 then left the live ledger
under rule 4. Plan 13 T0 then reproduced each 44,401-row physics run
byte-exactly, matched the SkullScope query packet, and passed the four-case /
47-assertion focused terrain-support oracle. No direct shoreline-scale test
exists yet; T3 owns that known coverage gap.
T1 renamed the cached build, `LocatePolygon`, and Physics terrain lookup around
the real world-X-major cell convention, removed the misleading-name warnings,
renamed every `quadric`, and made the exact-zero branch explicit. The focused
47 assertions, `validate_physics`, `validate_physics_deep`, the 44,401-row CSV,
and SkullScope packet all pass unchanged. T2 then made every
`LocatePolygon` post read locally provable, added a 4-by-4 exact-upper-edge
Debug fatal probe, and retained the method because its debug-visualizer caller
needs triangle vertices. Format, all 418 unit tests / 2,410,193 assertions, the
four-case / 47-assertion oracle, and byte-exact Physics pass. T3 then named the
full and shoreline seed scales and documented the vertical-gravity,
unit-normal, and equal-per-point assumptions. T4 replaced the synthetic edge
evidence with a real tilted two-point terrain manifold, added malformed
coordinate/scale fatal probes, completed the 4/4 whole-file comment audit, and
cleared all five independent-review blockers. All 421 tests / 2,410,268
assertions, byte-exact Physics, deep Physics, SkullScope, performance, and the
default full gate pass. Plan 13 and Plan 12 are complete and removed from the
live ledger under rule 4; Round 5 has no remaining queue item.
Evidence:
`Agentic/Reports/2026-07-27/allocator-foreign-pointer-safety-closure.md` and
`Agentic/Reports/2026-07-27/terrain-legacy-contact-seed-remediation-closure.md`
and `Agentic/Reports/2026-07-27/sbresult-frame-path-cost-closure.md`
and `Agentic/Reports/2026-07-27/terrain-contact-seed-t3.md` and
`Agentic/Reports/2026-07-27/terrain-index-safety-t2.md` and
`Agentic/Reports/2026-07-27/terrain-axis-convention-t1.md` and
`Agentic/Reports/2026-07-27/terrain-legacy-contact-seed-t0-baseline.md` and
`Agentic/Reports/2026-07-27/new-aggregate-ruling-gate-closure.md` and
`Agentic/Reports/2026-07-27/new-aggregate-ruling-gate-na1.md` and
`Agentic/Reports/2026-07-27/new-aggregate-ruling-gate-na0.md` and
`Agentic/Reports/2026-07-27/allocator-foreign-pointer-safety-af2-blocker.md` and
`Agentic/Reports/2026-07-27/scene-sized-store-capacity-sc0-census.md` and
`Agentic/Reports/2026-07-27/scene-sized-store-capacity-sc2-shape-storage.md` and
`Agentic/Reports/2026-07-27/scene-sized-store-capacity-sc3-binding.md` and
`Agentic/Reports/2026-07-27/scene-sized-store-capacity-sc4-hot-stage-lists.md` and
`Agentic/Reports/2026-07-27/extraction-scar-remediation-closure.md`.
Plan 8 SR0 evidence:
`Agentic/Reports/2026-07-27/scene-runtime-verb-partition-sr0-census.md`.
Plan 8 SR1 evidence:
`Agentic/Reports/2026-07-27/scene-runtime-verb-partition-sr1-owner-moves.md`.
Plan 8 SR2 evidence:
`Agentic/Reports/2026-07-27/scene-runtime-verb-partition-sr2-residual-naming.md`.
Plan 8 closure evidence:
`Agentic/Reports/2026-07-27/scene-runtime-verb-partition-closure.md`.
Plan 9 OC0 evidence:
`Agentic/Reports/2026-07-27/operator-command-invariant-ownership-oc0-census.md`.
Plan 9 OC1 evidence:
`Agentic/Reports/2026-07-27/operator-command-invariant-ownership-oc1-transaction.md`.
Plan 9 OC2 evidence:
`Agentic/Reports/2026-07-27/operator-command-invariant-ownership-oc2-owner-migration.md`.
Plan 9 closure evidence:
`Agentic/Reports/2026-07-27/operator-command-invariant-ownership-closure.md`.
Plan 10 CG0 evidence:
`Agentic/Reports/2026-07-27/coverage-gate-test-reorganization-cg0-map.md`.
Plan 10 CG1 evidence:
`Agentic/Reports/2026-07-27/coverage-gate-test-reorganization-cg1-move.md`.
Plan 10 closure evidence:
`Agentic/Reports/2026-07-27/coverage-gate-test-reorganization-closure.md`.

SC2 removed the headline collider-row inflation: `ColliderRecord` is now 80
bytes and borrows per-kind sphere, box, or hull backing; a zero-hull scene
commits no hull storage. Owner ruling at registration remains: runtime capacity
sized from the loaded scene, `MAX_SCENE_OBJECTS = 8192` retained as an absolute
fail-loud ceiling, capacity monotonic within a process, no shrink path.

Standing campaign constraints: no baseline, golden, schema, or config refresh in
any plan; no render interface, virtual dispatch, or type erasure reintroduced;
every PB0/GV1 explicit retain ruling carried forward untouched.
All three blocking owner decisions were ruled 2026-07-27 and recorded in their
owning plans: plan 5 FV0 takes concrete operands and no frame transaction; plan 11
AF1 is lane-F fatal in Debug/Profile and counted in Release; plan 13 T3 ratifies
the terrain seed rather than replacing it. Consequently **no Round 5 plan requires
divergence authority and none may refresh a baseline** — the campaign is
byte-exact throughout. Two smaller decisions received explicit safe defaults in
the same pass (plan 13 T2 keeps `LocatePolygon` behind a guard and only reports on
its debug caller; the Capability Slice Ownership Rule landed layer-agnostic).

DONE. Nightrunner 26 July is complete at 3/3 and removed from the live
inventory under rule 4. N26-1 caches dense solver scrub resolution and removes
duplicate availability/copy work. N26-2 ratifies the owner's 125-column,
compact-parameter, control-flow, assertion, comment, and parameter-order
style. N26-3 publishes a selected-body-only held-drag path preview, performs
no full prediction while held, schedules exactly one replacement on release,
and retains the preview until that generation commits. Independent review's
direct-consumer request-forwarding blocker was remediated and rechecked.
Permanent evidence is
`Agentic/Reports/2026-07-26/nightrunner-26-july-closure.md`.

NOW. The 2026-07-25 round-4 architecture campaign has no live plan.
`concrete-parameter-bag-elimination` is complete at 8/8 and has left the live
inventory under rule 4. PB0
ratified all 22 registered rows, added eight repair rows, carried forward three
13-parameter render/UI operations, and ruled every other reviewed hit. PB1
then repaired Scene save/load and split editor save/capture authority. PB2
deleted the pointer-routing projection chain while preserving exact editor,
mouse-pick, camera, Replay, and launcher precedence. PB3 deleted the
render-frame, UI-text, Replay-overlay, and graph-callback service bags while
preserving exact render, allocation, and visual behavior. PB4 replaced Replay
capture/focus bags with concrete values and restore bags with an owner-free,
phase-checked transaction. PB5 deleted the five Physics collision/solver bags
through concrete stages and direct values while preserving byte-exact pair
and solver behavior. PB6 deleted the seven sleep/force/terrain bags and their
obsolete shared header while preserving exact wake and worker order. PB7
reconciled all 30 repair rows and three ceiling defects, remediated the
independent review's UI ownership and focused-test findings, completed the
91/91 comment audit, and passed every cumulative gate without refresh.

Header Claim Staleness Remediation is complete at 3/3 and removed from the live
inventory under rule 4. Permanent evidence is
`Agentic/Reports/2026-07-25/header-claim-staleness-remediation-closure.md`.
Its corrected ownership claims and durable `Related:` paths are now the source
context for Replay and later GV work. Replay RS0-RS5 are complete; their
permanent evidence is
`Agentic/Reports/2026-07-25/replay-subsystem-partition-rs0-census.md` and
`Agentic/Reports/2026-07-25/replay-subsystem-partition-rs1-prediction.md`, and
`Agentic/Reports/2026-07-25/replay-subsystem-partition-rs2-planning.md`, and
`Agentic/Reports/2026-07-26/replay-subsystem-partition-rs3-seams.md`, and
`Agentic/Reports/2026-07-26/replay-subsystem-partition-rs4-enforcement.md`, and
`Agentic/Reports/2026-07-26/replay-subsystem-partition-closure.md`.
The completed Replay plan is removed from the live inventory under rule 4.
Downward Domain Bleed Remediation is complete at 6/6 and removed from the live
inventory under rule 4. Permanent evidence is
`Agentic/Reports/2026-07-26/downward-domain-bleed-remediation-closure.md`.
GV1 permanent evidence is
`Agentic/Reports/2026-07-26/invariant-ownership-governance-gv1-census.md`.
GV2 permanent evidence is
`Agentic/Reports/2026-07-26/invariant-ownership-governance-gv2-scene-load-transaction.md`.
GV3 permanent evidence is
`Agentic/Reports/2026-07-26/invariant-ownership-governance-gv3-generated-scene-transaction.md`.
GV4 closed the plan at 5/5; permanent evidence is
`Agentic/Reports/2026-07-26/invariant-ownership-governance-and-transaction-repair-closure.md`.
PB0 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md`.
PB1 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb1-scene.md`.
PB2 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb2-pointer-routing.md`.
PB3 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb3-render-ui.md`.
PB4 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb4-replay.md`.
PB5 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb5-physics-collision-solver.md`.
PB6 permanent evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-pb6-physics-sleep-force-terrain.md`.
PB7 permanent closure evidence is
`Agentic/Reports/2026-07-26/concrete-parameter-bag-elimination-closure.md`.
No plan in `Agentic/Plans/TODO/` remains live; retained completed files do not
contribute to the active/future ledger.

NOW. `solar-prediction-presentation-correction` is complete at 4/4. The
120-second Mars-assist scene keeps every planet/moon bounded, exact-compares
the Replay endpoint to the live oracle, refreshes retained paths during held
velocity drags, exposes and globally saves all 13 path-style values, and
schedules no shadow/reflection pass in either solar scene. The independent
review's two blockers and three proof gaps were remediated. Closure evidence is
in
`Agentic/Reports/2026-07-25/solar-prediction-presentation-correction-closure.md`.

NOW. The 2026-07-25 solar display follow-up is complete. The original
top-down/+Y-up cameras were the remaining orientation defect; both solar scenes
now use oblique cameras with +Z up. A scene-selectable `DeepSpace` sky style
returns literal black before procedural atmosphere and sun shading. Fresh
four-body and slingshot captures were inspected, their background samples are
exact `(0,0,0)`, and every mapped shader/scene gate passes.

NOW. `solar-system-slingshot-usability` is complete at 4/4. Solar scenes use
XY with Z up; prediction refreshes during both instant and amortized held
velocity drags; the default/operator maximum horizons are 20/120 seconds; and
the 32-body major-moon demonstration proves a non-contact Earth flyby followed
by a non-contact Mars encounter. The independent review's amortized starvation
finding was remediated and its held-drag gap received a real mouse probe.
Closure evidence is in
`Agentic/Reports/2026-07-24/solar-system-slingshot-usability-closure.md`.

NOW. `solar-system-trajectory-planner` is complete at 7/7. SS0 supplies bounded,
allocation-free orbital math. SS1's interactive four-body scene loads cleanly,
holds planet radii for 3.085 Mars periods, repeats its exact final state, keeps
all movable bodies awake, and proves the authored first transfer window. SS2
adds the bounded incremental Replay intercept consumer, independent target
selection, Legacy markers/text, focused coverage, and one-generation fidelity
proof. SS3 adds cold, fixed-capacity analytic Earth/Mars guide arcs with
default-off zero-cost behavior. SS4 adds the bounded Lambert-seeded shooting
planner with three-generation design-window convergence. SS5 adds the
fixed-grid porkchop launch-window panel. SS6 remediates the independent review,
proves rollback/default-off/epoch behavior, and closes every final gate.
Closure evidence is in
`Agentic/Reports/2026-07-24/solar-system-trajectory-planner-closure.md`.

NOW. `ui-runtime-separation` is complete at 5/5. UI is physically below
Runtime, has zero Runtime includes, and crosses through cohesive navigation,
input, command, and status values. Its single independent review found one test
depth gap, remediated by the focused `DiagnosticsPhysicsUI` owner and exact
command-to-state-to-status coverage. All standing proofs and final gates pass.
Closure evidence is in
`Agentic/Reports/2026-07-23/ui-runtime-separation-closure.md`.

NOW. `runtime-package-decomposition` is complete at 5/5. All 80 assigned files
live in owner packages and only `RuntimeFrameViews.h` remains at top level.
The standing Runtime edge table and 18 operational `\x22` proofs are in
`AGENTS.md`; the single independent review found and verified two proof
remediations, then reported no remaining finding. Replay visual fidelity,
platform-profiler markers, and the broad gate pass. Closure evidence is in
`Agentic/Reports/2026-07-23/runtime-package-decomposition-closure.md`.

NOW. `wide-signature-parameter-bag-remediation` is complete (6/6).
The owner rejected replacements such as `RenderModelPassInput` that merely
bundle arguments for immediate unpacking. B0 reopened the three prior
wide-signature closure claims and inventories every campaign-introduced or
repurposed parameter object. Mechanical input/request/descriptor/context
shapes must be removed through actual action or phase decomposition; real
body-state values, emitted diagnostics, and producer result records retain
their independent domain identity. PR #131 remains draft while this is fixed.
B1 removes `InGameUIInputFrame`, both Scene-tab frame packets, and all tornado
quad call packs. Scene-selection controls now belong to `UISceneTabState`; UI
consumes its pre-existing normalized input snapshot plus explicit facts under
the 12-parameter ceiling. The ten touched source files pass the comment audit,
the threshold-13 scan is empty, and `validate_fast` passes. B2 is now complete:
narrowphase borrows behavior from the sleep owner instead of a row
pack; texture and mesh creation consume explicit cold-upload facts; main and
reflection model submission are separate APIs with compile-time visibility;
and object shadow submission is structural. Fifteen touched source files pass
the comment audit. Physics, DX12, one-minute graphics stress, and performance
gates pass without baseline refresh, validation errors, allocation violations,
or performance regressions. B3 removes the Replay cause-tree and velocity
source packs, the loaded-presentation request, and the scrub-gesture packet.
Cause-tree surface/activation, velocity gizmo/target picking, artifact
reset/arming, and scrub/prediction-horizon gestures are now distinct operations.
`ReplayWorkspaceFrameInput` remains only at `TickWorkspace`; no extracted
operation receives or copies it. Nine touched source files pass the comment
audit, the Profile build and focused Replay tests pass, and the threshold-13
scan remains empty. B4 removes the Replay prediction frame request, the
additionally discovered prediction-job descriptor, the render-preparation
packet, and the restore-diagnostic input packet. Prediction and render now use
ordered owner operations, and restore sites directly build the real emitted
diagnostic. Six touched source files pass the comment audit; the Profile build,
53 focused Replay doctests / 799 assertions, removed-type scan, and threshold-13
inventory pass. B5's history reconstruction finds and removes six additional
descriptors plus `RuntimeRenderInputs` and `RuntimeRenderServices`. The final
55/55 comment audit, threshold/static proofs, hostile no-bag review, Replay
allocation/artifact/scrub-visual-fidelity gates, and broad gate pass. Closure
evidence is in
`Agentic/Reports/2026-07-23/wide-signature-parameter-bag-remediation-closure.md`.
The subsequent `source-blemish-remediation` campaign is complete at 6/6; its
closure evidence is recorded at the top of this file.

`wide-signature-decomposition-round-2` closed D0-D4 (5/5) and left the
live ledger. All eight reopened threshold-16 rows were removed without a
context/service bag, hidden mutable owner, or forwarding facade. The final scan
is empty; dependency/Replay-boundary proofs, allocation and Replay artifact
gates, scrub/visual fidelity, the broad gate, and independent review pass.
Evidence is in
`Agentic/Reports/2026-07-23/wide-signature-decomposition-round-2-closure.md`.
The prior
2026-07-22 architecture follow-up round-2 campaign remains closed. Wide
signature reduction removed all 16 ruled defect rows without introducing a
context bag; the final 285 rows all retain explicit owner rulings. The closure
report is `Agentic/Reports/2026-07-23/wide-signature-reduction-closure.md`.
The campaign section in
`Agentic/Plans/MASTER-PLAN.md` carries the ratified owner decisions,
including that replay is the engine's most important subsystem and its audit
was an internal-quality pass, not a slimming exercise. Replay deduplication
closed RD0-RD3 with evidence in
`Agentic/Reports/2026-07-22/replay-deduplication-closure.md`. The prior 2026-07-22
architecture follow-up campaign is closed: render interface retirement closed
RH0-RH5, owner fan-out reduction closed OF0-OF5, and Replay subsystem
consolidation closed RC0-RC6. Evidence is in
`Agentic/Reports/2026-07-22/render-interface-retirement-closure.md`,
`Agentic/Reports/2026-07-22/owner-fanout-reduction-of5-closure-census.md`, and
`Agentic/Reports/2026-07-22/replay-subsystem-consolidation-closure.md`.
RuntimeRenderer decomposition also closed RR0-RR5; evidence is in
`Agentic/Reports/2026-07-22/runtime-renderer-decomposition-closure.md`. The prior architecture-review campaign is
closed through replay policy, and the owner accepted ImGui/Tracy E17 for
extended hands-on use on 2026-07-21; closure evidence is in
`Agentic/Reports/2026-07-21/imgui-tracy-editor-campaign-closure.md`.

`dependency-direction-restoration` is closed 6/6 and archived in
`Agentic/Reports/2026-07-20/dependency-direction-restoration-closure.md`.
`allocation-namespace-restoration` is closed 1/1 and archived in
`Agentic/Reports/2026-07-20/allocation-namespace-restoration-closure.md`.
`physics-facade-unification` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/physics-facade-unification-closure.md`.
`physics-settings-snapshot` is closed 4/4 and archived in
`Agentic/Reports/2026-07-20/physics-settings-snapshot-closure.md`.
`run-execute-deaccretion` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/run-execute-deaccretion-closure.md`.
`render-graph-completion` is closed 6/6 and archived in
`Agentic/Reports/2026-07-20/render-graph-completion-closure.md`.
`render-hal-modernization` is closed 6/6 and archived in
`Agentic/Reports/2026-07-21/render-hal-modernization-closure.md`.
`gameplay-module-extraction` is closed 4/4 and archived in
`Agentic/Reports/2026-07-21/gameplay-module-extraction-closure.md`.
`replay-boundary-containment` is closed 3/3 and archived in
`Agentic/Reports/2026-07-21/replay-boundary-containment-closure.md`.
`replay-policy-debt-closure` is closed 4/4 and archived in
`Agentic/Reports/2026-07-21/replay-policy-debt-closure.md`; its single fresh
independent review is clear with no remaining findings.

`imgui-tracy-editor-campaign` closed at 18/18 after the owner accepted the clean
current-tip assisted ImGui/Tracy/Legacy playtest. The completed TODO plan is
deleted under MASTER inventory rule 4. Legacy remains default, ImGui remains
explicit opt-in, and only one UI surface owns focus/input at a time.

## Audio Removal

The `codex/remove-audio-pr` branch removes runtime audio ownership and contact
processing, both UI surfaces, operator commands and diagnostics, startup flags
and probes, XAudio linkage, `stb_vorbis`, the contact-audio scene, and all shipped
audio data. Config format v5 deterministically strips legacy
`contact_audio_*` settings. The only remaining audio spellings in live code and
tools are that v4-to-v5 migration and its fixtures.

## Physics Closure

The body-count campaign closed 8/8 on 2026-07-20. Final Profile
`Frame/Physics` P50 changed from P0 as follows: scale-200 0.1106 -> 0.1075 ms,
scale-520 0.8486 -> 0.8021 ms, scale-1,000 1.0688 -> 1.0850 ms,
scale-2,000 1.7852 -> 1.8878 ms, and sleepy-5,000 2.1479 -> 1.3026 ms.
The sleeping-heavy witness is 39.36% faster, while the all-awake 1,000/2,000
witnesses are 1.52%/5.75% slower; the owner explicitly accepted that trade.

The new 520-body unit fixture and six-scene runtime matrix certify byte-exact
0/1/4-worker determinism (18/18 process matches). Algorithmic and tested
multithreaded determinism are present; cross-platform and rollback determinism
are not certified. Full evidence:
`Agentic/Reports/2026-07-20/physics-body-count-scale-closure.md`.

## Final Validation

| Command | Time | Result |
|---|---:|---|
| prediction retained-rendering focused tests | — | PASS; canonical geometry 9 assertions, continuation adjacency 7, repaired suffix upload 9, scheduling 6, build-root prefix 3 |
| `tools\validate_replay_visual_fidelity.bat` (prediction retained-rendering) | 425.2 s | PASS; one process/generation/presentation, 2,401 ticks, all controls |
| `tools\validate_dx12_renderer.bat` (prediction retained-rendering) | 23.7 s | PASS; zero InfoQueue errors, all image baselines accepted |
| `tools\run_graphics_stress.bat 1` (prediction retained-rendering) | 61.1 s | PASS; bounded DX12 run completed crash-free |
| `tools\validate_full.bat` (prediction retained-rendering) | 182.7 s | PASS; mandatory CPU preflight and all five runtime processes |
| solar SS6 Profile tests build | 15.3 s | PASS; zero warnings/errors |
| solar SS6 focused planner tests | 1.69 s | PASS; 5/5 cases, 108/108 assertions |
| solar SS6 focused porkchop tests | 1.88 s | PASS; 4/4 cases, 70/70 assertions |
| solar SS6 final direct probe | 3.2 s | PASS; 15.4676 ms total, 0.6092 ms max frame, 0.345871 s sweep age |
| solar SS6 `validate_perf` first sample | 130.1 s | Inconclusive; one +13.4% DX12 frame sample, all allocation/absolute budgets clean |
| solar SS6 `validate_perf` unchanged rerun | 82.0 s | PASS; no repeatable regression |
| solar SS6 replay visual fidelity, sole invocation | 437.0 s | PASS; one process/generation/presentation and all controls |
| solar SS6 `agent_validate` | 250.1 s | PASS; CPU/coverage, five runtime lanes, zero DX12 errors, byte-exact physics |
| `python tools\check_allocation_policy.py --self-test` | 0.09 s | PASS |
| `python tools\check_allocation_policy.py --repo .` | 9.14 s | PASS; zero allowlist errors |
| `tools\validate_fast.bat` | 56.95 s | PASS |
| `tools\validate_physics_deep.bat` | 128.05 s | PASS |
| `tools\validate_build.bat Automation` | 14.40 s | PASS; zero warnings/errors |
| `tools\validate_full.bat` | 142.49 s | PASS |
| `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 2` | 1.205 s | PASS; marker emission enabled |
| `tools\validate_full.bat` (L5 closure tip) | 143.84 s | PASS |
| `tools\validate_physics.bat` (C1) | 79.83 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_perf.bat` (C1) | 109.20 s | PASS; zero allocation violations/regressions |
| replay visual fidelity (C1, one engine generation) | 391.01 s | PASS; all positive/negative controls |
| `tools\validate_full.bat` (C1) | 143.12 s | PASS |
| `tools\validate_tests.bat` (C2) | 12.84 s | PASS; 327 cases, 61,131 assertions |
| `tools\validate_tests.bat` (C3 review fixes) | 12.59 s | PASS; 328 cases, 61,341 assertions |
| `tools\validate_physics.bat` (C3) | 79.13 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_perf.bat` (C3) | 108.54 s | PASS; allocation and comparison gates report no regression |
| `tools\validate_full.bat` (C3) | 140.74 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X0) | 13.38 s | PASS; moved controller and Run call site compile |
| direct Release build (X0) | 38.67 s | PASS; production macro path compiles |
| `tools\validate_ui_stress.bat` (X0) | 72.77 s | PASS; moved command matrix, clean logs, zero DX12 errors |
| `tools\validate_full.bat` (X0) | 139.54 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X1 tip) | 10.46 s | PASS; zero warnings/errors |
| direct Release build (X1 tip) | 25.93 s | PASS; zero warnings/errors |
| focused runtime interaction policy test (X1) | 2.44 s | PASS; 1 case, 25 assertions |
| `tools\validate_ui_stress.bat` (X1) | 87.85 s | PASS; direct surface-command matrix and zero DX12 errors |
| `tools\validate_full.bat` (X1) | 148.65 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X2) | 19.72 s | PASS; zero warnings/errors |
| direct Release build (X2) | 49.56 s | PASS; production macro path, zero warnings/errors |
| focused scene proceed-policy test (X2) | 2.05 s | PASS; 1 case, 9 assertions |
| `tools\validate_automation.bat` (X2) | 40.46 s | PASS; combined replay/prediction/development-UI/Ctrl+0 lane |
| `tools\validate_full.bat` (X2) | 166.92 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (G1) | 19.20 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G1 run 1) | 78.30 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G1 run 2) | 55.00 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G1 run 3) | 55.10 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G1) | 61.58 s | PASS; bounded PID-scoped run, crash-free |
| direct Automation build (G2) | 19.20 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G2 run 1) | 79.70 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G2 run 2) | 55.10 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G2 run 3) | 55.70 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G2) | 61.96 s | PASS; bounded PID-scoped run, crash-free |
| direct Automation build (G3) | 21.87 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G3 run 1) | 66.98 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G3 run 2) | 55.18 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G3 run 3) | 55.24 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G3) | 61.70 s | PASS; bounded PID-scoped run, crash-free |
| replay visual fidelity (G3, one engine generation) | 447.61 s | PASS; all positive/negative controls, zero refresh |
| direct Automation build (G4) | 23.95 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G4 run 1) | 75.40 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G4 run 2) | 56.0 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G4 run 3) | 55.57 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G4) | 62.11 s | PASS; PID 39000, bounded PID-scoped run, crash-free |
| Debug DX12 architecture build/test (G5) | 26.50 s | PASS; single-path normal/capture contracts |
| direct Automation build (G5) | 18.31 s | PASS; zero warnings/errors |
| Legacy / ImGui / text-only / capture probes (G5) | 4.74 s | PASS; all exit 0, ImGui 5 frames / 77 draws |
| replay visual fidelity (G5, one engine generation) | 454.96 s | PASS; 2,401 ticks, all positive/negative controls, zero refresh |
| `tools\validate_format.bat` (G5 preflight correction) | 12.78 s | PASS; one touched header formatted |
| `tools\validate_full.bat` (G5 closure tip) | 177.73 s | PASS; CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_perf.bat` (G5) | 104.76 s | PASS; zero gameplay/reserve violations and no regression |
| `tools\run_graphics_stress.bat 1` (G5) | 62.66 s | PASS; PID 27628, bounded PID-scoped run, crash-free |
| `tools\validate_dx12_renderer.bat` (M2 run 1) | 79.64 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M2 run 2) | 56.00 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M2 run 3) | 56.14 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (M2) | 62.62 s | PASS; 13,045 frames, 358 scene loads, graceful PID-scoped stop, empty stderr |
| `tools\validate_full.bat` (M2) | 157.12 s | PASS; all CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_dx12_renderer.bat` (M3 run 1) | 79.46 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M3 run 2) | 56.07 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M3 run 3) | 55.78 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (M3) | 62.28 s | PASS; 13,149 frames, 361 scene loads, graceful PID-scoped stop, empty stderr |
| `tools\validate_perf.bat` (M3) | 110.79 s | PASS; absolute budgets and DX12/physics comparisons, no regressions |
| `tools\validate_full.bat` (M3) | 144.36 s | PASS; all CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_format.bat` (M3 final) | 13.41 s | PASS; 276 headers aligned and all source formatted |
| `tools\validate_dx12_renderer.bat` (M4) | 78.0 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (M4) | 61.0 s | PASS; 12,663 frames, 348 scene loads, graceful PID-scoped stop, empty stderr |
| Debug DXR-capability render-suite probe (M4) | 7.0 s | PASS; exit 0, empty stderr, `supported=1 tier=11` |
| `tools\validate_full.bat` (M4) | 156.0 s | PASS; 329 cases/61,354 assertions, all CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_full.bat` (M5 final) | 149.05 s | PASS; 329 cases/61,354 assertions, all CPU/coverage and five runtime lanes, zero DX12 errors, 44,401-line physics CSV byte-exact |
| `tools\validate_perf.bat` (M5 final) | 106.07 s | PASS; DX12 0.7245 ms average / 1.2042 ms P99, zero gameplay/reserve policy violations |
| `tools\run_graphics_stress.bat 1` (M5 final) | 61.99 s | PASS; 12,239 frames, 336 scene loads, graceful PID-scoped stop, empty stderr, 19 PSO misses fixed |
| platform-profiler 10-frame probe (M5 final) | 1.28 s | PASS; exit 0, marker emission requested/enabled, empty stderr |
| focused tornado force witness (T1) | 4.00 s | PASS; 1 case, 7 assertions, exact preserved body-state bits |
| `tools\validate_full.bat` (T1 final) | 199.48 s | PASS; all CPU/coverage/runtime lanes, zero DX12 errors, unchanged images, 44,401-line physics CSV byte-exact |
| `tools\validate_perf.bat` (T1) | 106.09 s | PASS; zero steady-gameplay allocations and no DX12/physics regression |
| replay visual fidelity (T1 final, one engine generation) | 440.53 s | PASS; 2,401 ticks, causal/durable-artifact proof and all negative controls |
| `tools\validate_fast.bat` (T2 final) | 80.83 s | PASS; format/metadata/size gates and zero-warning Profile/Debug builds |
| `tools\validate_dx12_renderer.bat` (T2 final) | 57.63 s | PASS; zero InfoQueue errors and all three screenshots within committed thresholds |
| `tools\run_graphics_stress.bat 1` (T2 final) | 62.74 s | PASS; PID 17628, bounded PID-scoped stop, crash-free, empty stderr |
| allocation policy self-test + repository scan (T3) | 9.37 s | PASS; 412 files, zero allowlist errors |
| `tools\validate_perf.bat` (T3 unchanged-tip rerun) | 108.58 s | PASS; zero gameplay/reserve violations and no DX12/physics regression |
| replay visual fidelity (T3 reconciled, one engine generation) | 452.2 s | PASS; 2,401 ticks and all positive/negative controls; hash-only provenance reconciliation |
| `tools\validate_dx12_renderer.bat` (T3 final) | 57.14 s | PASS; 43 fresh shader stages, zero InfoQueue errors, accepted captures |
| `tools\run_graphics_stress.bat 1` (T3 final) | 61.71 s | PASS; PID 48420, bounded PID-scoped stop, crash-free, empty stderr |
| `tools\validate_full.bat` (T3 final) | 145.4 s | PASS; CPU umbrella and five runtime lanes, byte-exact physics |
| `tools\validate_physics.bat` (T3 final) | 55.62 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_fast.bat` (RP1 final) | 75.65 s | PASS; format/metadata/size, Profile/Debug builds, 336 tests / 68,633 assertions |
| allocation self-test + repository scan (RP1) | 9.54 s | PASS; 412 files, zero allowlist errors |
| `tools\validate_perf.bat` (RP1) | 108.5 s | PASS; allocation/performance comparisons clean |
| strict two-generation Replay allocation (RP1) | 16.87 s | PASS; frame 180, zero gameplay/reserve-policy violations |
| `tools\validate_full.bat` (RP1) | 151.9 s | PASS; CPU umbrella and five runtime lanes, byte-exact physics |
| replay visual fidelity (RP1, one engine generation) | 447.2 s | PASS; 2,401 ticks and all positive/negative controls |
| focused Profile rebuild + doctests (RP2) | 27.53 s + 1.35 s | PASS; 336 tests / 68,633 assertions, byte-canonical artifact round trip |
| `tools\validate_physics.bat` (RP2) | 85.63 s | PASS; handle mirror smoke and 44,401-line CSV byte-exact |
| `tools\validate_full.bat` (RP2 final) | 153.8 s | PASS; CPU umbrella and five runtime lanes, accepted DX12 images, byte-exact physics |
| replay visual fidelity (RP2) | 447.0 s | PASS; 2,401 ticks, one generation/presentation, durable artifact and all controls |
| RP3 static boundary/identity/allocation/comment proofs | 2.2 s | PASS; zero forbidden/retired surfaces, exactly three owners, 57/57 comment audit |
| detached RP1 performance control | 157.28 s | PASS; DX12 frame average/p50 +1.8%/+1.2%, proving RP2-local regression |
| targeted RP3 alignment build + doctest | 10.81 s + 1.48 s | PASS; 1 case / 1 assertion pins offset 16 |
| `tools\validate_perf.bat` (RP3 final) | 137.29 s | PASS; DX12 +9.4%/+8.7%, allocation and physics comparisons clean |
| `tools\validate_full.bat` (RP3 final) | 145.73 s | PASS; 337 tests / 68,634 assertions, five runtime lanes |
| `tools\validate_physics.bat` (RP3 final) | 55.30 s | PASS; handle mirror and 44,401-line CSV byte-exact |
| strict two-generation Replay allocation (RP3 final) | 16.66 s | PASS; frame 180, zero gameplay/policy violations, three owners, zero failed growths |
| replay visual fidelity (RP3, one engine generation) | 441.56 s | PASS; 2,401 ticks, durable/causal proof and all controls |
| `tools\validate_fast.bat` (RP3 review hardening) | 56.35 s | PASS; 337 tests / 68,634 assertions, zero warnings/errors |
| hardened strict Replay allocation (RP3 final tip) | 16.72 s | PASS; fresh report, frame 180, exactly two generations, zero gameplay/policy violations |
| replay query identity witness | 0.4 s | PASS; existing artifact emits `sceneObjectId: 6`, no retired label |
| `tools\validate_fast.bat` (RP3 final tool tip) | 55.46 s | PASS; 337 tests / 68,634 assertions, zero warnings/errors |
| `tools\validate_project_filters.bat` (RH1) | 2.6 s | PASS; 725 project/filter items, zero errors |
| `tools\validate_fast.bat` (RH1) | 23.0 s | PASS; 338 tests / 68,642 assertions, zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (RH1) | 23.8 s | PASS; zero InfoQueue errors, all three screenshots accepted |
| `tools\run_graphics_stress.bat 1` (RH1) | 60.9 s | PASS; 12,027 frames, 330 scene loads, PID 29132 bounded stop, empty stderr |
| focused Profile rebuild + lifecycle doctests (OF1) | 11.0 s + 9.9 s | PASS; 5 cases / 61 assertions |
| `tools\validate_project_filters.bat` (OF1 correction) | 2.6 s | PASS; 722 project/filter items, zero errors |
| `tools\validate_full.bat` (OF1 final) | 154.9 s | PASS; CPU umbrella, five runtime lanes, accepted DX12 images, byte-exact physics |
| focused Profile rebuild + lifecycle/input doctests (OF2) | 10.1 s + 1.9 s | PASS; 4 cases / 56 assertions |
| replay visual fidelity (OF2, one completed engine generation) | 409.3 s | BLOCKED; behavior reached oracle, config provenance expected `83401d…a3f4`, actual `bd0bb7…5d93`; no config/golden edit in OF2 |
| `tools\validate_full.bat` (OF2 final) | 151.7 s | PASS; CPU umbrella, five runtime lanes, zero DX12 errors, accepted images, byte-exact physics |
| focused Profile rebuild + lifecycle/same-batch doctests (OF3) | 6.5 s + 1.8 s | PASS; 4 cases / 51 assertions |
| `tools\validate_full.bat` (OF3 final) | 147.8 s | PASS; CPU umbrella, five runtime lanes, zero DX12 errors, accepted images, byte-exact physics |
| focused Profile rebuild + complete doctests (OF4) | 10.5 s + 3.3 s | PASS; 342 cases / 68,685 assertions |
| `tools\validate_full.bat` (OF4 first attempt) | 7.2 s | BLOCKED then resolved; formatting-only preflight identified three touched implementations and two touched headers |
| `tools\validate_full.bat` (OF4 final) | 146.5 s | PASS; CPU umbrella, five runtime lanes, zero DX12 errors, accepted images, byte-exact physics |
| focused Profile rebuild + Simulation lifecycle doctests (OF5) | PASS | Zero warnings; 6 cases / 177 assertions |
| independent rubber-duck ownership review (OF5) | 7.3 min | PASS after remediation; ten inputs, exact render authority, ≤3-file witness, zero forbidden seams |
| `tools\validate_full.bat` (OF5 final) | 99.2 s | PASS; CPU umbrella, five runtime lanes, zero DX12 errors, accepted images, byte-exact physics |
| `tools\run_graphics_stress.bat 1` (OF5 final) | 61.0 s | PASS; PID 61368 bounded stop, crash-free |
| focused Profile build + Replay tests (RC1) | 8.3 s + 1.52 s | PASS; zero warnings, 10 cases / 180 assertions |
| `tools\validate_tests.bat` (RC1 final) | 3.7 s | PASS; 99/99 project/filter items, 343 cases / 68,693 assertions |
| replay visual fidelity (RC1, one engine generation) | 423.6 s | BLOCKED; one process/generation and 16/72 controls pass, then unchanged config-provenance mismatch |
| `tools\validate_full.bat` (RC1 formatting attempt) | 13.2 s | BLOCKED then resolved; one touched header needed the repository comment-alignment pass |
| `tools\validate_full.bat` (RC1 final) | 167.4 s | PASS; CPU/coverage umbrella and five runtime lanes, zero DX12 errors, accepted images, byte-exact physics |
| focused Profile build + Prediction doctests (RC2) | 3.1 s + 1.9 s | PASS; zero warnings, 4 cases / 24 assertions |
| `tools\validate_tests.bat` (RC2 final) | 11.1 s | PASS; 344 cases / 68,699 assertions, 99/99 test project/filter items |
| `tools\validate_fast.bat` (RC2 final) | 62.1 s | PASS after formatting/filter metadata corrections; 730/730 production items, zero-warning Profile/Debug builds |
| allocation self-test + repository scan (RC2) | 9.4 s | PASS; 414 files, zero allowlist errors; same three registered owners/caps |
| replay visual fidelity (RC2, one engine generation) | 422.9 s | BLOCKED; launcher shape and 16/72 controls pass, then unchanged config-provenance mismatch; no retry or metadata edit |
| `tools\validate_full.bat` (RC2 final) | 108.2 s | PASS; CPU/coverage umbrella and five runtime lanes, accepted DX12 images, byte-exact 44,401-line physics CSV |
| focused Profile build (RR3 final) | 17.1 s | PASS; zero warnings/errors after lifecycle extraction and formatting |
| `tools\validate_fast.bat` (RR3 final) | 58.2 s | PASS; format, 743/743 project/filter items, tests, and Profile/Debug builds |
| allocation self-test + repository scan (RR3) | 9.3 s | PASS; 427 files, zero allowlist errors; existing cold-owner rows relocated only |
| `tools\validate_dx12_renderer.bat` (RR3) | 24.7 s | PASS; zero InfoQueue errors and all three committed images accepted |
| `tools\run_graphics_stress.bat 1` (RR3) | 60.9 s | PASS; PID 47480 bounded stop, crash-free |
| `tools\validate_full.bat` (RR3 final) | 121.2 s | PASS; CPU/coverage and five runtime lanes, accepted DX12 images, byte-exact physics |
| focused Profile build (RR4 final) | 17.1 s | PASS; zero warnings/errors after exact backend-view narrowing |
| `tools\validate_dx12_renderer.bat` (RR4) | 39.8 s | PASS; zero InfoQueue errors and all three committed images accepted |
| `tools\run_graphics_stress.bat 1` (RR4) | 60.9 s | PASS; PID 46552 bounded stop, crash-free |
| `tools\validate_full.bat` (RR4 final) | 118.7 s | PASS; CPU/coverage and five runtime lanes, accepted DX12 images, byte-exact physics |
| focused Profile build (RR5 remediation final) | 8.17 s | PASS; zero warnings/errors after extracting `RenderModelFramePublisher` |
| `tools\validate_full.bat` (RR5 final) | 145.30 s | PASS; CPU/coverage and five runtime lanes, zero DX12 errors, accepted images, physics hash `0x953D97A226665242`, byte-exact 44,401-line CSV |
| three final `tools\validate_dx12_renderer.bat` repeats (RR5) | 23.23 / 23.38 / 23.34 s | PASS; every repeat reported zero InfoQueue errors and accepted all committed images |
| `tools\run_graphics_stress.bat 1` (RR5) | 60.91 s | PASS; PID 61432 bounded stop, crash-free |
| replay visual fidelity (RD0, sole invocation) | 432.83 s | PASS; one process/generation, 2,401 ticks, 17 cases / 75 assertions, all controls, zero refresh |
| replay visual fidelity (RD1, sole invocation) | 430.23 s | PASS; one process/generation, 2,401 ticks, 17 cases / 75 assertions, all controls, zero refresh |
| focused Profile build + Replay doctests (RD2 C1) | 3.56 s + 1.87 s | PASS; zero warnings/errors, 52 cases / 786 assertions |
| `tools\validate_format.bat` (RD2 C1 final) | 13.37 s | PASS; all implementation and header formatting clean |
| `tools\validate_tests.bat` (RD2 C1 final) | 11.34 s | PASS; 345 cases / 68,702 assertions, zero warnings/errors |
| replay visual fidelity (RD2 C1, sole invocation) | 442.68 s | PASS; one process/generation/presentation, 2,401 ticks, all positive/negative controls, zero refresh |
| focused Profile build + Replay doctests (RD2 C2) | 10.86 s + 0.05 s | PASS; zero build errors, 52 cases / 786 assertions |
| allocation self-test + repository scan (RD2 C2) | 9.20 s | PASS; 429 files, zero allowlist errors; allowlist relocation only |
| `tools\validate_fast.bat` (RD2 C2 final) | 44.73 s | PASS; format/metadata and zero-warning Profile/Debug builds |
| strict two-generation Replay allocation (RD2 C2) | 15.98 s | PASS; frame 180, exactly two generations, zero gameplay/policy violations |
| replay visual fidelity (RD2 C2, sole invocation) | 430.84 s | PASS; one process/generation/presentation, 2,401 ticks, all controls, zero refresh |
| focused Profile build + Replay doctests (RD2 C3) | 4.19 s + 2.41 s | PASS; zero build errors, 53 cases / 791 assertions including lookup policy |
| `tools\validate_format.bat` (RD2 C3 final) | 13.48 s | PASS; all implementation and header formatting clean |
| `tools\validate_tests.bat` (RD2 C3 final) | 8.75 s | PASS; 346 cases / 68,707 assertions, zero warnings/errors |
| replay visual fidelity (RD2 C3, sole invocation) | 439.01 s | PASS; one process/generation/presentation, 2,401 ticks, all controls, zero refresh |
| focused Profile build + Replay doctests (RD2 C4) | 4.32 s + 1.91 s | PASS; zero warnings/errors, 53 cases / 791 assertions |
| replay visual fidelity (RD2 C4, sole invocation) | 444.30 s | PASS; one process/generation/presentation, 2,401 ticks, all positive/negative controls, zero refresh |
| focused Profile build + Replay/causality doctests (RD2 C5) | 11.37 s + 2.32 s + 0.02 s | PASS; zero warnings/errors, 53 Replay cases / 791 assertions and 1 causality case / 16 assertions |
| `tools\validate_format.bat` (RD2 C5 final) | 13.42 s | PASS; all implementation and header formatting clean |
| `tools\validate_tests.bat` (RD2 C5 final) | 3.42 s | PASS; 346 cases / 68,707 assertions, zero warnings/errors |
| replay visual fidelity (RD2 C5, sole invocation) | 443.75 s | PASS; one process/generation/presentation, 2,401 ticks, all positive/negative controls, zero refresh |
| Profile startup log probe (RD3) | 4.49 s build + 3.13 s run | PASS; stdout ERROR, stderr FATAL, exit 1, no modal/hang |
| `tools\validate_tests.bat` (RD3 final) | 10.54 s | PASS; 346 cases / 68,715 assertions |
| strict Replay allocation (RD3 final) | 12.78 s | PASS; two-generation policy clean |
| `tools\validate_replay_v2_artifact.bat` (RD3 final) | 54.67 s | PASS; save/restore and hash verification |
| replay visual fidelity (RD3 final) | 471.77 s | PASS; one process/generation/presentation, 2,401 ticks, all controls |
| `tools\validate_physics.bat` (RD3 final) | 23.08 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_full.bat` (RD3 final) | 136.17 s | PASS; CPU umbrella, five runtime lanes, zero DX12 errors |
| wide-signature W1-corrected self-test + scan | 26.71 s | PASS; explicit stdout self-test/301-row PASS lines and `EXIT=0` |
| `tools\validate_fast.bat` (wide-signature W0) | 23.46 s | PASS; 346 cases / 68,715 assertions, zero warnings/errors, captured terminal pass line |
| wide-signature W1 corrected scan + rulings | 26.52 s | PASS; 301/301 rows ruled, explicit stdout PASS and `EXIT=0` |
| `tools\validate_fast.bat` (wide-signature W1) | 24.32 s | PASS; 346 cases / 68,715 assertions, zero warnings/errors |
| Profile build (wide-signature W2 UI input) | 11.63 s | PASS; zero warnings/errors |
| corrected inventory (wide-signature W2 UI input) | 26.44 s | PASS; 299 rows, both UI targets absent |
| `tools\validate_full.bat` (wide-signature W2 UI first attempt) | 7.12 s | BLOCKED then resolved; one touched file needed formatting |
| `tools\validate_full.bat` (wide-signature W2 UI input) | 131.72 s | PASS; CPU umbrella, five runtime lanes, byte-exact physics |
| `tools\validate_full.bat` (wide-signature W2 UI final tip) | 124.39 s | PASS; CPU umbrella, five runtime lanes, byte-exact physics |
| Profile build (wide-signature W2 Physics first attempt) | 22.78 s | BLOCKED then resolved; three missing namespace qualifiers |
| Profile build (wide-signature W2 Physics final) | 10.44 s | PASS; zero warnings/errors |
| focused Replay doctests (wide-signature W2 Physics) | 2.47 s | PASS; 53 cases / 799 assertions |
| corrected inventory (wide-signature W2 Physics) | 26.62 s | PASS; 297 rows, both restore targets absent |
| `tools\validate_full.bat` (wide-signature W2 Physics first attempt) | 13.55 s | BLOCKED then resolved; touched header formatting |
| `tools\validate_format.bat` (wide-signature W2 Physics) | 13.94 s | PASS |
| `tools\validate_full.bat` (wide-signature W2 Physics final) | 178.81 s | PASS; CPU umbrella, five runtime lanes, byte-exact physics |
| Replay visual fidelity (wide-signature W2 Physics) | 427.18 s | PASS; one process/generation/presentation, 2,401 ticks, all controls |
| Replay allocation policy (wide-signature W2 Physics) | 4.22 s | PASS; strict two-generation policy clean |
| Replay v2 artifact (wide-signature W2 Physics) | 33.16 s | PASS |
| Replay scrub (wide-signature W2 Physics) | 431.04 s | PASS; one-presentation fidelity and all false-pass controls |
| focused N26-3 owner-request doctests | PASS | 2 cases / 24 assertions; held samples coalesce without refresh, finish emits one refresh, preview clears only for the armed generation |
| `tools\validate_alt_velocity_visualization.bat` (N26-3 final) | PASS | instant and amortized paths; held preview visible, zero superseded restarts, click-off release retains preview through replacement |
| `tools\validate_replay_scrub.bat` (N26 final) | PASS | 17 cases / 75 assertions; 2,401-tick 200-body fidelity and every causal, artifact, determinism, and duplicate-generation control |
| `tools\validate_replay_allocation_policy.bat` (N26 final) | PASS | replay growth policy and registered-owner inventory clean |
| `tools\validate_dependency_graph.bat` (N26 final) | PASS | Runtime package and Replay-family directions clean |
| `tools\validate_perf.bat` (N26 final) | 58.55 s | PASS; allocation guard, structural selected-path proof, DX12 and Physics comparisons |
| `tools\validate_full.bat` (N26 final) | PASS | 402 doctests, required runtime lanes, accepted DX12 baselines, byte-exact 44,401-line Physics regression |
| `tools\run_graphics_stress.bat 1` (N26 final) | 60.83 s | PASS; PID 34120 bounded stop, crash-free, descriptor churn proof complete, empty stderr |
| wide-signature RG1 self-test + strict scan | 27.6 s | PASS; 33 current trigger rows, 28 retain-owner, 5 repair-plan, zero unruled/stale |
| `tools\validate_fast.bat` (RG1 clean snapshot) | 228 s | PASS; format/metadata/dependencies, all three ownership inventories, Profile/Debug builds, and unit tests |
| wide-signature RG2 self-tests + strict scans | 28.2 s max | PASS; 28/28 current trigger rows ruled, 86/86 aggregate rows ruled, 1/1 extraction scar ruled |
| clean-worktree Profile build (RG2) | 45.9 s | PASS; zero warnings/errors |
| `tools\validate_tests.bat` (RG2 clean snapshot) | 14.8 s | PASS; 423/423 cases and 2,410,303 assertions |
| independent rubber-duck ownership review (RG3) | PASS after remediation | Initial review found pending branch completion and unproved rollback could false-pass; transaction-owned proof states and two fatal probes closed both, and repeat review found zero blockers |
| `tools\validate_tests.bat` (RG3 remediation) | 35.6 s | PASS; 424/424 cases and 2,410,325 assertions |
| `tools\validate_fast.bat` (RG3 final source) | 211.8 s | PASS; formatting, metadata, dependencies, all three ownership inventories, Profile/Debug builds, and tests |
| Replay visual fidelity (RG3 final source) | 439.8 s | PASS; one engine process/generation/presentation, 2,401 ticks, all positive/negative controls, zero refresh |
| `tools\validate_full.bat` (RG3 final source) | 408.7 s | PASS; 424 cases / 2,410,325 assertions, all CPU/runtime lanes, accepted DX12 images, byte-exact 44,401-line Physics CSV |
| PhysicsFixedList FC0 census | documentation-only; independent review clear | One production Replay prediction `PhysicsEngine` assignment reaches 98 lists; direct list/ColliderStore transfers are test-only; no production moves, by-value paths, or value queues |
| SbResult SR1 ownership decision | documentation-only | App-composed 256-slot immutable store, 16-byte leased result, corrected 250/256 decision-tip conservative bound, last-copy reclamation, stale-generation/cross-store detection, bounded copy-out, explicit store injection, and no compatibility wrapper |
| SbResult SR2 implementation | focused implementation proof | 16-byte result, 159,760-byte store, SR2-reported 250/256 census later corrected by SR3 to 252/256, exact concurrent diagnostic bytes and active-lease destruction Lane F pass, every normal App exit reports active/high-water/capacity, clean-worktree `validate_fast` and all ownership inventories pass, and comment audit is 166/166 |
| SbResult SR3 closure | final-source broad proof | 252/256 conservative bound, exact size/payload/lease/identity tests, five DX12 epoch witnesses, Automation owner repair, 436 cases / 2,419,127 assertions, performance/full/stress gates, 18/18 comment audit, and accepted independent review |
| Vector dot-product VD2 closure | final-source broad proof | 173 configuration-complete uses across 36 files, one definition/179 calls, exact cancellation witness, 437 cases / 2,419,129 assertions, byte-exact and deep Physics, performance, Replay visual, full gates, 36/36 comment audit, and accepted independent review |

The first full gate found one Automation-only orphaned `GameObjects`
using-directive after the SkullScope namespace move. It was removed before the
targeted Automation and final full passes.

## Next Handoff

The Claim Integrity Campaign is complete and excluded under rule 4. The Gate
Blind Spot Campaign is active at **16/21 (76%)**. Solver Diagnostic Hot-Path Cost
HP0-HP3 are complete: count-only execution avoids payload construction,
trace-only vector loads, the diagnostic `sqrtf`, and row-local capacity
compares; Physics, Replay, overlay/SkullScope, and allocation evidence remain
exact; the same Profile workload records a 40.90% mean `SolveRows` reduction.
Runtime Contract Hygiene CH0-CH2 are complete with status-free frame boundaries,
a truthful Quaternion contract, zero engine throw expressions, byte-exact
Physics, and passing focused/full evidence. Engine Glossary Consolidation GC0-
GC3 establish the 321-term canonical glossary, remove all 1,208 shared source
definitions and 107 filler instances, replace all 117 basename-led summaries,
and close the checklist at 575/575 with zero deferred.
Angular Impulse Frame Correctness AI0 rules the application point a world-space
center-relative offset, pins the rotated anisotropic mismatch in an
expected-failure test, and predicts zero committed artifact movement. AI1
replaces the full triangular clear/rescan with canonical compact worker
prefixes and a linear active-pair reduce while preserving exact worker-count
results; the same Profile scene records 14.19% lower mean Reduce time. AI2
shares the world/body inertia conversion across gameplay, world-force, and
contact paths; its focused cross-path and exact-sphere tests, unit gate, core
Physics gate, and deep Physics gate pass without baseline movement. Owner
direction on 2026-08-01 removed the redundant extra `at_rest` frame assertion:
the existing deep lane already hashes all 7,649,427 bytes / 54,001 lines and
matches the committed signature. AI3 completed without behavior: it registered
Vector Frame Contract Closure for the mixed angular-drag axes, authored offset
schema/outlier, `VectorReflect` convention, and explicit public frame matrix.
AI4 is owner-accepted and closed with a zero-delta post-acceptance rerun.

The owner registered the **Gate Blind Spot Campaign (2026-07-31)** from a
source-and-tests-only engine review at tip `1967a863`. MASTER and SessionState
agree the active/future ledger is now **16/21 (76%)**. Run the five plans in
order:

1. `Plans/TODO/solver-diagnostic-hot-path-cost.md` (4/4, HP0-HP3) — complete;
   exact Physics, Replay, and enabled diagnostic artifacts are preserved without
   refresh, allocation ownership is unchanged, and the Profile win is recorded.
2. `Plans/TODO/runtime-contract-hygiene.md` (3/3, CH0-CH2) — complete; CH0
   removed droppable frame failure statuses, CH1 repaired the `Quaternion.h`
   surface and normalized-axis precondition, and CH2 removed the last engine
   throw expression under a compile-time relocation contract.
3. `Plans/TODO/engine-glossary-consolidation.md` (4/4, GC0-GC3) — complete;
   321 shared definitions are canonical, 964 local definitions remain in their
   owning files, all 117 basename-led summaries are repaired, and the 575-row
   checklist is closed with strict inventory and independent review evidence.
4. `Plans/TODO/angular-impulse-frame-correctness.md` (5/5, AI0-AI4) — complete
   with explicit owner acceptance, a no-blocker independent review, and zero
   artifact movement; retained until aggregate campaign closure.
5. `Plans/TODO/vector-frame-contract-closure.md` (0/5, VF0-VF4) — **the current
   plan.** VF0 owns the complete frame/caller census and pre-change artifact
   prediction; no phase carries baseline-refresh authority.

Plans 1-3 landed without an owner decision. Plan 4's owner gate is accepted and
Plan 5 may proceed. No plan in this campaign carries bounded-divergence
authority.
