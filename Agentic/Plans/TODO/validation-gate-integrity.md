# Validation Gate Integrity And Continuous Integration

Date: 2026-07-10
Status: Planned — 0/6 phases complete
Impact area: validation tooling, unit-test projects, GitHub pull-request gates,
sanitizer/static-analysis coverage
Owner: repository validation

## Problem

The documented broad gate is not a superset of cheaper validation:

- `tools\validate_full.bat` builds Profile/Debug, runs DX12 validation, and runs
  physics validation, but does not run `tools\validate_tests.bat`.
- `RuntimeInteractionPolicyTests`, `Dx12ArchUnitTests`, and
  `SceneParserUnitTests` have standalone scripts and are not part of the main
  solution or the default broad gate.
- `tools\agent_validate.bat` delegates to `validate_full`, so the "unsure"
  choice can pass while a CPU unit suite fails.
- No tracked CI workflow enforces even the CPU-only gates on pull requests.
- No sanitizer or compiler static-analysis lane covers native lifetime errors.

The repository therefore has strong local scripts but no single trustworthy
answer to "is this PR safe to merge?"

## Goal

One documented PR entry point runs every required CPU test exactly once, then
the smallest required runtime lanes. Pull requests receive an automatic Windows
CPU signal; a GPU-capable runner supplies DX12/runtime evidence when available.

## Design

Separate validation by capability rather than by historical script:

1. **CPU mandatory lane:** format, project/filter checks, staged-size policy,
   Profile build, doctest suite, interaction policy tests, scene parser tests,
   and DX12 architecture tests.
2. **Runtime mandatory lane:** Debug/Profile ready builds, DX12 renderer
   validation, and core physics determinism.
3. **Targeted lanes:** deep physics, replay scrub, interaction clicks, perf,
   graphics stress, and fault injection when touched files require them.
4. **Scheduled safety lane:** sanitizer/static analysis that is informative
   until the initial debt is fixed, then required.

## Phases

- [ ] **V0 — Gate graph inventory.** Document every validation script, which
  executable/project it builds, whether it launches the engine, runtime cost,
  and its callers. Acceptance: every test target has one owning umbrella lane.
- [ ] **V1 — CPU-test umbrella.** Add `tools\validate_all_cpu_tests.bat` that
  runs `validate_tests`, `validate_runtime_interaction_policy`,
  `validate_scene_parser_tests`, and `validate_dx12_arch_tests` with fail-fast
  exit codes and a combined summary. Avoid rebuilding unchanged dependencies
  where existing scripts support it. Acceptance: deliberately fail one test in
  each target and prove the umbrella fails at the named target.
- [ ] **V2 — Make broad mean superset.** Run the CPU umbrella from
  `validate_full` before runtime launches; make `agent_validate` call the same
  entry point. Reconcile `validate_fast` so tests are not duplicated when a
  caller already ran the umbrella. Acceptance: a broken unit test makes
  `validate_full` and `agent_validate` fail before launching the engine.
- [ ] **V3 — Pull-request CI.** Add a `windows-latest` GitHub Actions workflow
  for the CPU mandatory lane with artifact upload on failure. Add a separate
  self-hosted label (`Windows`, `x64`, `dx12`) for the runtime lane; do not fake
  GPU success on a hosted runner. Require the CPU lane immediately and require
  the runtime lane once a runner is registered and stable.
- [ ] **V4 — Sanitizer/static-analysis lane.** Add an MSVC AddressSanitizer
  configuration for CPU-testable engine code and a bounded `/analyze` or
  equivalent static-analysis job. Record suppressions with owner, reason, and
  deletion condition. Acceptance: one injected heap-use-after-free fixture is
  caught before removing the fixture.
- [ ] **V5 — Documentation and sustaining rule.** Update `AGENTS.md`, README,
  tools README, and file-to-gate mapping. New standalone test executables must
  register with the CPU umbrella in the same commit. Acceptance: no test script
  is reachable only by tribal knowledge or `validate_select`.

## Dependencies And Safety

- CI configuration must not claim DX12 evidence without a GPU-capable runner.
- V1/V2 should land before plans add more tests, otherwise new suites can remain
  orphaned.
- `behavioral-test-depth.md` P6 closes only after V2 and V5 complete.
- `dx12-failure-propagation.md` fault-injection tests register through V1.

## Validation

Changes under `tools/` require `tools\validate_fast.bat`, then the changed
script. Workflow YAML should be syntax-checked locally where practical and
proven by an actual pull-request run before the CI phase is checked complete.

## Definition Of Done

- `validate_full` is a true superset of mandatory CPU validation.
- Every first-party test executable runs through one CPU umbrella.
- `agent_validate` cannot pass with a broken unit test.
- A required Windows CI check protects pull requests.
- GPU validation is required only where a real DX12 runner supplies evidence.
- Sanitizer/static-analysis findings have an owned baseline and no silent
  blanket suppression.
