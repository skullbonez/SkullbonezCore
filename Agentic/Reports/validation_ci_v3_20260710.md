# Validation CI V3 Evidence — 2026-07-10

Plan: `Agentic/Plans/TODO/validation-gate-integrity.md` V3

Owner: repository validation

Branch: `engine-cleanup-10th-july`

## Result

Three capability-honest workflows are active on the default branch. The two V3
workflows are:

- `.github/workflows/mandatory-cpu-validation.yml` runs the mandatory hosted
  lane on `windows-latest`: `validate_fast --preflight-only` followed by the
  complete CPU test umbrella. Its stable check name is
  `Mandatory CPU lane (Windows hosted)`. It makes no renderer, GPU, or
  DX12-runtime claim. It covers pull requests, merge-queue `merge_group`
  checks, and manual dispatch.
- `.github/workflows/dx12-runtime-validation.yml` runs
  `tools\validate_full.bat` only on a runner matching all four labels:
  `self-hosted`, `Windows`, `x64`, and `dx12`. Its stable check name is
  `Runtime validation (self-hosted DX12)`. Because this repository is public,
  it has no pull-request trigger; it covers trusted pushes to `main` and
  privileged manual dispatch only.

Both workflows have read-only repository permissions, use immutable commit pins
for the official checkout and artifact actions, capture their batch exit code
without losing it through PowerShell logging, and bound execution/artifact
retention.

`.github/workflows/native-diagnostics.yml` supplies the separate V4 weekly/
manual hosted diagnostics lane. It does not change the V3 CPU/runtime security
boundary.

## Hosted CPU And Merge-Queue Lane

The hosted workflow runs on pull requests, merge-queue `merge_group` events,
and manual dispatch. Checkout uses `fetch-depth: 0` so the exact event base is
available locally. The lane exports `SKORE_SIZE_DIFF_BASE` from the pull
request base SHA or merge-group base SHA before one fail-fast PowerShell loop
runs these children in order:

1. `tools\validate_fast.bat --preflight-only` owns formatting, production
   project/filter metadata, staged-size policy, and its Profile preflight build
   plus ready-build checks without launching the doctest runner.
2. `tools\validate_all_cpu_tests.bat` runs every first-party CPU test target
   exactly once.

The loop writes every child's output and attributed exit code to the single
`TestOutput/validation/ci/mandatory-cpu-lane.log` transcript, stops at the
first failure, and returns that child's exit code unchanged. On a lane-step
failure it uploads all evidence that exists at that point, such as the
transcript, project-filter summaries, and available build logs, as
`mandatory-cpu-failure-<run-id>-<attempt>`.

No workflow can promise artifacts after failures that happen before checkout,
before the context step creates the artifact directory, or while the artifact
action itself is unavailable. The GitHub Actions log remains the evidence for
those earlier failures.

Pull-request and merge-group runs therefore ask staged-size policy to inspect
the exact changed HEAD blobs relative to the event's base commit. Manual
dispatch has neither event field, so `SKORE_SIZE_DIFF_BASE` is empty and the
checker falls back to local-index mode. Because checkout is clean, a manual run
is **not** changed-file size evidence and must not be cited as such.

Independent review found that filtering only added/modified Git statuses could
miss a rename from an allowlisted data directory into an ordinary path. Both
index and base-ref modes now disable rename detection, making the destination
an added blob that must satisfy its new path's policy. An isolated Git fixture
moved `SkullbonezData/large.bin` to `docs/large.bin`; with a one-byte test
budget, the checker named the destination and exited 1.

This hosted composition deliberately stops before the runtime portions of
`validate_full`: GitHub's hosted Windows runner is not accepted as renderer
evidence and does not launch the engine. `--preflight-only` prevents the main
doctest runner from executing before the umbrella, so the doctest,
interaction-policy, scene-parser, and device-free DX12 architecture targets
each run exactly once.

## Self-Hosted Runtime Lane

The runtime workflow is intentionally absent from pull requests. It responds
only to a reviewed commit pushed to `main` or a privileged maintainer's manual
dispatch, and the job runs only when repository variable
`SKULLBONEZ_DX12_CI_ENABLED` is exactly `true`. Until then it is skipped; a
skipped job is **not** DX12 evidence.

This boundary is mandatory for a public repository. Pull-request workflow YAML
and scripts come from the proposed merge ref and are attacker-controlled for a
fork contribution. A persistent self-hosted machine must never execute that
code, even with a same-repository-name or label expression intended as a guard.

When enabled, the job:

1. Targets only `[self-hosted, Windows, x64, dx12]`.
2. Records the runner identity and Windows video-controller/driver inventory.
3. Runs the repository's broad `tools\validate_full.bat` entry point, which
   supplies CPU, real DX12 renderer/InfoQueue/screenshot, and deterministic
   physics evidence.
