# Validation Gate Integrity And Continuous Integration

Date: 2026-07-10

Status: Blocked — 5/6 phases complete; V3 requires an eligible GitHub organization destination and a real merge-queue run

Impact area: validation tooling, unit-test projects, GitHub pull-request gates,
sanitizer/static-analysis coverage

Owner: repository validation

Recovery note (2026-07-30): commit `bb729812` deleted this plan as old work,
but `Agentic/Plans/MASTER-PLAN.md` continued to track it as a live blocked row.
The last authoritative pre-deletion plan text is restored here so the blocker,
owner, evidence, and exact unblock conditions remain replayable.

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

Current state (2026-07-30): `validate_full` and `agent_validate` reach all four
CPU test targets through `validate_all_cpu_tests` before runtime work, V4
supplies a proven ASan plus bounded `/analyze` lane, and the hosted CPU and
native-diagnostics workflows are active on the default branch. Mandatory CPU
validation passed real pull-request runs and a fresh manual run with
clang-format 21.1.8 pinned. `main` requires the stable hosted CPU check. By
owner ruling, anything needing a graphics card is local-only validation: no
GitHub workflow, runner, variable, or future merge-blocking GPU lane is allowed.
The remaining V3 trust gap is solely a real `merge_group` proof.

Current external audit (2026-07-30):

- GitHub registers the hosted CPU and native-diagnostics workflows as active.
  Hosted CPU manual run
  30469139071 passed at exact SHA `1faac75e` after the workflow installed and
  verified LLVM 21.1.8, then passed preflight and all 2,423,885 assertions.
- `main` branch protection is active and strict for
  `Mandatory CPU lane (Windows hosted)`, bound to the GitHub Actions app;
  administrators are included and force pushes/deletions remain disabled.
- The rejected self-hosted experiment is fully rolled back. GitHub workflow
  `dx12-runtime-validation.yml` is disabled and deleted; the repository has
  zero self-hosted runners and no `SKULLBONEZ_DX12_CI_ENABLED` variable. The
  host scheduled task, runner directory, and dedicated PowerShell installation
  are removed. Historical runs remain audit evidence, not active CI.
- The repository still has zero `merge_group` runs. GitHub rejected an active
  merge-queue ruleset with HTTP 422 because `SkullbonezCore` is owned by the
  personal account `skullbonez`; GitHub merge queues are available only to
  organization-owned repositories. V3 therefore requires repository transfer
  to an eligible organization (or a future GitHub availability change), then
  merge-queue enablement and one real queued-PR proof. Repository rules also
  prohibit creating or merging that proof PR without explicit user direction.
  A continuation audit found zero organization memberships for the
  authenticated owner, so no existing transfer destination can be selected
  safely.

## Goal

One documented PR entry point runs every required CPU test exactly once.
Pull requests receive an automatic Windows CPU signal. Renderer, screenshot,
InfoQueue, graphics-stress, and any other graphics-card validation run locally
and never on GitHub Actions.

## Design

Separate validation by capability rather than by historical script:

1. **CPU mandatory lane:** format, project/filter checks, staged-size policy,
   Profile build, doctest suite, interaction policy tests, scene parser tests,
   and DX12 architecture tests.
2. **Local runtime evidence lane:** Debug/Profile ready builds, DX12 renderer
   validation, screenshots, graphics stress, and core physics determinism run
   only on a local developer machine. No GitHub-hosted, self-hosted,
   persistent, ephemeral, or merge-blocking GPU lane is permitted.
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
  Debug or launching the engine; the behavioral-test-depth closure report
  supplies the later real-test mutation drill.
  Evidence (2026-07-10): byte-identical broad-gate harness copies both returned
  injected code 11 before Debug, DX12, physics, or engine execution. Production
  `validate_full` composes `validate_fast --preflight-only`, the CPU umbrella,
  then runtime lanes; `agent_validate` delegates once. The production full gate
  subsequently passed end-to-end: 78/78 doctest cases and 1,883 assertions,
  every standalone CPU target, zero DX12 validation errors and matching visual
  baselines, then a byte-exact 20,001-line physics baseline.
- [ ] **V3 — BLOCKED: Pull-request CI.** Add a `windows-latest` GitHub Actions
  workflow for CPU preflight, every CPU test target, and a Profile engine build,
  with artifact upload on failure and merge-queue coverage. Require the CPU
  lane after a real PR/merge-group proof. Do not add any GitHub workflow or
  runner for validation that needs a graphics card; those gates are local-only.
  Binding design and activation evidence:
  `Agentic/Reports/validation_ci_v3_20260710.md`.
  Activation audit refreshed 2026-07-30: clang-format 21.1.8 is pinned and
  hosted CPU run 30469139071 passes; `main` strictly requires the stable CPU
  job. The owner rejected GitHub graphics-card validation, so the experimental
  DX12 workflow and all runner infrastructure were removed. The only V3
  acceptance item left is a real `merge_group` proof, but GitHub rejects
  merge-queue enablement because this public repository is user-owned rather
  than organization-owned. Transfer to an eligible organization or wait for
  GitHub availability to change. The authenticated owner currently has zero
  organization memberships, so first create or join an organization that can
  receive the repository; then explicitly authorize creation/enqueue of a
  proof PR. Current evidence and the exact external boundary are recorded in
  `Agentic/Reports/validation_ci_v3_20260710.md`.
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
