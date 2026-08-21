# Full Validation Time And Value Audit

Date: 2026-08-20
Status: Active; 5/6 phases complete
Impact area: validation orchestration, build configuration, repository
inventories, CPU test and coverage gates, runtime probes, diagnostics, and
validation governance
Owner: Validation pipeline, with build, test, Physics, Rendering, Replay, and
governance owners ratifying changes to their evidence
Priority: Second active queue item; begins after `INVARIANT_HARDENING` IH7
Commit name: `VALIDATION_TIME_AUDIT`

## Goal

Measure the complete critical path of
`tools\agent_validate.bat --plan-completion`, explain the cost and unique value
of every nested stage, and shorten the gate without weakening its ability to
catch defects. The audit must distinguish build invalidation, repository scans,
ordinary test execution, coverage instrumentation, graphics/Physics workloads,
and informational diagnostics instead of reporting only one total duration.

The result is a decision-quality map: each stage has measured cost, the defect
class it uniquely owns, overlap with other stages, historical or false-pass
evidence, and a disposition. A slow stage is not removed merely because it is
slow. A stage may leave the blocking path only when its evidence is duplicated,
can be cached with exact currentness proof, can run safely in parallel, or is
explicitly informational and remains available through an appropriate focused,
hosted, or scheduled lane.

## Registration And Ordering

- The master ledger registers six portfolio tasks VTA0 through VTA5 under
  commit token `VALIDATION_TIME_AUDIT`.
- `INVARIANT_HARDENING` remains binding through IH7. VTA0 follows it so the
  audit measures one stable validation topology and does not change the gate
  beneath an in-flight plan-completion proof.
- This plan grants no authority to remove a mandatory defect oracle, lower a
  coverage floor, weaken a baseline comparison, refresh a golden, or turn a
  blocking failure into a warning. Those decisions require evidence and the
  affected subsystem owner's explicit ruling.
- Full validation remains a terminal plan gate. VTA0-VTA4 measure child
  commands and controlled equivalents; the one actual terminal full run belongs
  to VTA5 after review.

## Starting Evidence

The 2026-08-20 read-only diagnosis established enough evidence to justify the
audit, but none of these observations is a final performance conclusion:

- `validate_full.bat` serially builds Automation and Debug, delegates Profile
  and ownership preflight to `validate_fast`, runs six CPU targets, then runs
  Automation, DX12, Physics, and the replay-prediction spike diagnostic.
- A warm `validate_fast.bat` observation took approximately six minutes even
  though its embedded runtime note still says approximately 30 seconds.
- `TestOutput/validation/PREDICT_SOLVER_DETAIL_PSD7_agent_validate.log` spans
  approximately nine and a half minutes before its owner-controlled Physics
  baseline stop, despite its three top-level build invocations being warm.
- The main doctest run in that log reports 604 cases, 2,484,279 assertions, and
  approximately 39 seconds of summed case duration. Coverage then runs the
  complete Debug test executable again under OpenCppCoverage.
- The final replay-prediction diagnostic has a 105-second watchdog and is
  informational: its exit code does not change full-validation success.
- `validate_build.bat` does not pass MSBuild `/m`; whether parallel build nodes
  are safe and useful remains an experiment, not an assumed fix.

## Measurement Contract

### Scenarios

All destructive or timestamp-invalidating experiments run in a verified,
disposable clone outside the live worktree. Do not delete or invalidate the
user's existing build outputs.

| Scenario | Purpose | Minimum evidence |
|---|---|---|
| Warm no-op | Expose fixed orchestration, scan, test, and runtime cost when outputs are current | Three child-stage samples; report median, minimum, maximum, and concurrent machine load |
| Leaf source invalidation | Measure a representative `.cpp` edit that should rebuild a narrow set | Three samples in the disposable clone with the exact invalidated file recorded |
| Broad header invalidation | Measure the cost of a widely included contract change | One sample plus compiler/project fan-out from MSBuild performance output |
| Fresh clone/build | Establish the cold upper bound without deleting live outputs | One complete build/preflight sample with toolchain, cache, antivirus, disk, and machine state recorded |
| Historical full run | Ground the audit in real plan-completion behavior and failure positions | Parse every suitable retained full log; mark missing timestamps and never invent phase durations |

