# Validation Gate Integrity Closure

Date: 2026-07-30

Branch: `nightrunner-30th-JUL-26`

Plan: completed V0-V5 and removed from `Agentic/Plans/TODO/` under inventory
rule 4.

State: Complete

## Owner Decisions

The owner retired the merge-queue proposal rather than changing GitHub
repository ownership. No organization membership, repository transfer,
`merge_group` trigger, merge-queue ruleset, or queued proof pull request remains
in the validation contract.

The owner also ruled that every validation path requiring a graphics card is
local-only. The closing branch retains no GPU workflow file; GitHub has no
registered self-hosted runner, enablement variable, or active graphics-card
gate. Real renderer, InfoQueue, screenshot, and graphics-stress evidence is
produced locally.

## Acceptance

- V0 inventoried the validation entry points, test targets, CI coverage, and
  false-pass risks.
- V1 established one complete CPU test umbrella.
- V2 made full and agent validation consume that umbrella with fail-fast exit
  propagation.
- V3 provides one required `windows-latest` CPU check for pull requests plus
  manual dispatch. The stable check is
  `Mandatory CPU lane (Windows hosted)`.
- V3's final workflow has no `merge_group`, self-hosted, GPU, or
  `validate_full.bat` branch. Pull-request changed-file policy receives only
  `pull_request.base.sha`.
- V4 provides the separate scheduled/manual native diagnostics lane.
- V5 documents the validation matrix, evidence ownership, and sustaining
  rules.

## Hosted Proof And External State

GitHub Actions run
[30505659321](https://github.com/skullbonez/SkullbonezCore/actions/runs/30505659321)
passed on exact commit
`47a95da000234023d5255431eeea122da70a96a8`. Its log records:

- pinned clang-format 21 installation;
- `VALIDATE_FAST: PREFLIGHT PASSED`;
- 465/465 doctest cases and 2,423,885/2,423,885 assertions;
- `VALIDATE_COVERAGE: ALL PASSED`;
- `VALIDATE_ALL_CPU_TESTS: ALL PASSED`.

This manual run proves the final workflow and CPU lane on the exact retirement
commit. It is not cited as pull-request changed-file-size evidence because
manual dispatch has no pull-request base SHA.

The closing GitHub readback found:

- `main` strictly requires `Mandatory CPU lane (Windows hosted)`;
- zero registered self-hosted runners;
- zero repository Actions variables;
- the hosted CPU and native diagnostics workflows active;
- the deleted DX12 workflow disabled as historical default-branch
  registration.

## Local Validation And Review

- actionlint 1.7.12: both remaining workflow files pass;
- PyYAML structural audit: exactly the hosted CPU and native diagnostics
  workflow files remain, the CPU trigger set is exactly `pull_request` plus
  `workflow_dispatch`, and no workflow contains self-hosted or GPU enablement;
- `tools\validate_fast.bat`: pass;
- `python tools\validate_native_diagnostics.py --self-test`: pass;
- `git diff --check`: pass;
- independent read-only closure review: passed with zero blocking findings.

No engine source, runtime behavior, baseline, golden, or graphics-card
validation policy changed in this documentation-only closure slice. Detailed V3
design and historical activation evidence remain in
`../validation_ci_v3_20260710.md`.
