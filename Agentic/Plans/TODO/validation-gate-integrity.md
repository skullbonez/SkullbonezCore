# Validation Gate Integrity And Continuous Integration

Date: 2026-07-10
Status: Blocked — 5/6 phases complete; V3 requires external GitHub administration and runner infrastructure
Impact area: validation tooling, unit-test projects, GitHub pull-request gates,
sanitizer/static-analysis coverage
Owner: repository validation

## Baseline Problem (V0-V2 Closed 2026-07-10)

At the V0 baseline, the documented broad gate was not a superset of cheaper
validation:

- `tools\validate_full.bat` builds Profile/Debug, runs DX12 validation, and runs
  physics validation, but does not run `tools\validate_tests.bat`.
- `RuntimeInteractionPolicyTests`, `Dx12ArchUnitTests`, and
  `SceneParserUnitTests` have standalone scripts and are not part of the main
  solution or the default broad gate.
- `tools\agent_validate.bat` delegates to `validate_full`, so the "unsure"
  choice can pass while a CPU unit suite fails.
- At the V0 baseline, no tracked CI workflow enforced even the CPU-only gates
  on pull requests.
- At the V0 baseline, no sanitizer or compiler static-analysis lane covered
  native lifetime errors.

The repository therefore had strong local scripts but no single trustworthy
answer to "is this PR safe to merge?"

Current state (2026-07-11): `validate_full` and `agent_validate` reach all four
CPU test targets through `validate_all_cpu_tests` before runtime work, V4
supplies a proven ASan plus bounded `/analyze` lane, and all three workflows are
active on the default branch. Mandatory CPU validation passed a real pull-
request run. The remaining V3 trust gap is a real `merge_group` proof, required
CPU branch protection, and trusted DX12-runner activation; public-PR GPU
evidence still requires an ephemeral isolated runner.

## Goal

One documented PR entry point runs every required CPU test exactly once, then
the smallest required runtime lanes. Pull requests receive an automatic Windows
CPU signal; a GPU-capable runner supplies DX12/runtime evidence when available.

## Design

Separate validation by capability rather than by historical script:

1. **CPU mandatory lane:** format, project/filter checks, staged-size policy,
   Profile build, doctest suite, interaction policy tests, scene parser tests,
   and DX12 architecture tests.
2. **Runtime evidence lane:** Debug/Profile ready builds, DX12 renderer
   validation, and core physics determinism. A persistent GPU machine executes
   trusted `main`/manual refs only; public pull requests require a disposable,
   isolated GPU runner before this evidence can become merge-blocking.
3. **Targeted lanes:** deep physics, replay scrub, interaction clicks, perf,
   graphics stress, and fault injection when touched files require them.
4. **Scheduled safety lane:** sanitizer/static analysis that is informative
   until the initial debt is fixed, then required.

## Phases

- [x] **V0 — Gate graph inventory.** Document every validation script, which
  executable/project it builds, whether it launches the engine, runtime cost,
  and its callers. Acceptance: every test target has one owning umbrella lane.
  Evidence (2026-07-10):
  `Agentic/Reports/validation_gate_inventory_20260710.md` inventories 32 batch
  entry points, 18 owned helpers, all first-party test executables, callers,
  launch behavior, costs, and one mandatory owner per test target.
- [x] **V1 — CPU-test umbrella.** Add `tools\validate_all_cpu_tests.bat` that
  runs `validate_tests`, `validate_runtime_interaction_policy`,
  `validate_scene_parser_tests`, and `validate_dx12_arch_tests` with fail-fast
  exit codes and a combined summary. Avoid rebuilding unchanged dependencies
  where existing scripts support it. Acceptance: inject a deterministic child
  failure at each target and prove the umbrella fails at the named target while
  later targets remain unrun. Executable-level mutation evidence belongs to
  the behavioral-test-depth P6 evidence in
  `Agentic/Reports/behavioral_test_depth_closure_20260711.md`.
  Evidence (2026-07-10): the hermetic failure harness returned child codes 11,
  22, 33, and 44 unchanged at the named targets and marked later targets `NOT
  RUN`; the final production umbrella passed all four targets in 27.796s.
- [x] **V2 — Make broad mean superset.** Run the CPU umbrella from
  `validate_full` before runtime launches; make `agent_validate` call the same
  entry point. Reconcile `validate_fast` so tests are not duplicated when a
  caller already ran the umbrella. Acceptance: an injected CPU-umbrella failure
  makes `validate_full` and `agent_validate` return that failure before building
  Debug or launching the engine; the behavioral-test-depth closure report supplies the
  later real-test mutation drill.
  Evidence (2026-07-10): byte-identical broad-gate harness copies both returned
  injected code 11 before Debug, DX12, physics, or engine execution. Production
  `validate_full` composes `validate_fast --preflight-only`, the CPU umbrella,
  then runtime lanes; `agent_validate` delegates once. The production full gate
  subsequently passed end-to-end: 78/78 doctest cases and 1,883 assertions,
  every standalone CPU target, zero DX12 validation errors and matching visual
  baselines, then a byte-exact 20,001-line physics baseline.