If repeated samples vary materially, capture additional runs or identify the
competing process, thermal, disk-cache, antivirus, GPU, or network state. Do not
hide variance behind a single average.

### Per-Process Fields

The timing owner records these fields for every external process where Windows
exposes them reliably:

- stable stage id, parent stage id, exact command, working directory, and
  relevant environment overrides;
- UTC and local start/end timestamps, wall time, exit code, and whether the
  stage was executed, skipped, reused, or short-circuited;
- process CPU time, peak working set, and available read/write IO counters;
- build configuration and currentness state, test case/assertion counts, engine
  frame/workload count, and produced artifact paths;
- stdout/stderr log paths with bounded console output;
- machine CPU, memory, disk, GPU, power-mode, toolchain, and competing-workload
  context sufficient to compare samples honestly.

Instrumentation must preserve child stdout, stderr, environment, Ctrl+C/exit
semantics, fail-fast behavior, and the exact child exit code. A timing failure
must never convert a failed gate into success. Raw timing artifacts live under
`TestOutput/validation/VALIDATION_TIME_AUDIT/`; the plan and commit bodies retain
only bounded summaries and hashes.

## Complete Stage Inventory

VTA0 must reconcile this checklist against the actual batch/Python call graph.
Every external command receives its own timing row; a skipped branch remains a
row with the reason it did not run.

- [ ] Full phase 0A: Automation solution build.
- [ ] Full phase 0B: Debug solution build.
- [ ] Fast 1/9: formatting self-tests, related-path check, formatting scan, and
  aligned-header check.
- [ ] Fast 2/9: project-filter validation.
- [ ] Fast 3/9: dependency proof, fixtures, and repository scan.
- [ ] Fast 4/9: each ownership/determinism self-test and each repository scan
  timed separately; do not collapse all Python inventories into one row.
- [ ] Fast 5/9: staged-file size check.
- [ ] Fast 6/9: Profile solution build.
- [ ] Fast 7/9: explicit preflight-only test deferral.
- [ ] Fast 8/9: ready-build verification or parent-authorized skip.
- [ ] Fast 9/9: compiled-symbol reachability scan across Automation, Debug,
  Profile, and standalone test evidence.
- [ ] CPU umbrella dependency skip/preflight decision.
- [ ] CPU 1/6: Profile test filter check, build, and ordinary doctest run.
- [ ] CPU 2/6: coverage self-test, Debug test build, OpenCppCoverage launch,
  XML export, and floor summary.
- [ ] CPU 3/6: Debug and Release runtime-interaction-policy builds and runs.
- [ ] CPU 4/6: scene-parser build and run.
- [ ] CPU 5/6: UI-boundary build and run.
- [ ] CPU 6/6: DX12-architecture build and run.
- [ ] Automation: Profile negative boundary, incremental Automation build, and
  positive replay/prediction smoke.
- [ ] DX12 1/8 through 8/8: shader freshness, formatting, Profile reuse/build,
  cleanup, renderer workload, artifact presence, InfoQueue/log checks, baseline
  comparison, and ready-build handling.
- [ ] Physics 1/5 through 5/5: Debug reuse/build, lifecycle smoke, varied-scene
  regression, baseline comparison, and ready-build handling.
- [ ] Replay-prediction spike diagnostic: analyzer unit tests, script check,
  generated scene, four-generation engine workload, report checks, and spike
  analysis.
- [ ] Wrapper-only overhead, batch startup, tool discovery, artifact cleanup,
  logging, and any unexplained difference between child sums and parent wall
  time.

## Value And Redundancy Review

Every checklist row receives one decision record with these questions answered:

1. What exact defect class or currentness invariant does this stage own?
2. Which other mandatory lane observes the same defect, and is that observation
   truly equivalent in configuration, inputs, and failure semantics?
3. What repository commit, CI failure, negative fixture, or injected false-pass
   control proves the stage can fail for the defect it claims to catch?
