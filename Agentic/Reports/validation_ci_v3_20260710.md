# Validation CI V3 Evidence — 2026-07-10

Plan: `Agentic/Plans/TODO/validation-gate-integrity.md` V3

Owner: repository validation

Branch: `engine-cleanup-10th-july`

## Result

Two capability-honest hosted workflows remain active on the default branch:

- `.github/workflows/mandatory-cpu-validation.yml` runs the mandatory hosted
  lane on `windows-latest`: `validate_fast --preflight-only` followed by the
  complete CPU test umbrella. Its stable check name is
  `Mandatory CPU lane (Windows hosted)`. It makes no renderer, GPU, or
  DX12-runtime claim. It covers pull requests and manual dispatch.
- `.github/workflows/native-diagnostics.yml` supplies the separate V4 weekly/
  manual hosted diagnostics lane.

Owner ruling (2026-07-30): anything needing a graphics card is local-only
validation. The experimental `.github/workflows/dx12-runtime-validation.yml`,
self-hosted runner, enable variable, scheduled task, and dedicated local
installations were removed. No persistent or ephemeral GitHub GPU lane may
replace them. The historical design and run evidence below are retained only
to make the rejected experiment auditable.

## Hosted CPU Lane

The hosted workflow runs on pull requests and manual dispatch. Checkout uses
`fetch-depth: 0` so the exact pull-request base is available locally. The lane
exports `SKORE_SIZE_DIFF_BASE` from the pull-request base SHA before one
fail-fast PowerShell loop runs these children in order:

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

Pull-request runs therefore ask staged-size policy to inspect the exact changed
HEAD blobs relative to the event's base commit. Manual dispatch has no
pull-request field, so `SKORE_SIZE_DIFF_BASE` is empty and the checker falls
back to local-index mode. Because checkout is clean, a manual run is **not**
changed-file size evidence and must not be cited as such.

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

## Superseded Self-Hosted Runtime Lane (Historical)

This section describes the rejected experiment and is not an active repository
contract. The owner subsequently ruled all graphics-card validation local-only.

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

## Historical Local Static Validation

This section records the pre-retirement workflow shape. The final
pull-request/manual-only validation is recorded in the closure section below.

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

The self-hosted/GPU rows below are historical execution evidence, not current
requirements. The owner ruling above retires that entire GitHub GPU path.

- [x] Push the workflow files and observe one successful pull-request run of
  `Mandatory CPU lane (Windows hosted)`. Evidence: run 29148955729 completed
  successfully on 2026-07-11.
- [ ] Exercise the merge queue and prove the same CPU check runs for a
  `merge_group` event rather than remaining permanently expected. Confirm the
  log names that event's `base_sha` for changed-file size inspection.
- [x] Add that exact CPU job name to the protected branch's required checks.
  Evidence: `main` strictly requires `Mandatory CPU lane (Windows hosted)`,
  bound to GitHub Actions app ID 15368, with administrators included.
- [x] Register exactly one dedicated Windows x64 Actions runner with the custom
  `dx12` label; ensure the effective labels are `self-hosted`, `Windows`,
  `X64`, and `dx12`. Runner `skullbonez-dx12-sesch-rtx3080` is the sole
  registered self-hosted runner and was online/idle at the final audit.
- [x] Install Visual Studio C++/LLVM tools, Windows SDK/debug layer, Git,
  Python, and Pillow as described by `FIRST_TIME_SETUP.md`; additionally
  install PowerShell 7.2 or newer and update the Actions runner to at least
  `2.327.1`. The runner is 2.336.0 and uses PowerShell 7.6.4.
- [x] Confirm the runner service account can access the physical DX12 adapter
  and write the repository's build/validation directories. Trusted run
  30472584471 exercised the NVIDIA GeForce RTX 3080, wrote all evidence, and
  completed the full gate.
- [x] Set repository variable `SKULLBONEZ_DX12_CI_ENABLED=true` only after the
  runner is registered. API readback confirms `true`.
- [x] Prove several trusted `main` push/manual runs, including zero InfoQueue
  errors, matching screenshot baselines, and byte-exact physics output. Manual
  dispatch must select only a maintainer-reviewed ref. Runs 30472584471,
  30473669720, and 30473675109 all passed at reviewed exact SHA `f4c0b33e`.
- [x] Keep `Runtime validation (self-hosted DX12)` post-merge/informational. Do
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

## Live Activation Audit — 2026-07-30

The owner approved the formatter repair and required GitHub administration.
The implementation and all viable activation work are now complete:

- `.github/workflows/mandatory-cpu-validation.yml` installs exact official
  LLVM 21.1.8 through Chocolatey, verifies that version and repository style
  parsing, exports its explicit `clang-format.exe` path, and records the tool
  identity. Manual hosted run
  [30469139071](https://github.com/skullbonez/SkullbonezCore/actions/runs/30469139071)
  passed at exact SHA `1faac75e750c045ceaf50addbc4a9ca3d34f772b`:
  preflight passed and all 465 cases / 2,423,885 assertions plus the complete
  CPU umbrella passed.
- `main` branch protection is active, strict, and enforced for administrators.
  Its sole required check is `Mandatory CPU lane (Windows hosted)`, bound to
  GitHub Actions app ID 15368. Force pushes and deletions are disabled.
- Exactly one repository runner is registered:
  `skullbonez-dx12-sesch-rtx3080` (runner 2.336.0), with effective labels
  `self-hosted`, `Windows`, `X64`, and `dx12`. It uses PowerShell 7.6.4 and an
  NVIDIA GeForce RTX 3080. Because this shell is non-administrative, the runner
  is launched by the user-level at-logon scheduled task
  `SkullbonezCore-GitHub-DX12-Runner`, not a Windows service; it is therefore
  intentionally unavailable before that user logs in.
- The runner package SHA-256 is
  `d59123a43003e357b0805b5d0f611d0bd2f65ab67d51bd070dd4e7a0f685c162`.
  The PowerShell package SHA-256 is
  `80832551c52809301e6071c8bac977beb5a2f1ec953eb4db9f94deb953333793`.
  Repository variable `SKULLBONEZ_DX12_CI_ENABLED` reads back as `true`.
- `.github/workflows/dx12-runtime-validation.yml` now checks out recursive
  submodules, repairing the absent Tracy/imgui `Related:` evidence observed in
  the first live attempt. Three trusted manual runs—
  [30472584471](https://github.com/skullbonez/SkullbonezCore/actions/runs/30472584471),
  [30473669720](https://github.com/skullbonez/SkullbonezCore/actions/runs/30473669720),
  and
  [30473675109](https://github.com/skullbonez/SkullbonezCore/actions/runs/30473675109)—passed
  at exact SHA `f4c0b33e11c4e7cf5e1dc702f7d1b45929cc437f`:
  all CPU tests passed, DX12 reported zero InfoQueue validation errors,
  screenshots matched, `physics_regression_varied.csv` was byte-exact across
  two output runs and one baseline, deep Physics and performance passed, and
  `VALIDATE_FULL: DEFAULT GATE PASSED`. Artifact
  [dx12-runtime-30472584471-1](https://github.com/skullbonez/SkullbonezCore/actions/runs/30472584471/artifacts/8732551823)
  has SHA-256
  `bc1473ca9c262b83c7670a8af3a27310bc31573e1095111668b8f398f411e1fa`.
- The persistent DX12 workflow remains trusted-only: `main` push and manual
  dispatch, never `pull_request`, and it is not a required check. An ephemeral
  isolated GPU worker remains a conditional future requirement only if GPU
  evidence is later made merge-blocking.

The final merge-queue administration is not available on this repository.
`SkullbonezCore` is public but owned by the personal account `skullbonez`.
GitHub documents merge queues as available only for organization-owned public
repositories (or qualifying organization-owned private repositories). A
dedicated active `Main merge queue` ruleset POST therefore returned HTTP 422
`Invalid rule 'merge_queue'`; no partial ruleset was created, the existing
disabled `Block Delete & Force Push` ruleset was untouched, and the repository
still reports zero `merge_group` runs.

V3 remains blocked at 5/6 solely on the real `merge_group` proof. The exact
unblock is: create or join an eligible organization that can receive the
repository, transfer it there (or wait for GitHub to expand feature
availability), enable the merge queue on `main`, then explicitly authorize
creation and enqueue of a proof pull request. The authenticated owner currently
reports zero organization memberships, so there is no existing transfer
destination to select. Creating an organization, transferring the repository,
and creating or merging that PR remain outside current authority.

## Owner-Directed GPU Rollback — 2026-07-30

The owner ruled that there will be no DX12 validation on GitHub and that
anything requiring a graphics card is local-only validation. The rollback
removed every active component of the rejected experiment:

- disabled GitHub workflow ID 311199536 and deleted tracked
  `.github/workflows/dx12-runtime-validation.yml`;
- deleted repository variable `SKULLBONEZ_DX12_CI_ENABLED`;
- unregistered runner ID 22, `skullbonez-dx12-sesch-rtx3080`;
- stopped its one listener process and removed scheduled task
  `SkullbonezCore-GitHub-DX12-Runner`;
- moved the dedicated runner directory and PowerShell 7.6.4 installation to
  the Windows Recycle Bin.

The hosted CPU workflow, its strict `main` branch protection, and the
device-free DX12 architecture tests remain. Real renderer, InfoQueue,
screenshot, graphics-stress, and other GPU-dependent gates run locally only.
Historical workflow runs and artifacts remain finite-retention audit evidence;
they do not authorize or reactivate a GitHub GPU lane.

## Owner-Directed Merge-Queue Retirement — 2026-07-30

The owner will not change the GitHub repository or its ownership for merge
queues and directed that the entire idea be removed. The final V3 contract is:

- hosted CPU CI runs on `pull_request` and `workflow_dispatch` only;
- `main` strictly requires `Mandatory CPU lane (Windows hosted)`;
- `SKORE_SIZE_DIFF_BASE` uses only `pull_request.base.sha`;
- no `merge_group` trigger, merge-queue requirement, organization transfer, or
  proof PR remains;
- all graphics-card validation is local-only.

The historical merge-queue investigation above remains audit evidence, not an
active requirement.