- [ ] **V3 — BLOCKED: Pull-request CI.** Add a `windows-latest` GitHub Actions workflow
  for CPU preflight, every CPU test target, and a Profile engine build, with
  artifact upload on failure and merge-queue coverage. Add a separate
  self-hosted label (`Windows`, `x64`, `dx12`) for trusted `main`/manual runtime
  evidence; never run public-PR code on a persistent machine or treat a skipped
  job as evidence. Require the CPU lane after a real PR/merge-group proof.
  Runtime may become a required PR check only after replacement by an
  ephemeral, disposable, isolated GPU worker. Binding design and activation
  checklist: `Agentic/Reports/validation_ci_v3_20260710.md`.
  Activation audit refreshed 2026-07-11: GitHub registers all three default-
  branch workflows, and `Mandatory CPU validation` passed pull-request run
  29148955729 for `engine-cleanup-10th-july`. DX12 main-push runs 29149260881
  and 29149344794 were correctly skipped because trusted runner activation is
  still disabled; skipped jobs are not evidence. `main` remains unprotected.
  No local implementation is missing. Prove the `merge_group` event next, then
  require the stable CPU job name through branch protection. Keep the
  persistent DX12 lane post-merge/informational until a dedicated trusted-ref
  runner is registered; require public-PR GPU evidence only after an ephemeral
  isolated runner replaces it. Current evidence and remaining administration
  are recorded in `Agentic/Reports/validation_ci_v3_20260710.md`.
- [x] **V4 — Sanitizer/static-analysis lane.** Add an MSVC AddressSanitizer
  configuration for CPU-testable engine code and a bounded `/analyze` or
  equivalent static-analysis job. Record suppressions with owner, reason, and
  deletion condition. Acceptance: one injected heap-use-after-free fixture is
  caught before removing the fixture.
  Evidence (2026-07-10):
  `tools\validate_native_diagnostics.bat --prove-asan-fixture` caught the
  isolated heap-use-after-free with exit 3,
  then passed 78/78 healthy ASan CPU tests and 1,883 assertions. The bounded
  maths `/analyze` lane emitted five fresh evidence sidecars, zero warnings,
  and zero governed suppressions. Complete-output scanning plus bounded
  persisted logs was re-proven in 17.186s. A weekly/manual hosted workflow now
  runs the same detector proof and healthy lanes; it remains informational
  until a real hosted run succeeds. Report:
  `Agentic/Reports/validation_sanitizer_v4_20260710.md`.
- [x] **V5 — Documentation and sustaining rule.** Update `AGENTS.md`, README,
  tools README, and file-to-gate mapping. New standalone test executables must
  register with the CPU umbrella in the same commit. Acceptance: no test script
  is reachable only by tribal knowledge or `validate_select`.
  Evidence (2026-07-10): root, setup, Agentic, and tools documentation describe
  the same CPU-first/two-runtime-lane composition and honest three-process
  count. `Agentic/Tests/*` has an explicit gate mapping, the same-commit umbrella
  registration rule is normative, and selectors expose the CPU umbrella plus
  every individual CPU owner. `validate_select all-cpu-tests` passed all four
  targets through the new selector.

## Dependencies And Safety

- CI configuration must not claim DX12 evidence without a GPU-capable runner,
  and a persistent self-hosted runner must never execute public-PR code.
- V1/V2 should land before plans add more tests, otherwise new suites can remain
  orphaned.
- Behavioral-test-depth P6 closed after V2 and V5; durable evidence is in
  `Agentic/Reports/behavioral_test_depth_closure_20260711.md`.
- DX12 failure-propagation fault-injection tests register through V1; durable
  evidence is in `Agentic/Reports/dx12_failure_inventory_20260710.md`.

## Validation

Changes under `tools/` require `tools\validate_fast.bat`, then the changed
script. Workflow YAML should be syntax-checked locally where practical and
proven by an actual pull-request run before the CI phase is checked complete.

## Definition Of Done

- `validate_full` is a true superset of mandatory CPU validation.
- Every first-party test executable runs through one CPU umbrella.
- `agent_validate` cannot pass with a broken unit test.
- A required Windows CI check protects pull requests.
- GPU validation is required only where a real, safely isolated DX12 runner
  supplies evidence; the persistent trusted-ref workflow is post-merge until
  that stronger boundary exists.
- Sanitizer/static-analysis findings have an owned baseline and no silent
  blanket suppression.