4. Is the result actionable before merge, only at plan completion, or merely
   diagnostic after the blocking evidence is already complete?
5. Does the stage require fresh source, a particular configuration/object root,
   GPU state, a baseline, network state, or output from a preceding stage?
6. Can exact input/output fingerprints prove safe reuse, or would caching risk a
   stale false pass?
7. Can it run concurrently without contending for the same output directories,
   executables, PDBs, GPU, baselines, or generated artifacts?
8. Which disposition is justified: retain in order, reorder, parallelize,
   cache, merge with another stage, move to a focused gate, move to hosted or
   scheduled validation, keep informational outside the blocking path, or
   remove?

The final decision matrix reports wall-time share and cumulative critical-path
cost, but time is evidence rather than a frozen budget. No stage is kept because
it is cheap, and no stage is removed because it is expensive.

## Candidate Experiments

These are hypotheses to test individually, not pre-approved implementation:

- add durable nested timings before changing orchestration;
- parallelize independent read-only Python inventories while preserving bounded
  output and first-failure attribution;
- cache repeated parse/index work across inventories only if exact source and
  ruling fingerprints prove currentness;
- evaluate MSBuild `/m` within one configuration while keeping configurations
  sequential and output roots isolated;
- remove genuinely redundant incremental builds or replace them with explicit
  freshness proof;
- decide whether ordinary Profile doctests and the full Debug coverage run both
  belong in the terminal critical path, based on configuration-specific signal;
- eliminate repeated formatting/shader/tool discovery work when a trustworthy
  parent result can be passed down;
- move the non-blocking 105-second spike diagnostic after the blocking result or
  to a focused/scheduled lane while retaining its owner and artifacts;
- reduce engine workload only when a negative control proves the shorter input
  still reaches the same state and detects the seeded defect;
- improve fail-fast order using measured failure value and cost without moving
  a prerequisite behind its consumer.

Each accepted experiment needs an A/B measurement on the same machine state, a
semantic/failure-propagation control, and an owner ruling. Rejected experiments
remain recorded with the reason so a later audit does not repeat them blindly.

## Phases

### VTA0 - Topology And Timing Instrumentation

- [x] Generate the reconciled stage manifest from current scripts and account
  for every external process and conditional branch.
- [x] Add or select a timing mechanism that obeys the measurement contract and
  has self-tests for success, child failure, timeout, missing executable,
  spaces/quoting, redirected logs, and Ctrl+C/termination propagation.
- [x] Prove instrumentation overhead with a bounded no-op/control workload and
  record its resolution limits.
- [x] Capture the machine/toolchain manifest and artifact schema.

### VTA1 - Controlled Baseline Measurements

- [x] Measure the warm, leaf-source, broad-header, fresh-build, and historical
  scenarios without repeatedly invoking the terminal full wrapper.
- [x] Produce the nested flame/timeline table, parent-versus-child reconciliation,
  build fan-out, CPU/coverage comparison, and runtime-workload breakdown.
- [x] Identify variance and repeat noisy samples rather than selecting the most
  favorable run.
- [x] Record the baseline critical path and the largest cumulative contributors.

### VTA2 - Evidence Value Audit And Owner Rulings

- [x] Complete the eight-question decision record for every stage.
- [x] Mine bounded Git/CI/local logs for real catches and distinguish them from
  assertions that a stage is useful.
- [x] Add or run false-pass controls where a stage lacks credible failure proof.
- [x] Obtain owner rulings for every proposed removal, demotion, merge, cache,
  or workload reduction and freeze the approved experiment set.

### VTA3 - Build And Preflight Critical-Path Reduction

- [x] Run approved build parallelism, freshness, inventory sharing/caching,
  scan parallelism, and repeated-check experiments one at a time.
- [x] Implement only experiments with equivalent failure semantics and a clear
  same-state timing improvement.
- [x] Preserve readable failure ownership and deterministic output; do not trade
  wall time for intermittent file locks or hidden background work.
- [x] Re-measure all VTA1 preflight/build scenarios after the accepted changes.

### VTA4 - CPU And Runtime Critical-Path Reduction