4. Attempts to upload all evidence available when the artifact step is reached,
   including runtime logs, GPU identity, DX12 captures and comparisons,
   InfoQueue output, and the generated core physics CSV.

The workflow has no `concurrency` block. Exactly one dedicated runner must carry
the complete label set, so the native GitHub runner queue serializes every
runtime job and preserves pending runs. Workflow concurrency is not a durable
queue: it keeps at most one pending run and can cancel an older pending run.

`Runtime validation (self-hosted DX12)` therefore cannot be a required
pull-request check in this design. Making runtime evidence merge-blocking would
require replacing the persistent runner with a genuinely ephemeral,
disposable, and isolated GPU worker whose machine and credentials are destroyed
after each untrusted run. That replacement needs a separate security review;
renaming or conditionally skipping this persistent job is not sufficient.

## Action Supply Chain

Official action releases were resolved on 2026-07-10 and pinned to immutable
commit SHAs:

| Action | Release | Commit |
|---|---|---|
| `actions/checkout` | `v7.0.0` | `9c091bb21b7c1c1d1991bb908d89e4e9dddfe3e0` |
| `actions/upload-artifact` | `v7.0.1` | `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` |

These Node 24 action releases require a self-hosted Actions runner at version
`2.327.1` or newer. The hosted runner is maintained by GitHub; the DX12 runner
owner must keep its agent current. The runtime steps also require PowerShell
7.2 or newer (`pwsh`).

## Local Static Validation

No repository validation or engine launch is appropriate for workflow-only
configuration. The downloaded `actionlint` v1.7.12 Windows binary matched its
published SHA-256
`6e7241b51e6817ea6a047693d8e6fed13b31819c9a0dd6c5a726e1592d22f6e9`.
The hosted workflow passed `actionlint` directly. The runtime workflow passed
with only the narrow `label .+ is unknown` diagnostic ignored because `dx12`
is intentionally a repository-owned self-hosted label that cannot exist in
actionlint's built-in GitHub-hosted label catalog.

A separate PyYAML structural assertion returned `workflow_structure_exit=0`
and proved the CPU pull-request/merge-group/manual triggers, the runtime
main-push/manual-only triggers, read-only permissions, stable one-job layouts,
immutable 40-character action pins, full-history CPU checkout, the exact
PR/merge base expression, exact script entry points, absence of runtime
workflow concurrency, failure/evidence artifact steps, the enable variable,
and exact runtime label vector
`[self-hosted, Windows, x64, dx12]`.

Final local check output after the public-repository safety and mandatory
hosted-lane revisions:

```text
actionlint_cpu_exit=0
actionlint_runtime_exit=0
workflow_structure_exit=0
cpu_triggers=pull_request,merge_group,workflow_dispatch
cpu_check_name=Mandatory CPU lane (Windows hosted)
cpu_checkout_fetch_depth=0
cpu_size_diff_base_expression=pull_request.base.sha||merge_group.base_sha||empty
manual_dispatch_size_evidence=false
cpu_size_diff_base_child_env_probe=0
manual_dispatch_empty_base_child_env_probe=0
cpu_gate_order=validate_fast_preflight,validate_all_cpu_tests
cpu_direct_test_umbrella_invocations=1
cpu_explicit_profile_build_invocations=0
runtime_triggers=push(main),workflow_dispatch
runtime_pull_request_trigger=false
workflow_concurrency_blocks=0
runtime_labels=self-hosted,Windows,x64,dx12
cpu_lane_probe=preflight expected_exit=31 actual_exit=31 executed=1 later=NOT_RUN
cpu_lane_probe=cpu_umbrella expected_exit=32 actual_exit=32 executed=2 later=N/A
cpu_lane_probe=success expected_exit=0 actual_exit=0 executed=2 later=ALL_RUN
runtime_logging_probe_expected=41 actual=41
```

The local Windows PowerShell probes use the same ordered native-command-to-
`Tee-Object` control flow as the workflow step. They prove exact child-code
propagation, later-gate suppression, one two-gate success transcript, and
that transcript capture does not replace a batch failure with a successful
pipeline result. Hosted run 29148955729 now proves the healthy path under its
PowerShell 7 `pwsh` environment; the injected failure-code cases remain local
mutation evidence by design.

V3 remains incomplete. A real pull-request CPU run now passes; a merge-queue
CPU run, CPU branch protection, and trusted runtime runs are still required.
Local validation cannot prove repository policy, runner registration,
interactive DX12 access, or branch protection.

## Live Activation Audit — 2026-07-11

Read-only GitHub inspection was refreshed after default-branch integration:

- default branch `main`; repository is public;
- `Mandatory CPU validation`, `DX12 runtime validation`, and
  `Native diagnostics` are registered and active;
- pull-request run
  `https://github.com/skullbonez/SkullbonezCore/actions/runs/29148955729`
  completed successfully for `engine-cleanup-10th-july`;
- DX12 main-push runs 29149260881 and 29149344794 completed as skipped, which
  correctly supplies no runtime evidence while runner activation is disabled;
- `main` is not protected.

Both V3 workflow files still pass the downloaded actionlint 1.7.12 binary
(`ACTIONLINT_CPU_EXIT=0`, `ACTIONLINT_RUNTIME_EXIT=0`), with only the documented
custom-label diagnostic ignored for the DX12 workflow. This proves the local
configuration remains actionable but cannot substitute for hosted execution.

The remaining blocker is external GitHub administration, not missing local
implementation. Changing repository settings and registering runners are
outside this campaign's authority. The remaining activation order is:

1. Exercise the merge queue and prove a real `merge_group` event.
2. Add `Mandatory CPU lane (Windows hosted)` to `main` branch protection after
   that proof, avoiding a permanently expected merge-queue check.
3. Register and harden exactly one trusted-ref persistent DX12 runner, set the
   enable variable, and keep that lane post-merge/informational.
4. If GPU evidence must block pull requests, replace the persistent machine
   with an ephemeral isolated worker and security-review that boundary first.

V3 unblocks only when run URLs/logs, branch-protection state, runner labels,
repository-variable state, and trusted runtime evidence can be recorded below.

## External Activation Checklist

- [x] Push the workflow files and observe one successful pull-request run of
  `Mandatory CPU lane (Windows hosted)`. Evidence: run 29148955729 completed
  successfully on 2026-07-11.
- [ ] Exercise the merge queue and prove the same CPU check runs for a
  `merge_group` event rather than remaining permanently expected. Confirm the
  log names that event's `base_sha` for changed-file size inspection.
- [ ] Add that exact CPU job name to the protected branch's required checks.
- [ ] Register exactly one dedicated Windows x64 Actions runner with the custom
  `dx12` label; ensure the effective labels are `self-hosted`, `Windows`,
  `x64`, and `dx12`. Do not assign that complete label set to a second runner.
- [ ] Install Visual Studio C++/LLVM tools, Windows SDK/debug layer, Git,
  Python, and Pillow as described by `FIRST_TIME_SETUP.md`; additionally
  install PowerShell 7.2 or newer and update the Actions runner to at least
  `2.327.1`.
- [ ] Confirm the runner service account can access the physical DX12 adapter
  and write the repository's build/validation directories.
- [ ] Set repository variable `SKULLBONEZ_DX12_CI_ENABLED=true` only after the
  runner is registered.
- [ ] Prove several trusted `main` push/manual runs, including zero InfoQueue
  errors, matching screenshot baselines, and byte-exact physics output. Manual
  dispatch must select only a maintainer-reviewed ref.
- [ ] Keep `Runtime validation (self-hosted DX12)` post-merge/informational. Do
  not add this persistent-runner job to pull-request required checks.
- [ ] If merge-blocking GPU evidence is required, replace this lane with a
  truly ephemeral/disposable isolated GPU runner, security-review it against
  fork code and credential persistence, and only then consider it for branch
  protection.

Branch-protection rules, repository variables, and runner registration are
GitHub administrative state; committing workflow YAML cannot set or prove any
of them. V3 must not be checked complete merely because these files exist.

## Live Activation Audit — 2026-07-12

The final MASTER execution pass rechecked GitHub after all local plans closed:

- `Mandatory CPU validation` pull-request run 29179364775 succeeded for
  `nightrunner-11th-july` on 2026-07-12, providing a second healthy hosted PR
  example.
- The latest 20 workflow runs contain no `merge_group` event, so merge-queue
  base-SHA handling still lacks real hosted proof.
- `GET /repos/skullbonez/SkullbonezCore/branches/main/protection` returns HTTP
  404 `Branch not protected`; the stable CPU job is therefore not required.
- DX12 runtime run 29179368932 on `main` is skipped. No dedicated runner with
  effective labels `self-hosted`, `Windows`, `x64`, and `dx12` is active, and
  skipped work remains non-evidence.
- The authenticated CLI token has repository/workflow scopes, but the missing
  merge-queue event and runner machine cannot be manufactured by a source-tree
  change. Repository rules also prohibit creating or merging a PR without an
  explicit user request.

V3 remains blocked at 5/6. Completion requires an administrator to enable and
exercise the merge queue, protect `main` with `Mandatory CPU lane (Windows
hosted)`, and provision the trusted or ephemeral DX12 runner described above.