- [x] Run approved test/coverage, Automation, DX12, Physics, and informational
  diagnostic experiments one at a time.
- [x] Prove any reduced workload with a seeded-defect/negative control and any
  moved lane through the mandatory hosted/focused/scheduled entry point that now
  owns it.
- [x] Preserve zero-warning builds, coverage-floor enforcement, DX12 InfoQueue
  validation, byte-exact Physics comparison, baseline authority, and fail-fast
  exit propagation.
- [x] Re-measure the affected VTA1 scenarios after the accepted changes.

### VTA5 - Terminal Proof And Governance Closure

- [ ] Obtain independent review of timing integrity, cache/freshness safety,
  failure propagation, lost evidence, output-root races, and decision rulings.
- [ ] Run every changed focused gate, then one timed
  `tools\agent_validate.bat --plan-completion` terminal proof.
- [ ] Compare pre/post stage costs on equivalent scenarios and report absolute
  and relative critical-path change with variance, without presenting one lucky
  sample as the result.
- [ ] Update `AGENTS.md`, `README.md`, `Agentic/README.md`, `tools/README.md`,
  script headers, and the master/session ledgers with measured current runtimes
  and the final lane ownership.
- [ ] Delete this completed plan under repository convention after its closure
  evidence is retained in the commit and master ledger.

## Validation Map

| Change | Required evidence before its task commit |
|---|---|
| Timing tool only | Tool self-tests, failure-propagation controls, and `tools\validate_fast.bat` |
| Build invocation or project scheduling | Debug, Profile, and Automation builds from the controlled clone; build-config consistency self-test/repository scan; `tools\validate_fast.bat` |
| Fast/preflight inventories or orchestration | Changed tool self-tests and repository scans, preflight harness controls, and `tools\validate_fast.bat` |
| CPU umbrella, tests, or coverage | Wrapper failure-propagation controls, `tools\validate_all_cpu_tests.bat`, and direct coverage validation when its scope changes |
| Automation lane | `tools\validate_automation.bat` plus its negative-boundary control |
| DX12 lane | `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` |
| Physics lane | `tools\validate_physics.bat`; never refresh a baseline without explicit owner approval |
| Replay-prediction spike lane | Analyzer unit tests and `tools\validate_replay_prediction_frame_spikes.bat` |
| Terminal closure | All changed focused gates, independent review, then one `tools\agent_validate.bat --plan-completion` run |

## Acceptance Criteria

- Every external process and conditional skip in the full-validation call graph
  has a reconciled timing/value row; parent wall time has no unexplained material
  residual.
- Warm, leaf-source, broad-header, and fresh-build behavior is reported
  separately with raw artifacts, environment context, and honest variance.
- Every retained blocking stage names unique or intentionally redundant evidence
  and has credible failure proof. Every removed, moved, merged, shortened,
  parallelized, or cached stage has an owner ruling and an equivalent mandatory
  evidence path.
- The final pipeline preserves exact exit codes, fail-fast ownership, coverage
  floors, DX12 validation and baselines, Physics determinism and baseline
  authority, allocation/dependency/ownership gates, and required build
  currentness.
- Pre/post results show the measured critical-path change and explain rejected
  optimization ideas. No arbitrary time budget replaces the evidence review.
- The terminal full run succeeds. If an inherited owner-controlled Physics or
  visual baseline blocks that proof, preserve the exact stop and artifacts and
  obtain owner disposition; do not refresh the oracle or claim full closure.
- Documentation runtime claims are replaced with measured ranges and named
  scenarios rather than one stale approximate duration.

## Non-Goals

- Weakening a correctness, determinism, coverage, allocation, dependency,
  ownership, rendering, or baseline contract to obtain a faster number.
- Refreshing Physics, replay, visual, or performance baselines.
- Optimizing engine runtime except where a validation-only workload or harness
  is proven to dominate and the same defect sensitivity is retained.
- Replacing local mandatory evidence with an optional command nobody runs.
- Treating a faster workstation, warm filesystem cache, hidden background
  process, or reduced logging as a pipeline optimization.
- Adding a frozen duration ratchet that becomes stale as hardware and repository
  size change.

